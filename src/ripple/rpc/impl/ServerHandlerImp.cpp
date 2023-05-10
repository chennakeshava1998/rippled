//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/beast/net/IPAddressConversion.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/core/JobQueue.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/net/RPCErr.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/resource/Fees.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/ServerHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/ServerHandlerImp.h>
#include <ripple/rpc/impl/Tuning.h>
#include <ripple/rpc/json_body.h>
#include <ripple/server/Server.h>
#include <ripple/server/SimpleWriter.h>
#include <ripple/server/impl/JSONRPCUtil.h>
#include <boost/algorithm/string.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/type_traits.hpp>
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <boost/json.hpp>

namespace ripple {

static bool
isStatusRequest(http_request_type const& request)
{
    return request.version() >= 11 && request.target() == "/" &&
        request.body().size() == 0 &&
        request.method() == boost::beast::http::verb::get;
}

static Handoff
statusRequestResponse(
    http_request_type const& request,
    boost::beast::http::status status)
{
    using namespace boost::beast::http;
    Handoff handoff;
    response<string_body> msg;
    msg.version(request.version());
    msg.result(status);
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "text/html");
    msg.insert("Connection", "close");
    msg.body() = "Invalid protocol.";
    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return handoff;
}

// VFALCO TODO Rewrite to use boost::beast::http::fields
static bool
authorized(Port const& port, std::map<std::string, std::string> const& h)
{
    if (port.user.empty() || port.password.empty())
        return true;

    auto const it = h.find("authorization");
    if ((it == h.end()) || (it->second.substr(0, 6) != "Basic "))
        return false;
    std::string strUserPass64 = it->second.substr(6);
    boost::trim(strUserPass64);
    std::string strUserPass = base64_decode(strUserPass64);
    std::string::size_type nColon = strUserPass.find(":");
    if (nColon == std::string::npos)
        return false;
    std::string strUser = strUserPass.substr(0, nColon);
    std::string strPassword = strUserPass.substr(nColon + 1);
    return strUser == port.user && strPassword == port.password;
}

ServerHandlerImp::ServerHandlerImp(
    Application& app,
    boost::asio::io_service& io_service,
    JobQueue& jobQueue,
    NetworkOPs& networkOPs,
    Resource::Manager& resourceManager,
    CollectorManager& cm)
    : app_(app)
    , m_resourceManager(resourceManager)
    , m_journal(app_.journal("Server"))
    , m_networkOPs(networkOPs)
    , m_server(make_Server(*this, io_service, app_.journal("Server")))
    , m_jobQueue(jobQueue)
{
    auto const& group(cm.group("rpc"));
    rpc_requests_ = group->make_counter("requests");
    rpc_size_ = group->make_event("size");
    rpc_time_ = group->make_event("time");
}

ServerHandlerImp::~ServerHandlerImp()
{
    m_server = nullptr;
}

void
ServerHandlerImp::setup(Setup const& setup, beast::Journal journal)
{
    setup_ = setup;
    m_server->ports(setup.ports);
}

//------------------------------------------------------------------------------

void
ServerHandlerImp::stop()
{
    m_server->close();
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return stopped_; });
    }
}

//------------------------------------------------------------------------------

bool
ServerHandlerImp::onAccept(
    Session& session,
    boost::asio::ip::tcp::endpoint endpoint)
{
    auto const& port = session.port();

    auto const c = [this, &port]() {
        std::lock_guard lock(mutex_);
        return ++count_[port];
    }();

    if (port.limit && c >= port.limit)
    {
        JLOG(m_journal.trace())
            << port.name << " is full; dropping " << endpoint;
        return false;
    }

    return true;
}

Handoff
ServerHandlerImp::onHandoff(
    Session& session,
    std::unique_ptr<stream_type>&& bundle,
    http_request_type&& request,
    boost::asio::ip::tcp::endpoint const& remote_address)
{
    using namespace boost::beast;
    auto const& p{session.port().protocol};
    bool const is_ws{
        p.count("ws") > 0 || p.count("ws2") > 0 || p.count("wss") > 0 ||
        p.count("wss2") > 0};

    if (websocket::is_upgrade(request))
    {
        if (!is_ws)
            return statusRequestResponse(request, http::status::unauthorized);

        std::shared_ptr<WSSession> ws;
        try
        {
            ws = session.websocketUpgrade();
        }
        catch (std::exception const& e)
        {
            JLOG(m_journal.error())
                << "Exception upgrading websocket: " << e.what() << "\n";
            return statusRequestResponse(
                request, http::status::internal_server_error);
        }

        auto is{std::make_shared<WSInfoSub>(m_networkOPs, ws)};
        auto const beast_remote_address =
            beast::IPAddressConversion::from_asio(remote_address);
        is->getConsumer() = requestInboundEndpoint(
            m_resourceManager,
            beast_remote_address,
            requestRole(
                Role::GUEST,
                session.port(),
                boost::json::object(),
                beast_remote_address,
                is->user()),
            is->user(),
            is->forwarded_for());
        ws->appDefined = std::move(is);
        ws->run();

        Handoff handoff;
        handoff.moved = true;
        return handoff;
    }

    if (bundle && p.count("peer") > 0)
        return app_.overlay().onHandoff(
            std::move(bundle), std::move(request), remote_address);

    if (is_ws && isStatusRequest(request))
        return statusResponse(request);

    // Otherwise pass to legacy onRequest or websocket
    return {};
}

static inline Json::Output
makeOutput(Session& session)
{
    return [&](boost::beast::string_view const& b) {
        session.write(b.data(), b.size());
    };
}

static std::map<std::string, std::string>
build_map(boost::beast::http::fields const& h)
{
    std::map<std::string, std::string> c;
    for (auto const& e : h)
    {
        auto key(e.name_string().to_string());
        std::transform(key.begin(), key.end(), key.begin(), [](auto kc) {
            return std::tolower(static_cast<unsigned char>(kc));
        });
        c[key] = e.value().to_string();
    }
    return c;
}

template <class ConstBufferSequence>
static std::string
buffers_to_string(ConstBufferSequence const& bs)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    std::string s;
    s.reserve(buffer_size(bs));
    // Use auto&& so the right thing happens whether bs returns a copy or
    // a reference
    for (auto&& b : bs)
        s.append(buffer_cast<char const*>(b), buffer_size(b));
    return s;
}

void
ServerHandlerImp::onRequest(Session& session)
{
    // Make sure RPC is enabled on the port
    if (session.port().protocol.count("http") == 0 &&
        session.port().protocol.count("https") == 0)
    {
        HTTPReply(403, "Forbidden", makeOutput(session), app_.journal("RPC"));
        session.close(true);
        return;
    }

    // Check user/password authorization
    if (!authorized(session.port(), build_map(session.request())))
    {
        HTTPReply(403, "Forbidden", makeOutput(session), app_.journal("RPC"));
        session.close(true);
        return;
    }

    std::shared_ptr<Session> detachedSession = session.detach();
    auto const postResult = m_jobQueue.postCoro(
        jtCLIENT_RPC,
        "RPC-Client",
        [this, detachedSession](std::shared_ptr<JobQueue::Coro> coro) {
            processSession(detachedSession, coro);
        });
    if (postResult == nullptr)
    {
        // The coroutine was rejected, probably because we're shutting down.
        HTTPReply(
            503,
            "Service Unavailable",
            makeOutput(*detachedSession),
            app_.journal("RPC"));
        detachedSession->close(true);
        return;
    }
}

void
ServerHandlerImp::onWSMessage(
    std::shared_ptr<WSSession> session,
    std::vector<boost::asio::const_buffer> const& buffers)
{
    boost::json::value jv;
    parse(jv, buffers);
    auto const size = boost::asio::buffer_size(buffers);
    if (size > RPC::Tuning::maxRequestSize ||
        !jv.is_null() || !jv.is_object())
    {
        boost::json::object jvResult;
        jvResult[jss::type.c_str()] = jss::error;
        jvResult[jss::error.c_str()] = "jsonInvalid";
        jvResult[jss::value.c_str()] = buffers_to_string(buffers);
        boost::beast::multi_buffer sb;
        std::string data = serialize(jvResult);
        unsigned int n = data.size();
        sb.commit(boost::asio::buffer_copy(sb.prepare(n), boost::asio::buffer(data, n)));
//        Json::stream(jvResult, [&sb](auto const p, auto const n) {
//            sb.commit(boost::asio::buffer_copy(
//                sb.prepare(n), boost::asio::buffer(p, n)));
//        });
        JLOG(m_journal.trace()) << "Websocket sending '" << jvResult << "'";
        session->send(
            std::make_shared<StreambufWSMsg<decltype(sb)>>(std::move(sb)));
        session->complete();
        return;
    }

    JLOG(m_journal.trace()) << "Websocket received '" << jv << "'";

    auto const postResult = m_jobQueue.postCoro(
        jtCLIENT_WEBSOCKET,
        "WS-Client",
        [this, session, jv = std::move(jv)](
            std::shared_ptr<JobQueue::Coro> const& coro) {
            auto const jr = this->processSession(session, coro, jv.as_object());
            auto const s = serialize(jr);
            auto const n = s.length();
            boost::beast::multi_buffer sb(n);
            sb.commit(boost::asio::buffer_copy(
                sb.prepare(n), boost::asio::buffer(s.c_str(), n)));
            session->send(
                std::make_shared<StreambufWSMsg<decltype(sb)>>(std::move(sb)));
            session->complete();
        });
    if (postResult == nullptr)
    {
        // The coroutine was rejected, probably because we're shutting down.
        session->close({boost::beast::websocket::going_away, "Shutting Down"});
    }
}

void
ServerHandlerImp::onClose(Session& session, boost::system::error_code const&)
{
    std::lock_guard lock(mutex_);
    --count_[session.port()];
}

void
ServerHandlerImp::onStopped(Server&)
{
    std::lock_guard lock(mutex_);
    stopped_ = true;
    condition_.notify_one();
}

//------------------------------------------------------------------------------

template <class T>
void
logDuration(
    boost::json::object const& request,
    T const& duration,
    beast::Journal& journal)
{
    using namespace std::chrono_literals;
    auto const level = (duration >= 10s)
        ? journal.error()
        : (duration >= 1s) ? journal.warn() : journal.debug();

    JLOG(level) << "RPC request processing duration = "
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       duration)
                       .count()
                << " microseconds. request = " << request;
}

boost::json::object
ServerHandlerImp::processSession(
    std::shared_ptr<WSSession> const& session,
    std::shared_ptr<JobQueue::Coro> const& coro,
    boost::json::object const& jv) // Keshava: temporarily removing const for ease of prototyping
{
    auto is = std::static_pointer_cast<WSInfoSub>(session->appDefined);
    if (is->getConsumer().disconnect(m_journal))
    {
        session->close(
            {boost::beast::websocket::policy_error, "threshold exceeded"});
        // FIX: This rpcError is not delivered since the session
        // was just closed.
        return rpcError(rpcSLOW_DOWN);
    }

    // Requests without "command" are invalid.
    boost::json::object jr;
    Resource::Charge loadType = Resource::feeReferenceRPC;
    try
    {
        auto apiVersion =
            RPC::getAPIVersionNumber(jv, app_.config().BETA_RPC_API);
        if (apiVersion == RPC::apiInvalidVersion ||
            (!jv.contains(jss::command.c_str()) && !jv.contains(jss::method.c_str())) ||
            (jv.contains(jss::command.c_str()) && !jv.at(jss::command.c_str()).is_string()) ||
            (jv.contains(jss::method.c_str()) && !jv.at(jss::method.c_str()).is_string()) ||
            (jv.contains(jss::command.c_str()) && jv.contains(jss::method.c_str()) &&
             jv.at(jss::command.c_str()).as_string() != jv.at(jss::method.c_str()).as_string()))
        {
            jr[jss::type.c_str()]= jss::response;
            jr[jss::status.c_str()] = jss::error;
            jr[jss::error.c_str()] = apiVersion == RPC::apiInvalidVersion
                ? jss::invalid_API_version
                : jss::missingCommand;
            jr[jss::request.c_str()] = jv;
            if (jv.contains(jss::id.c_str()))
                jr[jss::id.c_str()] = jv.at(jss::id.c_str());
            if (jv.contains(jss::jsonrpc.c_str()))
                jr[jss::jsonrpc.c_str()] = jv.at(jss::jsonrpc.c_str());
            if (jv.contains(jss::ripplerpc.c_str()))
                jr[jss::ripplerpc.c_str()] = jv.at(jss::ripplerpc.c_str());
            if (jv.contains(jss::api_version.c_str()))
                jr[jss::api_version.c_str()] = jv.at(jss::api_version.c_str());

            is->getConsumer().charge(Resource::feeInvalidRPC);
            return jr;
        }

        auto required = RPC::roleRequired(
            apiVersion,
            app_.config().BETA_RPC_API,
            jv.contains(jss::command.c_str()) ? jv.at(jss::command.c_str()).as_string().c_str()
                                      : jv.at(jss::method.c_str()).as_string().c_str());
        auto role = requestRole(
            required,
            session->port(),
            jv,
            beast::IP::from_asio(session->remote_endpoint().address()),
            is->user());
        if (Role::FORBID == role)
        {
            loadType = Resource::feeInvalidRPC;
            jr[jss::result.c_str()] = rpcError(rpcFORBIDDEN);
        }
        else
        {
            RPC::JsonContext context{
                {app_.journal("RPCHandler"),
                 app_,
                 loadType,
                 app_.getOPs(),
                 app_.getLedgerMaster(),
                 is->getConsumer(),
                 role,
                 coro,
                 is,
                 apiVersion},
                jv,
                {is->user(), is->forwarded_for()}};

            auto start = std::chrono::system_clock::now();
            RPC::doCommand(context, jr[jss::result.c_str()]);
            auto end = std::chrono::system_clock::now();
            logDuration(jv, end - start, m_journal);
        }
    }
    catch (std::exception const& ex)
    {
        jr[jss::result.c_str()] = RPC::make_error(rpcINTERNAL);
        JLOG(m_journal.error())
            << "Exception while processing WS: " << ex.what() << "\n"
            << "Input JSON: " << serialize(jv);
    }

    is->getConsumer().charge(loadType);
    if (is->getConsumer().warn())
        jr[jss::warning.c_str()] = jss::load;

    // Currently we will simply unwrap errors returned by the RPC
    // API, in the future maybe we can make the responses
    // consistent.
    //
    // Regularize result. This is duplicate code.
    if (jr[jss::result.c_str()].as_object().contains(jss::error.c_str()))
    {
        jr = jr[jss::result.c_str()].as_object();
        jr[jss::status.c_str()] = jss::error;

        auto rq = jv;

        if (rq.contains(jss::passphrase.c_str()))
            rq[jss::passphrase.c_str()] = "<masked>";
        if (rq.contains(jss::secret.c_str()))
            rq[jss::secret.c_str()] = "<masked>";
        if (rq.contains(jss::seed.c_str()))
            rq[jss::seed.c_str()] = "<masked>";
        if (rq.contains(jss::seed_hex.c_str()))
            rq[jss::seed_hex.c_str()] = "<masked>";


        jr[jss::request.c_str()] = rq;
    }
    else
    {
        if (jr[jss::result.c_str()].as_object().contains("forwarded"))
            jr = jr[jss::result.c_str()].as_object();
        jr[jss::status.c_str()] = jss::success;
    }

    if (jv.contains(jss::id.c_str()))
        jr[jss::id.c_str()] = jv.at(jss::id.c_str());
    if (jv.contains(jss::jsonrpc.c_str()))
        jr[jss::jsonrpc.c_str()] = jv.at(jss::jsonrpc.c_str());
    if (jv.contains(jss::ripplerpc.c_str()))
        jr[jss::ripplerpc.c_str()] = jv.at(jss::ripplerpc.c_str());
    if (jv.contains(jss::api_version.c_str()))
        jr[jss::api_version.c_str()] = jv.at(jss::api_version.c_str());

    jr[jss::type.c_str()] = jss::response;
    return jr;
}

// Run as a coroutine.
void
ServerHandlerImp::processSession(
    std::shared_ptr<Session> const& session,
    std::shared_ptr<JobQueue::Coro> coro)
{
    processRequest(
        session->port(),
        buffers_to_string(session->request().body().data()),
        session->remoteAddress().at_port(0),
        makeOutput(*session),
        coro,
        forwardedFor(session->request()),
        [&] {
            auto const iter = session->request().find("X-User");
            if (iter != session->request().end())
                return iter->value();
            return boost::beast::string_view{};
        }());

    if (beast::rfc2616::is_keep_alive(session->request()))
        session->complete();
    else
        session->close(true);
}

static boost::json::object
make_json_error(Json::Int code, const char* message)
{
    boost::json::object sub;
    sub["code"] = code;
    sub["message"] = std::move(message);
    boost::json::object r;
    r["error"] = sub;
    return r;
}

Json::Int constexpr method_not_found = -32601;
Json::Int constexpr server_overloaded = -32604;
Json::Int constexpr forbidden = -32605;
Json::Int constexpr wrong_version = -32606;

void
ServerHandlerImp::processRequest(
    Port const& port,
    std::string const& request,
    beast::IP::Endpoint const& remoteIPAddress,
    Output&& output,
    std::shared_ptr<JobQueue::Coro> coro,
    boost::string_view forwardedFor,
    boost::string_view user)
{
    auto rpcJ = app_.journal("RPC");

    boost::json::value jsonOrig(boost::json::parse(request));
    {
        if ((request.size() > RPC::Tuning::maxRequestSize) ||
            !jsonOrig.is_null() ||
            !jsonOrig.is_object())
        {
            HTTPReply(
                400,
                "Unable to parse request: ", // Keshava: TODO: need to format the error msgs from boost::json::parse
                output,
                rpcJ);
            return;
        }
    }

    bool batch = false;
    unsigned size = 1;
    if (jsonOrig.as_object().contains(jss::method.c_str()) && jsonOrig.as_object()[jss::method.c_str()] == "batch")
    {
        batch = true;
        if (!jsonOrig.as_object().contains(jss::params.c_str()) || !jsonOrig.as_object()[jss::params.c_str()].is_array())
        {
            HTTPReply(400, "Malformed batch request", output, rpcJ);
            return;
        }
        size = jsonOrig.as_object()[jss::params.c_str()].as_array().size();
    }

    boost::json::value reply;
    auto const start(std::chrono::high_resolution_clock::now());
    for (unsigned i = 0; i < size; ++i)
    {
        boost::json::value & jsonRPC =
            batch ? jsonOrig.as_object()[jss::params.c_str()].as_array()[i] : jsonOrig;

        if (!jsonRPC.is_object())
        {
            boost::json::object r;
            r[jss::request.c_str()] = jsonRPC;
            r[jss::error.c_str()] =
                make_json_error(method_not_found, "Method not found");
            reply.as_array().emplace_back(r);
            continue;
        }

        auto apiVersion = RPC::apiVersionIfUnspecified;
        if (jsonRPC.as_object().contains(jss::params.c_str()) && jsonRPC.as_object()[jss::params.c_str()].is_array() &&
            jsonRPC.as_object()[jss::params.c_str()].as_array().size() > 0 &&
            jsonRPC.as_object()[jss::params.c_str()].as_array()[0u].is_object())
        {
            apiVersion = RPC::getAPIVersionNumber(
                jsonRPC.as_object()[jss::params.c_str()].as_array()[0u],
                app_.config().BETA_RPC_API);
        }

        if (apiVersion == RPC::apiVersionIfUnspecified && batch)
        {
            // for batch request, api_version may be at a different level
            apiVersion =
                RPC::getAPIVersionNumber(jsonRPC, app_.config().BETA_RPC_API);
        }

        if (apiVersion == RPC::apiInvalidVersion)
        {
            if (!batch)
            {
                HTTPReply(400, jss::invalid_API_version.c_str(), output, rpcJ);
                return;
            }
            boost::json::object r;
            r[jss::request.c_str()] = jsonRPC;
            r[jss::error.c_str()] = make_json_error(
                wrong_version, jss::invalid_API_version.c_str());
            reply.as_array().emplace_back(r);
            continue;
        }

        /* ------------------------------------------------------------------ */
        auto role = Role::FORBID;
        auto required = Role::FORBID;
        if (jsonRPC.as_object().contains(jss::method.c_str()) && jsonRPC.as_object()[jss::method.c_str()].is_string())
            required = RPC::roleRequired(
                apiVersion,
                app_.config().BETA_RPC_API,
                jsonRPC.as_object()[jss::method.c_str()].as_string().c_str());

        if (jsonRPC.as_object().contains(jss::params.c_str()) && jsonRPC.as_object()[jss::params.c_str()].is_array() &&
            jsonRPC.as_object()[jss::params.c_str()].as_array().size() > 0 &&
            (jsonRPC.as_object()[jss::params.c_str()].as_array()[0u].is_object() || jsonRPC.as_object()[jss::params.c_str()].as_array()[0u].is_null()))
        {
            role = requestRole(
                required,
                port,
                jsonRPC.as_object()[jss::params.c_str()].as_array()[0u].as_object(),
                remoteIPAddress,
                user);
        }
        else
        {
            role = requestRole(
                required, port, boost::json::object(), remoteIPAddress, user);
        }

        Resource::Consumer usage;
        if (isUnlimited(role))
        {
            usage = m_resourceManager.newUnlimitedEndpoint(remoteIPAddress);
        }
        else
        {
            usage = m_resourceManager.newInboundEndpoint(
                remoteIPAddress, role == Role::PROXY, forwardedFor);
            if (usage.disconnect(m_journal))
            {
                if (!batch)
                {
                    HTTPReply(503, "Server is overloaded", output, rpcJ);
                    return;
                }
                boost::json::object r = jsonRPC.as_object();
                r[jss::error.c_str()] =
                    make_json_error(server_overloaded, "Server is overloaded");
                reply.as_array().emplace_back(r);
                continue;
            }
        }

        if (role == Role::FORBID)
        {
            usage.charge(Resource::feeInvalidRPC);
            if (!batch)
            {
                HTTPReply(403, "Forbidden", output, rpcJ);
                return;
            }
            boost::json::object r = jsonRPC.as_object();
            r[jss::error.c_str()] = make_json_error(forbidden, "Forbidden");
            reply.as_array().emplace_back(r);
            continue;
        }

        if (!jsonRPC.as_object().contains(jss::method.c_str()) || jsonRPC.as_object()[jss::method.c_str()].is_null())
        {
            usage.charge(Resource::feeInvalidRPC);
            if (!batch)
            {
                HTTPReply(400, "Null method", output, rpcJ);
                return;
            }
            boost::json::object r = jsonRPC.as_object();
            r[jss::error.c_str()] = make_json_error(method_not_found, "Null method");
            reply.as_array().emplace_back(r);
            continue;
        }

        boost::json::value const& method = jsonRPC.as_object()[jss::method.c_str()];
        if (!method.is_string())
        {
            usage.charge(Resource::feeInvalidRPC);
            if (!batch)
            {
                HTTPReply(400, "method is not string", output, rpcJ);
                return;
            }
            boost::json::object r = jsonRPC.as_object();
            r[jss::error.c_str()] =
                make_json_error(method_not_found, "method is not string");
            reply.as_array().emplace_back(r);
            continue;
        }

        std::string strMethod{method.as_string()};
        if (strMethod.empty())
        {
            usage.charge(Resource::feeInvalidRPC);
            if (!batch)
            {
                HTTPReply(400, "method is empty", output, rpcJ);
                return;
            }
            boost::json::object r = jsonRPC.as_object();
            r[jss::error.c_str()] =
                make_json_error(method_not_found, "method is empty");
            reply.as_array().emplace_back(r);
            continue;
        }

        // Extract request parameters from the request Json as `params`.
        //
        // If the field "params" is empty, `params` is an empty object.
        //
        // Otherwise, that field must be an array of length 1 (why?)
        // and we take that first entry and validate that it's an object.
        boost::json::value params;
        if (!batch)
        {
            params = jsonRPC.as_object()[jss::params.c_str()];
            if (params.is_null())
                params.emplace_object();
            else if (!params.is_array() || params.as_array().size() != 1)
            {
                usage.charge(Resource::feeInvalidRPC);
                HTTPReply(400, "params unparseable", output, rpcJ);
                return;
            }
            else
            {
                params = std::move(params.as_array()[0u]);
                if (!(params.is_object() || params.is_null()))
                {
                    usage.charge(Resource::feeInvalidRPC);
                    HTTPReply(400, "params unparseable", output, rpcJ);
                    return;
                }
            }
        }
        else  // batch
        {
            params = jsonRPC;
        }

        std::string ripplerpc = "1.0";
        if (params.as_object().contains(jss::ripplerpc.c_str()))
        {
            if (!params.as_object()[jss::ripplerpc.c_str()].is_string())
            {
                usage.charge(Resource::feeInvalidRPC);
                if (!batch)
                {
                    HTTPReply(400, "ripplerpc is not a string", output, rpcJ);
                    return;
                }

                boost::json::object r = jsonRPC.as_object();
                r[jss::error.c_str()] = make_json_error(
                    method_not_found, "ripplerpc is not a string");
                reply.as_array().emplace_back(r);
                continue;
            }
            ripplerpc = params.as_object()[jss::ripplerpc.c_str()].as_string();
        }

        /**
         * Clear header-assigned values if not positively identified from a
         * secure_gateway.
         */
        if (role != Role::IDENTIFIED && role != Role::PROXY)
        {
            forwardedFor.clear();
            user.clear();
        }

        JLOG(m_journal.debug()) << "Query: " << strMethod << params;

        // Provide the JSON-RPC method as the field "command" in the request.
        params.as_object()[jss::command.c_str()] = strMethod;
        JLOG(m_journal.trace())
            << "doRpcCommand:" << strMethod << ":" << params;

        Resource::Charge loadType = Resource::feeReferenceRPC;

        RPC::JsonContext context{
            {m_journal,
             app_,
             loadType,
             m_networkOPs,
             app_.getLedgerMaster(),
             usage,
             role,
             coro,
             InfoSub::pointer(),
             apiVersion},
            params.as_object(),
            {user, forwardedFor}};
        boost::json::value result;

        auto start = std::chrono::system_clock::now();

        try
        {
            RPC::doCommand(context, result);
        }
        catch (std::exception const& ex)
        {
            result = RPC::make_error(rpcINTERNAL);
            JLOG(m_journal.error()) << "Internal error : " << ex.what()
                                    << " when processing request: "
                                    << serialize(params);
        }

        auto end = std::chrono::system_clock::now();

        logDuration(params.as_object(), end - start, m_journal);

        usage.charge(loadType);
        if (usage.warn())
            result.as_object()[jss::warning.c_str()] = jss::load;

        boost::json::object r;
        if (ripplerpc >= "2.0")
        {
            if (result.as_object().contains(jss::error.c_str()))
            {
                result.as_object()[jss::status.c_str()] = jss::error;
                result.as_object()["code"] = result.as_object()[jss::error_code.c_str()];
                result.as_object()["message"] = result.as_object()[jss::error_message.c_str()];
                result.as_object().erase(jss::error_message.c_str());
                JLOG(m_journal.debug()) << "rpcError: " << result.as_object()[jss::error.c_str()]
                                        << ": " << result.as_object()[jss::error_message.c_str()];
                r[jss::error.c_str()] = std::move(result);
            }
            else
            {
                result.as_object()[jss::status.c_str()] = jss::success;
                r[jss::result.c_str()] = std::move(result);
            }
        }
        else
        {
            // Always report "status".  On an error report the request as
            // received.
            if (result.as_object().contains(jss::error.c_str()))
            {
                auto rq = params;

                if (rq.is_object())
                {  // But mask potentially sensitive information.
                    if (rq.as_object().contains(jss::passphrase.c_str()))
                        rq.as_object()[jss::passphrase.c_str()] = "<masked>";
                    if (rq.as_object().contains(jss::secret.c_str()))
                        rq.as_object()[jss::secret.c_str()] = "<masked>";
                    if (rq.as_object().contains(jss::seed.c_str()))
                        rq.as_object()[jss::seed.c_str()] = "<masked>";
                    if (rq.as_object().contains(jss::seed_hex.c_str()))
                        rq.as_object()[jss::seed_hex.c_str()] = "<masked>";
                }

                result.as_object()[jss::status.c_str()] = jss::error;
                result.as_object()[jss::request.c_str()] = rq;

                JLOG(m_journal.debug()) << "rpcError: " << result.as_object()[jss::error.c_str()]
                                        << ": " << result.as_object()[jss::error_message.c_str()];
            }
            else
            {
                result.as_object()[jss::status.c_str()] = jss::success;
            }
            r[jss::result.c_str()] = std::move(result);
        }

        if (params.as_object().contains(jss::jsonrpc.c_str()))
            r[jss::jsonrpc.c_str()] = params.as_object()[jss::jsonrpc.c_str()];
        if (params.as_object().contains(jss::ripplerpc.c_str()))
            r[jss::ripplerpc.c_str()] = params.as_object()[jss::ripplerpc.c_str()];
        if (params.as_object().contains(jss::id.c_str()))
            r[jss::id.c_str()] = params.as_object()[jss::id.c_str()];
        if (batch)
            reply.as_array().emplace_back(std::move(r));
        else
            reply = std::move(r);

        if (reply.as_object().contains(jss::result.c_str()) &&
            reply.as_object()[jss::result.c_str()].as_object().contains(jss::result.c_str()))
        {
            reply = reply.as_object()[jss::result.c_str()];
            if (reply.as_object().contains(jss::status.c_str()))
            {
                reply.as_object()[jss::result.c_str()].as_object()[jss::status.c_str()] = reply.as_object()[jss::status.c_str()];
                reply.as_object().erase(jss::status.c_str());
            }
        }
    }

    // If we're returning an error_code, use that to determine the HTTP status.
    int const httpStatus = [&reply]() {
        // This feature is enabled with ripplerpc version 3.0 and above.
        // Before ripplerpc version 3.0 always return 200.
        if (reply.as_object().contains(jss::ripplerpc.c_str()) &&
            reply.as_object()[jss::ripplerpc.c_str()].is_string() &&
            reply.as_object()[jss::ripplerpc.c_str()].as_string() >= "3.0")
        {
            // If there's an error_code, use that to determine the HTTP Status.
            if (reply.as_object().contains(jss::error.c_str()) &&
                reply.as_object()[jss::error.c_str()].as_object().contains(jss::error_code.c_str()) &&
                reply.as_object()[jss::error.c_str()].as_object()[jss::error_code.c_str()].is_int64())
            {
                int const errCode = reply.as_object()[jss::error.c_str()].as_object()[jss::error_code.c_str()].as_int64();
                return RPC::error_code_http_status(
                    static_cast<error_code_i>(errCode));
            }
        }
        // Return OK.
        return 200;
    }();

    auto response = serialize(reply);

    rpc_time_.notify(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start));
    ++rpc_requests_;
    rpc_size_.notify(beast::insight::Event::value_type{response.size()});

    response += '\n';

    if (auto stream = m_journal.debug())
    {
        static const int maxSize = 10000;
        if (response.size() <= maxSize)
            stream << "Reply: " << response;
        else
            stream << "Reply: " << response.substr(0, maxSize);
    }

    HTTPReply(httpStatus, response, output, rpcJ);
}

//------------------------------------------------------------------------------

/*  This response is used with load balancing.
    If the server is overloaded, status 500 is reported. Otherwise status 200
    is reported, meaning the server can accept more connections.
*/
Handoff
ServerHandlerImp::statusResponse(http_request_type const& request) const
{
    using namespace boost::beast::http;
    Handoff handoff;
    response<string_body> msg;
    std::string reason;
    if (app_.serverOkay(reason))
    {
        msg.result(boost::beast::http::status::ok);
        msg.body() = "<!DOCTYPE html><html><head><title>" + systemName() +
            " Test page for rippled</title></head><body><h1>" + systemName() +
            " Test</h1><p>This page shows rippled http(s) "
            "connectivity is working.</p></body></html>";
    }
    else
    {
        msg.result(boost::beast::http::status::internal_server_error);
        msg.body() = "<HTML><BODY>Server cannot accept clients: " + reason +
            "</BODY></HTML>";
    }
    msg.version(request.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "text/html");
    msg.insert("Connection", "close");
    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return handoff;
}

//------------------------------------------------------------------------------

void
ServerHandler::Setup::makeContexts()
{
    for (auto& p : ports)
    {
        if (p.secure())
        {
            if (p.ssl_key.empty() && p.ssl_cert.empty() && p.ssl_chain.empty())
                p.context = make_SSLContext(p.ssl_ciphers);
            else
                p.context = make_SSLContextAuthed(
                    p.ssl_key, p.ssl_cert, p.ssl_chain, p.ssl_ciphers);
        }
        else
        {
            p.context = std::make_shared<boost::asio::ssl::context>(
                boost::asio::ssl::context::sslv23);
        }
    }
}

static Port
to_Port(ParsedPort const& parsed, std::ostream& log)
{
    Port p;
    p.name = parsed.name;

    if (!parsed.ip)
    {
        log << "Missing 'ip' in [" << p.name << "]";
        Throw<std::exception>();
    }
    p.ip = *parsed.ip;

    if (!parsed.port)
    {
        log << "Missing 'port' in [" << p.name << "]";
        Throw<std::exception>();
    }
    else if (*parsed.port == 0)
    {
        log << "Port " << *parsed.port << "in [" << p.name << "] is invalid";
        Throw<std::exception>();
    }
    p.port = *parsed.port;

    if (parsed.protocol.empty())
    {
        log << "Missing 'protocol' in [" << p.name << "]";
        Throw<std::exception>();
    }
    p.protocol = parsed.protocol;

    p.user = parsed.user;
    p.password = parsed.password;
    p.admin_user = parsed.admin_user;
    p.admin_password = parsed.admin_password;
    p.ssl_key = parsed.ssl_key;
    p.ssl_cert = parsed.ssl_cert;
    p.ssl_chain = parsed.ssl_chain;
    p.ssl_ciphers = parsed.ssl_ciphers;
    p.pmd_options = parsed.pmd_options;
    p.ws_queue_limit = parsed.ws_queue_limit;
    p.limit = parsed.limit;
    p.admin_nets_v4 = parsed.admin_nets_v4;
    p.admin_nets_v6 = parsed.admin_nets_v6;
    p.secure_gateway_nets_v4 = parsed.secure_gateway_nets_v4;
    p.secure_gateway_nets_v6 = parsed.secure_gateway_nets_v6;

    return p;
}

static std::vector<Port>
parse_Ports(Config const& config, std::ostream& log)
{
    std::vector<Port> result;

    if (!config.exists("server"))
    {
        log << "Required section [server] is missing";
        Throw<std::exception>();
    }

    ParsedPort common;
    parse_Port(common, config["server"], log);

    auto const& names = config.section("server").values();
    result.reserve(names.size());
    for (auto const& name : names)
    {
        if (!config.exists(name))
        {
            log << "Missing section: [" << name << "]";
            Throw<std::exception>();
        }
        ParsedPort parsed = common;
        parsed.name = name;
        parse_Port(parsed, config[name], log);
        result.push_back(to_Port(parsed, log));
    }

    if (config.standalone())
    {
        auto it = result.begin();

        while (it != result.end())
        {
            auto& p = it->protocol;

            // Remove the peer protocol, and if that would
            // leave the port empty, remove the port as well
            if (p.erase("peer") && p.empty())
                it = result.erase(it);
            else
                ++it;
        }
    }
    else
    {
        auto const count =
            std::count_if(result.cbegin(), result.cend(), [](Port const& p) {
                return p.protocol.count("peer") != 0;
            });

        if (count > 1)
        {
            log << "Error: More than one peer protocol configured in [server]";
            Throw<std::exception>();
        }

        if (count == 0)
            log << "Warning: No peer protocol configured";
    }

    return result;
}

// Fill out the client portion of the Setup
static void
setup_Client(ServerHandler::Setup& setup)
{
    decltype(setup.ports)::const_iterator iter;
    for (iter = setup.ports.cbegin(); iter != setup.ports.cend(); ++iter)
        if (iter->protocol.count("http") > 0 ||
            iter->protocol.count("https") > 0)
            break;
    if (iter == setup.ports.cend())
        return;
    setup.client.secure = iter->protocol.count("https") > 0;
    setup.client.ip = beast::IP::is_unspecified(iter->ip)
        ?
        // VFALCO HACK! to make localhost work
        (iter->ip.is_v6() ? "::1" : "127.0.0.1")
        : iter->ip.to_string();
    setup.client.port = iter->port;
    setup.client.user = iter->user;
    setup.client.password = iter->password;
    setup.client.admin_user = iter->admin_user;
    setup.client.admin_password = iter->admin_password;
}

// Fill out the overlay portion of the Setup
static void
setup_Overlay(ServerHandler::Setup& setup)
{
    auto const iter = std::find_if(
        setup.ports.cbegin(), setup.ports.cend(), [](Port const& port) {
            return port.protocol.count("peer") != 0;
        });
    if (iter == setup.ports.cend())
    {
        setup.overlay.port = 0;
        return;
    }
    setup.overlay.ip = iter->ip;
    setup.overlay.port = iter->port;
}

ServerHandler::Setup
setup_ServerHandler(Config const& config, std::ostream&& log)
{
    ServerHandler::Setup setup;
    setup.ports = parse_Ports(config, log);

    setup_Client(setup);
    setup_Overlay(setup);

    return setup;
}

std::unique_ptr<ServerHandler>
make_ServerHandler(
    Application& app,
    boost::asio::io_service& io_service,
    JobQueue& jobQueue,
    NetworkOPs& networkOPs,
    Resource::Manager& resourceManager,
    CollectorManager& cm)
{
    return std::make_unique<ServerHandlerImp>(
        app, io_service, jobQueue, networkOPs, resourceManager, cm);
}

}  // namespace ripple
