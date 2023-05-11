//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/jss.h>
#include <ripple/server/Port.h>
#include <boost/asio.hpp>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <string>
#include <test/jtx/JSONRPCClient.h>
#include <boost/json.hpp>
#include <boost/json/parse.hpp>

namespace ripple {
namespace test {

class JSONRPCClient : public AbstractClient
{
    static boost::asio::ip::tcp::endpoint
    getEndpoint(BasicConfig const& cfg)
    {
        auto& log = std::cerr;
        ParsedPort common;
        parse_Port(common, cfg["server"], log);
        for (auto const& name : cfg.section("server").values())
        {
            if (!cfg.exists(name))
                continue;
            ParsedPort pp;
            parse_Port(pp, cfg[name], log);
            if (pp.protocol.count("http") == 0)
                continue;
            using namespace boost::asio::ip;
            if (pp.ip && pp.ip->is_unspecified())
                *pp.ip = pp.ip->is_v6() ? address{address_v6::loopback()}
                                        : address{address_v4::loopback()};
            return {*pp.ip, *pp.port};
        }
        Throw<std::runtime_error>("Missing HTTP port");
        return {};  // Silence compiler control paths return value warning
    }

    template <class ConstBufferSequence>
    static std::string
    buffer_string(ConstBufferSequence const& b)
    {
        using namespace boost::asio;
        std::string s;
        s.resize(buffer_size(b));
        buffer_copy(buffer(&s[0], s.size()), b);
        return s;
    }

    boost::asio::ip::tcp::endpoint ep_;
    boost::asio::io_service ios_;
    boost::asio::ip::tcp::socket stream_;
    boost::beast::multi_buffer bin_;
    boost::beast::multi_buffer bout_;
    unsigned rpc_version_;

public:
    explicit JSONRPCClient(Config const& cfg, unsigned rpc_version)
        : ep_(getEndpoint(cfg)), stream_(ios_), rpc_version_(rpc_version)
    {
        stream_.connect(ep_);
    }

    ~JSONRPCClient() override
    {
        // stream_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        // stream_.close();
    }

    /*
        Return value is an Object type with up to three keys:
            status
            error
            result
    */
    boost::json::value
    invoke(std::string const& cmd, boost::json::value const& params) override
    {
        using namespace boost::beast::http;
        using namespace boost::asio;
        using namespace std::string_literals;

        request<string_body> req;
        req.method(boost::beast::http::verb::post);
        req.target("/");
        req.version(11);
        req.insert("Content-Type", "application/json; charset=UTF-8");
        {
            std::ostringstream ostr;
            ostr << ep_;
            req.insert("Host", ostr.str());
        }
        {
            boost::json::object jr;
            jr[jss::method.c_str()] = cmd;
            if (rpc_version_ == 2)
            {
                jr[jss::jsonrpc.c_str()] = "2.0";
                jr[jss::ripplerpc.c_str()] = "2.0";
                jr[jss::id.c_str()] = 5;
            }
            if (!params.is_null())
            {
                boost::json::array& ja = jr[jss::params.c_str()].emplace_array();
                ja.emplace_back(params);
            }
            req.body() = serialize(jr);
        }
        req.prepare_payload();
        write(stream_, req);

        response<dynamic_body> res;
        read(stream_, bin_, res);

        boost::json::value jv = boost::json::parse(buffer_string(res.body().data()));

        // TODO: include an assert condition here
//        ASSERT(jv.if_object());
        if (jv.as_object()["result"].if_object()->contains("error"))
            jv.as_object()["error"] = jv.as_object()["result"].as_object()["error"];
        if (jv.as_object()["result"].if_object()->contains("status"))
            jv.as_object()["status"] = jv.as_object()["result"].as_object()["status"];
        return jv;
    }

    unsigned
    version() const override
    {
        return rpc_version_;
    }
};

std::unique_ptr<AbstractClient>
makeJSONRPCClient(Config const& cfg, unsigned rpc_version)
{
    return std::make_unique<JSONRPCClient>(cfg, rpc_version);
}

}  // namespace test
}  // namespace ripple
