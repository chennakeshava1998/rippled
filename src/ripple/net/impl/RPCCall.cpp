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
#include <ripple/basics/ByteUtilities.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/contract.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/core/Config.h>
#include <ripple/json/Object.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/to_string.h>
#include <ripple/net/HTTPClient.h>
#include <ripple/net/RPCCall.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/ServerHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/regex.hpp>

#include <array>
#include <iostream>
#include <type_traits>
#include <unordered_map>

namespace ripple {

class RPCParser;

//
// HTTP protocol
//
// This ain't Apache.  We're just using HTTP header for the length field
// and to be compatible with other JSON-RPC implementations.
//

std::string
createHTTPPost(
    std::string const& strHost,
    std::string const& strPath,
    std::string const& strMsg,
    std::unordered_map<std::string, std::string> const& mapRequestHeaders)
{
    std::ostringstream s;

    // CHECKME this uses a different version than the replies below use. Is
    //         this by design or an accident or should it be using
    //         BuildInfo::getFullVersionString () as well?

    s << "POST " << (strPath.empty() ? "/" : strPath) << " HTTP/1.0\r\n"
      << "User-Agent: " << systemName() << "-json-rpc/v1\r\n"
      << "Host: " << strHost << "\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << strMsg.size() << "\r\n"
      << "Accept: application/json\r\n";

    for (auto const& [k, v] : mapRequestHeaders)
        s << k << ": " << v << "\r\n";

    s << "\r\n" << strMsg;

    return s.str();
}

class RPCParser
{
private:
    beast::Journal const j_;

    // TODO New routine for parsing ledger parameters, other routines should
    // standardize on this.
    static bool
    jvParseLedger(boost::json::object& jvRequest, std::string const& strLedger)
    {
        if (strLedger == "current" || strLedger == "closed" ||
            strLedger == "validated")
        {
            jvRequest[jss::ledger_index.c_str()] = strLedger;
        }
        else if (strLedger.length() == 64)
        {
            // YYY Could confirm this is a uint256.
            jvRequest[jss::ledger_hash.c_str()] = strLedger;
        }
        else
        {
            jvRequest[jss::ledger_index.c_str()] =
                beast::lexicalCast<std::uint32_t>(strLedger);
        }

        return true;
    }

    // Build a object { "currency" : "XYZ", "issuer" : "rXYX" }
    static boost::json::value
    jvParseCurrencyIssuer(std::string const& strCurrencyIssuer)
    {
        static boost::regex reCurIss("\\`([[:alpha:]]{3})(?:/(.+))?\\'");

        boost::smatch smMatch;

        if (boost::regex_match(strCurrencyIssuer, smMatch, reCurIss))
        {
            boost::json::object jvResult;
            std::string strCurrency = smMatch[1];
            std::string strIssuer = smMatch[2];

            jvResult[jss::currency.c_str()] = strCurrency;

            if (strIssuer.length())
            {
                // Could confirm issuer is a valid Ripple address.
                jvResult[jss::issuer.c_str()] = strIssuer;
            }

            return jvResult;
        }
        else
        {
            return RPC::make_param_error(
                std::string("Invalid currency/issuer '") + strCurrencyIssuer +
                "'");
        }
    }

    static bool
    validPublicKey(
        std::string const& strPk,
        TokenType type = TokenType::AccountPublic)
    {
        if (parseBase58<PublicKey>(type, strPk))
            return true;

        auto pkHex = strUnHex(strPk);
        if (!pkHex)
            return false;

        if (!publicKeyType(makeSlice(*pkHex)))
            return false;

        return true;
    }

private:
    using parseFuncPtr =
        boost::json::value (RPCParser::*)(boost::json::array const& jvParams);

    boost::json::value
    parseAsIs(boost::json::array const& jvParams)
    {
        boost::json::object v;

        if (jvParams.size() > 0)
            v[jss::params.c_str()] = jvParams;

        return v;
    }

    boost::json::value
    parseDownloadShard(boost::json::array const& jvParams)
    {
        boost::json::object jvResult;
        unsigned int sz{static_cast<unsigned int>(jvParams.size())};
        unsigned int i{0};

        // If odd number of params then 'novalidate' may have been specified
        if (sz & 1)
        {
            if (boost::iequals(jvParams[0u].as_string(), "novalidate"))
                ++i;
            else if (!boost::iequals(jvParams[--sz].as_string(), "novalidate"))
                return rpcError(rpcINVALID_PARAMS);
        }

        // Create the 'shards' array
        boost::json::array shards;
        for (; i < sz; i += 2)
        {
            boost::json::object shard;
            shard[jss::index.c_str()] = jvParams[i].as_uint64();
            shard[jss::url.c_str()] = jvParams[i + 1].as_string();
            shards.emplace_back(std::move(shard));
        }
        jvResult[jss::shards.c_str()] = std::move(shards);

        return jvResult;
    }

    boost::json::value
    parseInternal(boost::json::array const& jvParams)
    {
        boost::json::object v;
        v[jss::internal_command.c_str()] = jvParams[0u];

        boost::json::array params;

        for (unsigned i = 1; i < jvParams.size(); ++i)
            params.emplace_back(jvParams[i]);

        v[jss::params.c_str()] = params;

        return v;
    }

    boost::json::value
    parseManifest(boost::json::array const& jvParams)
    {
        if (jvParams.size() == 1)
        {
            boost::json::object jvRequest;

            std::string const strPk = std::string{jvParams[0u].as_string()};
            if (!validPublicKey(strPk, TokenType::NodePublic))
                return rpcError(rpcPUBLIC_MALFORMED);

            jvRequest[jss::public_key.c_str()] = strPk;

            return jvRequest;
        }

        return rpcError(rpcINVALID_PARAMS);
    }

    // fetch_info [clear]
    boost::json::value
    parseFetchInfo(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;
        unsigned int iParams = jvParams.size();

        if (iParams != 0)
            jvRequest[jvParams[0u].as_string()] = true;

        return jvRequest;
    }

    // account_tx accountID [ledger_min [ledger_max [limit [offset]]]] [binary]
    // [count] [descending]
    boost::json::value
    parseAccountTransactions(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;
        unsigned int iParams = jvParams.size();

        auto const account = parseBase58<AccountID>(std::string{jvParams[0u].as_string()});
        if (!account)
            return rpcError(rpcACT_MALFORMED);

        jvRequest[jss::account.c_str()] = toBase58(*account);

        bool bDone = false;

        while (!bDone && iParams >= 2)
        {
            // VFALCO Why is Json::StaticString appearing on the right side?
            if (jvParams[iParams - 1].as_string() == jss::binary.c_str())
            {
                jvRequest[jss::binary.c_str()] = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].as_string() == jss::count.c_str())
            {
                jvRequest[jss::count.c_str()] = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].as_string() == jss::descending.c_str())
            {
                jvRequest[jss::descending.c_str()] = true;
                --iParams;
            }
            else
            {
                bDone = true;
            }
        }

        if (1 == iParams)
        {
        }
        else if (2 == iParams)
        {
            if (!jvParseLedger(jvRequest, std::string{jvParams[1u].as_string()}))
                return jvRequest;
        }
        else
        {
            std::int64_t uLedgerMin = jvParams[1u].as_int64();
            std::int64_t uLedgerMax = jvParams[2u].as_int64();

            if (uLedgerMax != -1 && uLedgerMax < uLedgerMin)
            {
                // The command line always follows apiMaximumSupportedVersion
                if (RPC::apiMaximumSupportedVersion == 1)
                    return rpcError(rpcLGR_IDXS_INVALID);
                return rpcError(rpcNOT_SYNCED);
            }

            jvRequest[jss::ledger_index_min.c_str()] = jvParams[1u].as_int64();
            jvRequest[jss::ledger_index_max.c_str()] = jvParams[2u].as_int64();

            if (iParams >= 4)
                jvRequest[jss::limit.c_str()] = jvParams[3u].as_int64();

            if (iParams >= 5)
                jvRequest[jss::offset.c_str()] = jvParams[4u].as_int64();
        }

        return jvRequest;
    }

    // tx_account accountID [ledger_min [ledger_max [limit]]]] [binary] [count]
    // [forward]
    boost::json::value
    parseTxAccount(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;
        unsigned int iParams = jvParams.size();

        auto const account = parseBase58<AccountID>(std::string{jvParams[0u].as_string()});
        if (!account)
            return rpcError(rpcACT_MALFORMED);

        jvRequest[jss::account.c_str()] = toBase58(*account);

        bool bDone = false;

        while (!bDone && iParams >= 2)
        {
            if (jvParams[iParams - 1].as_string() == jss::binary.c_str())
            {
                jvRequest[jss::binary.c_str()] = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].as_string() == jss::count.c_str())
            {
                jvRequest[jss::count.c_str()] = true;
                --iParams;
            }
            else if (jvParams[iParams - 1].as_string() == jss::forward.c_str())
            {
                jvRequest[jss::forward.c_str()] = true;
                --iParams;
            }
            else
            {
                bDone = true;
            }
        }

        if (1 == iParams)
        {
        }
        else if (2 == iParams)
        {
            if (!jvParseLedger(jvRequest, std::string{jvParams[1u].as_string()}))
                return jvRequest;
        }
        else
        {
            std::int64_t uLedgerMin = jvParams[1u].as_int64();
            std::int64_t uLedgerMax = jvParams[2u].as_int64();

            if (uLedgerMax != -1 && uLedgerMax < uLedgerMin)
            {
                // The command line always follows apiMaximumSupportedVersion
                if (RPC::apiMaximumSupportedVersion == 1)
                    return rpcError(rpcLGR_IDXS_INVALID);
                return rpcError(rpcNOT_SYNCED);
            }

            jvRequest[jss::ledger_index_min.c_str()] = jvParams[1u].as_int64();
            jvRequest[jss::ledger_index_max.c_str()] = jvParams[2u].as_int64();

            if (iParams >= 4)
                jvRequest[jss::limit.c_str()] = jvParams[3u].as_int64();
        }

        return jvRequest;
    }

    // book_offers <taker_pays> <taker_gets> [<taker> [<ledger> [<limit>
    // [<proof> [<marker>]]]]] limit: 0 = no limit proof: 0 or 1
    //
    // Mnemonic: taker pays --> offer --> taker gets
    boost::json::value
    parseBookOffers(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        boost::json::value jvTakerPays =
            jvParseCurrencyIssuer(std::string{jvParams[0u].as_string()});
        boost::json::value jvTakerGets =
            jvParseCurrencyIssuer(std::string{jvParams[1u].as_string()});

        if (isRpcError(jvTakerPays))
        {
            return jvTakerPays;
        }
        else
        {
            jvRequest[jss::taker_pays.c_str()] = jvTakerPays;
        }

        if (isRpcError(jvTakerGets))
        {
            return jvTakerGets;
        }
        else
        {
            jvRequest[jss::taker_gets.c_str()] = jvTakerGets;
        }

        if (jvParams.size() >= 3)
        {
            jvRequest[jss::issuer.c_str()] = jvParams[2u].as_string();
        }

        if (jvParams.size() >= 4 &&
            !jvParseLedger(jvRequest, std::string{jvParams[3u].as_string()}))
            return jvRequest;

        if (jvParams.size() >= 5)
        {
            int iLimit = jvParams[5u].as_int64();

            if (iLimit > 0)
                jvRequest[jss::limit.c_str()] = iLimit;
        }

        if (jvParams.size() >= 6 && jvParams[5u].as_int64())
        {
            jvRequest[jss::proof.c_str()] = true;
        }

        if (jvParams.size() == 7)
            jvRequest[jss::marker.c_str()] = jvParams[6u];

        return jvRequest;
    }

    // can_delete [<ledgerid>|<ledgerhash>|now|always|never]
    boost::json::value
    parseCanDelete(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        if (!jvParams.size())
            return jvRequest;

        std::string input = std::string{jvParams[0u].as_string()};
        if (input.find_first_not_of("0123456789") == std::string::npos)
            jvRequest["can_delete"] = jvParams[0u].as_uint64();
        else
            jvRequest["can_delete"] = input;

        return jvRequest;
    }

    // connect <ip[:port]> [port]
    boost::json::value
    parseConnect(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;
        std::string ip = std::string{jvParams[0u].as_string()};
        if (jvParams.size() == 2)
        {
            jvRequest[jss::ip.c_str()] = ip;
            jvRequest[jss::port.c_str()] = jvParams[1u].as_uint64();
            return jvRequest;
        }

        // handle case where there is one argument of the form ip:port
        if (std::count(ip.begin(), ip.end(), ':') == 1)
        {
            std::size_t colon = ip.find_last_of(":");
            jvRequest[jss::ip.c_str()] = std::string{ip, 0, colon};
            jvRequest[jss::port.c_str()] =
                boost::json::value{std::string{ip, colon + 1}}.as_uint64();
            return jvRequest;
        }

        // default case, no port
        jvRequest[jss::ip.c_str()] = ip;
        return jvRequest;
    }

    // deposit_authorized <source_account> <destination_account> [<ledger>]
    boost::json::value
    parseDepositAuthorized(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;
        jvRequest[jss::source_account.c_str()] = jvParams[0u].as_string();
        jvRequest[jss::destination_account.c_str()] = jvParams[1u].as_string();

        if (jvParams.size() == 3)
            jvParseLedger(jvRequest, std::string{jvParams[2u].as_string()});

        return jvRequest;
    }

    // Return an error for attemping to subscribe/unsubscribe via RPC.
    boost::json::value
    parseEvented(boost::json::array const& jvParams)
    {
        return rpcError(rpcNO_EVENTS);
    }

    // feature [<feature>] [accept|reject]
    boost::json::value
    parseFeature(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        if (jvParams.size() > 0)
            jvRequest[jss::feature.c_str()] = jvParams[0u].as_string();

        if (jvParams.size() > 1)
        {
            auto const action = jvParams[1u].as_string();

            // This may look reversed, but it's intentional: jss::vetoed
            // determines whether an amendment is vetoed - so "reject" means
            // that jss::vetoed is true.
            if (boost::iequals(action, "reject"))
                jvRequest[jss::vetoed.c_str()] = boost::json::value(true);
            else if (boost::iequals(action, "accept"))
                jvRequest[jss::vetoed.c_str()] = boost::json::value(false);
            else
                return rpcError(rpcINVALID_PARAMS);
        }

        return jvRequest;
    }

    // get_counts [<min_count>]
    boost::json::value
    parseGetCounts(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        if (jvParams.size())
            jvRequest[jss::min_count.c_str()] = jvParams[0u].as_uint64();

        return jvRequest;
    }

    // sign_for <account> <secret> <json> offline
    // sign_for <account> <secret> <json>
    boost::json::value
    parseSignFor(boost::json::array const& jvParams)
    {
        bool const bOffline =
            4 == jvParams.size() && jvParams[3u].as_string() == "offline";

        if (3 == jvParams.size() || bOffline)
        {
            boost::json::value txJSON(parse(jvParams[2u].as_string()));
            if (!txJSON.is_null())
            {
                // sign_for txJSON.
                boost::json::object jvRequest;

                jvRequest[jss::account.c_str()] = jvParams[0u].as_string();
                jvRequest[jss::secret.c_str()] = jvParams[1u].as_string();
                jvRequest[jss::tx_json.c_str()] = txJSON;

                if (bOffline)
                    jvRequest[jss::offline.c_str()] = true;

                return jvRequest;
            }
        }
        return rpcError(rpcINVALID_PARAMS);
    }

    // json <command> <json>
    boost::json::value
    parseJson(boost::json::array const& jvParams)
    {
        boost::json::value jvRequest(boost::json::parse(jvParams[1u].as_string()));

        JLOG(j_.trace()) << "RPC method: " << jvParams[0u];
        JLOG(j_.trace()) << "RPC json: " << jvParams[1u];

        if (!jvRequest.is_null())
        {
            if (!jvRequest.is_object())
                return rpcError(rpcINVALID_PARAMS);

            jvRequest.as_object()[jss::method.c_str()] = jvParams[0u];

            return jvRequest;
        }

        return rpcError(rpcINVALID_PARAMS);
    }

    bool
    isValidJson2(boost::json::value const& jv)
    {
        if (jv.is_array())
        {
            if (jv.as_array().size() == 0)
                return false;
            for (auto const& j : jv.as_array())
            {
                if (!isValidJson2(j))
                    return false;
            }
            return true;
        }
        if (jv.is_object())
        {
            if (jv.as_object().contains(jss::jsonrpc.c_str()) && jv.as_object().at(jss::jsonrpc.c_str()) == "2.0" &&
                jv.as_object().contains(jss::ripplerpc.c_str()) && jv.as_object().at(jss::ripplerpc.c_str()) == "2.0" &&
                jv.as_object().contains(jss::id.c_str()) && jv.as_object().contains(jss::method.c_str()))
            {
                if (jv.as_object().contains(jss::params.c_str()) &&
                    !(jv.as_object().at(jss::params.c_str()).is_null() || jv.as_object().at(jss::params.c_str()).is_array() ||
                      jv.as_object().at(jss::params.c_str()).is_object()))
                    return false;
                return true;
            }
        }
        return false;
    }

    boost::json::value
    parseJson2(boost::json::array const& jvParams)
    {
        boost::json::value jvVal(parse(jvParams[0u].as_string()));
        if (!jvVal.is_null() && isValidJson2(jvVal))
        {
            if (jvVal.is_object())
            {
                boost::json::object jv = jvVal.as_object();
                boost::json::object jv1;
                if (jv.contains(jss::params.c_str()))
                {
                    jv1 = jv[jss::params.c_str()].as_object();
                }
                jv1[jss::jsonrpc.c_str()] = jv[jss::jsonrpc.c_str()];
                jv1[jss::ripplerpc.c_str()] = jv[jss::ripplerpc.c_str()];
                jv1[jss::id.c_str()] = jv[jss::id.c_str()];
                jv1[jss::method.c_str()] = jv[jss::method.c_str()];
                return jv1;
            }

            // else jv.is_array()
            boost::json::array jv = jvVal.as_array();
            boost::json::array jv1;
            for (Json::UInt j = 0; j < jv.size(); ++j)
            {
                if (jv[j].as_object().contains(jss::params.c_str()))
                {
                    jv1[j] = jv[j].as_object()[jss::params.c_str()].as_object();
                }
                jv1[j].as_object()[jss::jsonrpc.c_str()] = jv[j].as_object()[jss::jsonrpc.c_str()];
                jv1[j].as_object()[jss::ripplerpc.c_str()] = jv[j].as_object()[jss::ripplerpc.c_str()];
                jv1[j].as_object()[jss::id.c_str()] = jv[j].as_object()[jss::id.c_str()];
                jv1[j].as_object()[jss::method.c_str()] = jv[j].as_object()[jss::method.c_str()];
            }
            return jv1;
        }
        auto jv_error = rpcError(rpcINVALID_PARAMS);
        if (jvVal.as_object().contains(jss::jsonrpc.c_str()))
            jv_error[jss::jsonrpc.c_str()] = jvVal.as_object()[jss::jsonrpc.c_str()];
        if (jvVal.as_object().contains(jss::ripplerpc.c_str()))
            jv_error[jss::ripplerpc.c_str()] = jvVal.as_object()[jss::ripplerpc.c_str()];
        if (jvVal.as_object().contains(jss::id.c_str()))
            jv_error[jss::id.c_str()] = jvVal.as_object()[jss::id.c_str()];
        return jv_error;
    }

    // ledger [id|index|current|closed|validated] [full|tx]
    boost::json::value
    parseLedger(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        if (!jvParams.size())
        {
            return jvRequest;
        }

        jvParseLedger(jvRequest, std::string{jvParams[0u].as_string()});

        if (2 == jvParams.size())
        {
            if (jvParams[1u].as_string() == "full")
            {
                jvRequest[jss::full.c_str()] = true;
            }
            else if (jvParams[1u].as_string() == "tx")
            {
                jvRequest[jss::transactions.c_str()] = true;
                jvRequest[jss::expand.c_str()] = true;
            }
        }

        return jvRequest;
    }

    // ledger_header <id>|<index>
    boost::json::value
    parseLedgerId(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        std::string strLedger = std::string{jvParams[0u].as_string()};

        if (strLedger.length() == 64)
        {
            jvRequest[jss::ledger_hash.c_str()] = strLedger;
        }
        else
        {
            jvRequest[jss::ledger_index.c_str()] =
                beast::lexicalCast<std::uint32_t>(strLedger);
        }

        return jvRequest;
    }

    // log_level:                           Get log levels
    // log_level <severity>:                Set master log level to the
    // specified severity log_level <partition> <severity>:    Set specified
    // partition to specified severity
    boost::json::value
    parseLogLevel(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        if (jvParams.size() == 1)
        {
            jvRequest[jss::severity.c_str()] = jvParams[0u].as_string();
        }
        else if (jvParams.size() == 2)
        {
            jvRequest[jss::partition.c_str()] = jvParams[0u].as_string();
            jvRequest[jss::severity.c_str()] = jvParams[1u].as_string();
        }

        return jvRequest;
    }

    // owner_info <account>|<account_public_key> [strict]
    // owner_info <seed>|<pass_phrase>|<key> [<ledger>] [strict]
    // account_info <account>|<account_public_key> [strict]
    // account_info <seed>|<pass_phrase>|<key> [<ledger>] [strict]
    // account_offers <account>|<account_public_key> [<ledger>] [strict]
    boost::json::value
    parseAccountItems(boost::json::array const& jvParams)
    {
        return parseAccountRaw1(jvParams);
    }

    boost::json::value
    parseAccountCurrencies(boost::json::array const& jvParams)
    {
        return parseAccountRaw1(jvParams);
    }

    // account_lines <account> <account>|"" [<ledger>]
    boost::json::value
    parseAccountLines(boost::json::array const& jvParams)
    {
        return parseAccountRaw2(jvParams, jss::peer);
    }

    // account_channels <account> <account>|"" [<ledger>]
    boost::json::value
    parseAccountChannels(boost::json::array const& jvParams)
    {
        return parseAccountRaw2(jvParams, jss::destination_account.c_str());
    }

    // channel_authorize: <private_key> [<key_type>] <channel_id> <drops>
    boost::json::value
    parseChannelAuthorize(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        unsigned int index = 0;

        if (jvParams.size() == 4)
        {
            jvRequest[jss::passphrase.c_str()] = jvParams[index];
            index++;

            if (!keyTypeFromString(std::string{jvParams[index].as_string()}))
                return rpcError(rpcBAD_KEY_TYPE);
            jvRequest[jss::key_type.c_str()] = jvParams[index];
            index++;
        }
        else
        {
            jvRequest[jss::secret.c_str()] = jvParams[index];
            index++;
        }

        {
            // verify the channel id is a valid 256 bit number
            uint256 channelId;
            if (!channelId.parseHex(jvParams[index].as_string()))
                return rpcError(rpcCHANNEL_MALFORMED);
            jvRequest[jss::channel_id.c_str()] = to_string(channelId);
            index++;
        }

        if (!jvParams[index].is_string() ||
            !to_uint64(std::string{jvParams[index].as_string()}))
            return rpcError(rpcCHANNEL_AMT_MALFORMED);
        jvRequest[jss::amount.c_str()] = jvParams[index];

        // If additional parameters are appended, be sure to increment index
        // here

        return jvRequest;
    }

    // channel_verify <public_key> <channel_id> <drops> <signature>
    boost::json::value
    parseChannelVerify(boost::json::array const& jvParams)
    {
        std::string const strPk = std::string{jvParams[0u].as_string()};

        if (!validPublicKey(strPk))
            return rpcError(rpcPUBLIC_MALFORMED);

        boost::json::object jvRequest;

        jvRequest[jss::public_key.c_str()] = strPk;
        {
            // verify the channel id is a valid 256 bit number
            uint256 channelId;
            if (!channelId.parseHex(jvParams[1u].as_string()))
                return rpcError(rpcCHANNEL_MALFORMED);
        }
        jvRequest[jss::channel_id.c_str()] = jvParams[1u].as_string();

        if (!jvParams[2u].is_string() || !to_uint64(std::string{jvParams[2u].as_string()}))
            return rpcError(rpcCHANNEL_AMT_MALFORMED);
        jvRequest[jss::amount.c_str()] = jvParams[2u];

        jvRequest[jss::signature.c_str()] = jvParams[3u].as_string();

        return jvRequest;
    }

    boost::json::value
    parseAccountRaw2(boost::json::array const& jvParams, char const* const acc2Field)
    {
        std::array<char const* const, 2> accFields{{jss::account.c_str(), acc2Field}};
        auto const nParams = jvParams.size();
        boost::json::object jvRequest;
        for (auto i = 0; i < nParams; ++i)
        {
            std::string strParam = std::string{jvParams[i].as_string()};

            if (i == 1 && strParam.empty())
                continue;

            // Parameters 0 and 1 are accounts
            if (i < 2)
            {
                if (parseBase58<PublicKey>(
                        TokenType::AccountPublic, strParam) ||
                    parseBase58<AccountID>(strParam) ||
                    parseGenericSeed(strParam))
                {
                    jvRequest[accFields[i]] = std::move(strParam);
                }
                else
                {
                    return rpcError(rpcACT_MALFORMED);
                }
            }
            else
            {
                if (jvParseLedger(jvRequest, strParam))
                    return jvRequest;
                return rpcError(rpcLGR_IDX_MALFORMED);
            }
        }

        return jvRequest;
    }

    // TODO: Get index from an alternate syntax: rXYZ:<index>
    boost::json::value
    parseAccountRaw1(boost::json::array const& jvParams)
    {
        std::string strIdent = std::string{jvParams[0u].as_string()};
        unsigned int iCursor = jvParams.size();
        bool bStrict = false;

        if (iCursor >= 2 && jvParams[iCursor - 1] == jss::strict.c_str())
        {
            bStrict = true;
            --iCursor;
        }

        if (!parseBase58<PublicKey>(TokenType::AccountPublic, strIdent) &&
            !parseBase58<AccountID>(strIdent) && !parseGenericSeed(strIdent))
            return rpcError(rpcACT_MALFORMED);

        // Get info on account.
        boost::json::object jvRequest;

        jvRequest[jss::account.c_str()] = strIdent;

        if (bStrict)
            jvRequest[jss::strict.c_str()] = 1;

        if (iCursor == 2 && !jvParseLedger(jvRequest, std::string{jvParams[1u].as_string()}))
            return rpcError(rpcLGR_IDX_MALFORMED);

        return jvRequest;
    }

    boost::json::value
    parseNodeToShard(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;
        jvRequest[jss::action.c_str()] = jvParams[0u].as_string();

        return jvRequest;
    }

    // peer_reservations_add <public_key> [<name>]
    boost::json::value
    parsePeerReservationsAdd(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;
        jvRequest[jss::public_key.c_str()] = jvParams[0u].as_string();
        if (jvParams.size() > 1)
        {
            jvRequest[jss::description.c_str()] = jvParams[1u].as_string();
        }
        return jvRequest;
    }

    // peer_reservations_del <public_key>
    boost::json::value
    parsePeerReservationsDel(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;
        jvRequest[jss::public_key.c_str()] = jvParams[0u].as_string();
        return jvRequest;
    }

    // ripple_path_find <json> [<ledger>]
    boost::json::value
    parseRipplePathFind(boost::json::array const& jvParams)
    {
        bool bLedger = 2 == jvParams.size();

        JLOG(j_.trace()) << "RPC json: " << jvParams[0u];
        boost::json::value jvRequest(parse(jvParams[0u].as_string()));

        if (!jvRequest.is_null())
        {
            if (bLedger)
            {
                jvParseLedger(jvRequest.as_object(), std::string{jvParams[1u].as_string()});
            }

            return jvRequest;
        }

        return rpcError(rpcINVALID_PARAMS);
    }

    // sign/submit any transaction to the network
    //
    // sign <private_key> <json> offline
    // submit <private_key> <json>
    // submit <tx_blob>
    boost::json::value
    parseSignSubmit(boost::json::array const& jvParams)
    {
        boost::json::value txJSON(parse(jvParams[1u].as_string()));
        bool const bOffline =
            3 == jvParams.size() && jvParams[2u].as_string() == "offline";

        if (1 == jvParams.size())
        {
            // Submitting tx_blob

            boost::json::object jvRequest;

            jvRequest[jss::tx_blob.c_str()] = jvParams[0u].as_string();

            return jvRequest;
        }
        else if (
            (2 == jvParams.size() || bOffline) &&
            !txJSON.is_null())
        {
            // Signing or submitting tx_json.
            boost::json::object jvRequest;

            jvRequest[jss::secret.c_str()] = jvParams[0u].as_string();
            jvRequest[jss::tx_json.c_str()] = txJSON;

            if (bOffline)
                jvRequest[jss::offline.c_str()] = true;

            return jvRequest;
        }

        return rpcError(rpcINVALID_PARAMS);
    }

    // submit any multisigned transaction to the network
    //
    // submit_multisigned <json>
    boost::json::value
    parseSubmitMultiSigned(boost::json::array const& jvParams)
    {
        if (1 == jvParams.size())
        {
            boost::json::value txJSON(parse(jvParams[0u].as_string()));
            if (!txJSON.is_null())
            {
                boost::json::object jvRequest;
                jvRequest[jss::tx_json.c_str()] = txJSON;
                return jvRequest;
            }
        }

        return rpcError(rpcINVALID_PARAMS);
    }

    // transaction_entry <tx_hash> <ledger_hash/ledger_index>
    boost::json::value
    parseTransactionEntry(boost::json::array const& jvParams)
    {
        // Parameter count should have already been verified.
        assert(jvParams.size() == 2);

        std::string const txHash = std::string{jvParams[0u].as_string()};
        if (txHash.length() != 64)
            return rpcError(rpcINVALID_PARAMS);

        boost::json::object jvRequest;
        jvRequest[jss::tx_hash.c_str()] = txHash;

        jvParseLedger(jvRequest, std::string{jvParams[1u].as_string()});

        // jvParseLedger inserts a "ledger_index" of 0 if it doesn't
        // find a match.
        if (jvRequest.contains(jss::ledger_index.c_str()) &&
            jvRequest[jss::ledger_index.c_str()] == 0)
            return rpcError(rpcINVALID_PARAMS);

        return jvRequest;
    }

    // tx <transaction_id>
    boost::json::value
    parseTx(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        if (jvParams.size() == 2 || jvParams.size() == 4)
        {
            if (jvParams[1u].as_string() == jss::binary.c_str())
                jvRequest[jss::binary.c_str()] = true;
        }

        if (jvParams.size() >= 3)
        {
            const auto offset = jvParams.size() == 3 ? 0 : 1;

            jvRequest[jss::min_ledger.c_str()] = jvParams[1u + offset].as_string();
            jvRequest[jss::max_ledger.c_str()] = jvParams[2u + offset].as_string();
        }

        jvRequest[jss::transaction.c_str()] = jvParams[0u].as_string();
        return jvRequest;
    }

    // tx_history <index>
    boost::json::value
    parseTxHistory(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        jvRequest[jss::start.c_str()] = jvParams[0u].as_uint64();

        return jvRequest;
    }

    // validation_create [<pass_phrase>|<seed>|<seed_key>]
    //
    // NOTE: It is poor security to specify secret information on the command
    // line.  This information might be saved in the command shell history file
    // (e.g. .bash_history) and it may be leaked via the process status command
    // (i.e. ps).
    boost::json::value
    parseValidationCreate(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        if (jvParams.size())
            jvRequest[jss::secret.c_str()] = jvParams[0u].as_string();

        return jvRequest;
    }

    // wallet_propose [<passphrase>]
    // <passphrase> is only for testing. Master seeds should only be generated
    // randomly.
    boost::json::value
    parseWalletPropose(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;

        if (jvParams.size())
            jvRequest[jss::passphrase.c_str()] = jvParams[0u].as_string();

        return jvRequest;
    }

    // parse gateway balances
    // gateway_balances [<ledger>] <issuer_account> [ <hotwallet> [ <hotwallet>
    // ]]

    boost::json::value
    parseGatewayBalances(boost::json::array const& jvParams)
    {
        unsigned int index = 0;
        const unsigned int size = jvParams.size();

        boost::json::object jvRequest;

        std::string param = std::string{jvParams[index++].as_string()};
        if (param.empty())
            return RPC::make_param_error("Invalid first parameter");

        if (param[0] != 'r')
        {
            if (param.size() == 64)
                jvRequest[jss::ledger_hash.c_str()] = param;
            else
                jvRequest[jss::ledger_index.c_str()] = param;

            if (size <= index)
                return RPC::make_param_error("Invalid hotwallet");

            param = jvParams[index++].as_string();
        }

        jvRequest[jss::account.c_str()] = param;

        if (index < size)
        {
            boost::json::array& hotWallets =
                jvRequest["hotwallet"].emplace_array();
            while (index < size)
                hotWallets.emplace_back(jvParams[index++].as_string());
        }

        return jvRequest;
    }

    // server_info [counters]
    boost::json::value
    parseServerInfo(boost::json::array const& jvParams)
    {
        boost::json::object jvRequest;
        if (jvParams.size() == 1 && jvParams[0u].as_string() == "counters")
            jvRequest[jss::counters.c_str()] = true;
        return jvRequest;
    }

public:
    //--------------------------------------------------------------------------

    explicit RPCParser(beast::Journal j) : j_(j)
    {
    }

    //--------------------------------------------------------------------------

    // Convert a rpc method and params to a request.
    // <-- { method: xyz, params: [... ] } or { error: ..., ... }
    boost::json::value
    parseCommand(
        std::string strMethod,
        boost::json::array jvParams,
        bool allowAnyCommand)
    {
        if (auto stream = j_.trace())
        {
            stream << "Method: '" << strMethod << "'";
            stream << "Params: " << jvParams;
        }

        struct Command
        {
            const char* name;
            parseFuncPtr parse;
            int minParams;
            int maxParams;
        };

        static constexpr Command commands[] = {
            // Request-response methods
            // - Returns an error, or the request.
            // - To modify the method, provide a new method in the request.
            {"account_currencies", &RPCParser::parseAccountCurrencies, 1, 3},
            {"account_info", &RPCParser::parseAccountItems, 1, 3},
            {"account_lines", &RPCParser::parseAccountLines, 1, 5},
            {"account_channels", &RPCParser::parseAccountChannels, 1, 3},
            {"account_nfts", &RPCParser::parseAccountItems, 1, 5},
            {"account_objects", &RPCParser::parseAccountItems, 1, 5},
            {"account_offers", &RPCParser::parseAccountItems, 1, 4},
            {"account_tx", &RPCParser::parseAccountTransactions, 1, 8},
            {"book_changes", &RPCParser::parseLedgerId, 1, 1},
            {"book_offers", &RPCParser::parseBookOffers, 2, 7},
            {"can_delete", &RPCParser::parseCanDelete, 0, 1},
            {"channel_authorize", &RPCParser::parseChannelAuthorize, 3, 4},
            {"channel_verify", &RPCParser::parseChannelVerify, 4, 4},
            {"connect", &RPCParser::parseConnect, 1, 2},
            {"consensus_info", &RPCParser::parseAsIs, 0, 0},
            {"deposit_authorized", &RPCParser::parseDepositAuthorized, 2, 3},
            {"download_shard", &RPCParser::parseDownloadShard, 2, -1},
            {"feature", &RPCParser::parseFeature, 0, 2},
            {"fetch_info", &RPCParser::parseFetchInfo, 0, 1},
            {"gateway_balances", &RPCParser::parseGatewayBalances, 1, -1},
            {"get_counts", &RPCParser::parseGetCounts, 0, 1},
            {"json", &RPCParser::parseJson, 2, 2},
            {"json2", &RPCParser::parseJson2, 1, 1},
            {"ledger", &RPCParser::parseLedger, 0, 2},
            {"ledger_accept", &RPCParser::parseAsIs, 0, 0},
            {"ledger_closed", &RPCParser::parseAsIs, 0, 0},
            {"ledger_current", &RPCParser::parseAsIs, 0, 0},
            //      {   "ledger_entry",         &RPCParser::parseLedgerEntry,
            //      -1, -1   },
            {"ledger_header", &RPCParser::parseLedgerId, 1, 1},
            {"ledger_request", &RPCParser::parseLedgerId, 1, 1},
            {"log_level", &RPCParser::parseLogLevel, 0, 2},
            {"logrotate", &RPCParser::parseAsIs, 0, 0},
            {"manifest", &RPCParser::parseManifest, 1, 1},
            {"node_to_shard", &RPCParser::parseNodeToShard, 1, 1},
            {"owner_info", &RPCParser::parseAccountItems, 1, 3},
            {"peers", &RPCParser::parseAsIs, 0, 0},
            {"ping", &RPCParser::parseAsIs, 0, 0},
            {"print", &RPCParser::parseAsIs, 0, 1},
            //      {   "profile",              &RPCParser::parseProfile, 1,  9
            //      },
            {"random", &RPCParser::parseAsIs, 0, 0},
            {"peer_reservations_add",
             &RPCParser::parsePeerReservationsAdd,
             1,
             2},
            {"peer_reservations_del",
             &RPCParser::parsePeerReservationsDel,
             1,
             1},
            {"peer_reservations_list", &RPCParser::parseAsIs, 0, 0},
            {"ripple_path_find", &RPCParser::parseRipplePathFind, 1, 2},
            {"sign", &RPCParser::parseSignSubmit, 2, 3},
            {"sign_for", &RPCParser::parseSignFor, 3, 4},
            {"submit", &RPCParser::parseSignSubmit, 1, 3},
            {"submit_multisigned", &RPCParser::parseSubmitMultiSigned, 1, 1},
            {"server_info", &RPCParser::parseServerInfo, 0, 1},
            {"server_state", &RPCParser::parseServerInfo, 0, 1},
            {"crawl_shards", &RPCParser::parseAsIs, 0, 2},
            {"stop", &RPCParser::parseAsIs, 0, 0},
            {"transaction_entry", &RPCParser::parseTransactionEntry, 2, 2},
            {"tx", &RPCParser::parseTx, 1, 4},
            {"tx_account", &RPCParser::parseTxAccount, 1, 7},
            {"tx_history", &RPCParser::parseTxHistory, 1, 1},
            {"unl_list", &RPCParser::parseAsIs, 0, 0},
            {"validation_create", &RPCParser::parseValidationCreate, 0, 1},
            {"validator_info", &RPCParser::parseAsIs, 0, 0},
            {"version", &RPCParser::parseAsIs, 0, 0},
            {"wallet_propose", &RPCParser::parseWalletPropose, 0, 1},
            {"internal", &RPCParser::parseInternal, 1, -1},

            // Evented methods
            {"path_find", &RPCParser::parseEvented, -1, -1},
            {"subscribe", &RPCParser::parseEvented, -1, -1},
            {"unsubscribe", &RPCParser::parseEvented, -1, -1},
        };

        auto const count = jvParams.size();

        for (auto const& command : commands)
        {
            if (strMethod == command.name)
            {
                if ((command.minParams >= 0 && count < command.minParams) ||
                    (command.maxParams >= 0 && count > command.maxParams))
                {
                    JLOG(j_.debug())
                        << "Wrong number of parameters for " << command.name
                        << " minimum=" << command.minParams
                        << " maximum=" << command.maxParams
                        << " actual=" << count;

                    return rpcError(rpcBAD_SYNTAX);
                }

                return (this->*(command.parse))(jvParams);
            }
        }

        // The command could not be found
        if (!allowAnyCommand)
            return rpcError(rpcUNKNOWN_COMMAND);

        return parseAsIs(jvParams);
    }
};

//------------------------------------------------------------------------------

//
// JSON-RPC protocol.  Bitcoin speaks version 1.0 for maximum compatibility,
// but uses JSON-RPC 1.1/2.0 standards for parts of the 1.0 standard that were
// unspecified (HTTP errors and contents of 'error').
//
// 1.0 spec: http://json-rpc.org/wiki/specification
// 1.2 spec: http://groups.google.com/group/json-rpc/web/json-rpc-over-http
//

std::string
JSONRPCRequest(
    std::string const& strMethod,
    boost::json::value const& params,
    boost::json::value const& id)
{
    boost::json::object request;
    request[jss::method.c_str()] = strMethod;
    request[jss::params.c_str()] = params;
    request[jss::id.c_str()] = id;
    return serialize(request) + "\n";
}

namespace {
// Special local exception type thrown when request can't be parsed.
class RequestNotParseable : public std::runtime_error
{
    using std::runtime_error::runtime_error;  // Inherit constructors
};
};  // namespace

struct RPCCallImp
{
    explicit RPCCallImp() = default;

    // VFALCO NOTE Is this a to-do comment or a doc comment?
    // Place the async result somewhere useful.
    static void
    callRPCHandler(boost::json::value* jvOutput, boost::json::value const& jvInput)
    {
        (*jvOutput) = jvInput;
    }

    static bool
    onResponse(
        std::function<void(boost::json::value const& jvInput)> callbackFuncP,
        const boost::system::error_code& ecResult,
        int iStatus,
        std::string const& strData,
        beast::Journal j)
    {
        if (callbackFuncP)
        {
            // Only care about the result, if we care to deliver it
            // callbackFuncP.

            // Receive reply
            if (strData.empty())
                Throw<std::runtime_error>("no response from server");

            // Parse reply
            JLOG(j.debug()) << "RPC reply: " << strData << std::endl;
            if (strData.find("Unable to parse request") == 0 ||
                strData.find(jss::invalid_API_version.c_str()) == 0)
                Throw<RequestNotParseable>(strData);
            boost::json::value jvReply(boost::json::parse(strData));
            if (jvReply.is_null())
                Throw<std::runtime_error>("couldn't parse reply from server");

            if (!jvReply.as_object().contains(jss::result.c_str()) || jvReply.as_object().contains(jss::error.c_str()) || jvReply.as_object().contains(jss::id.c_str()))
                Throw<std::runtime_error>(
                    "expected reply to have result, error and id properties");

            boost::json::object jvResult;

            jvResult["result"] = jvReply;

            (callbackFuncP)(jvResult);
        }

        return false;
    }

    // Build the request.
    static void
    onRequest(
        std::string const& strMethod,
        boost::json::value const& jvParams,
        std::unordered_map<std::string, std::string> const& headers,
        std::string const& strPath,
        boost::asio::streambuf& sb,
        std::string const& strHost,
        beast::Journal j)
    {
        JLOG(j.debug()) << "requestRPC: strPath='" << strPath << "'";

        std::ostream osRequest(&sb);
        osRequest << createHTTPPost(
            strHost,
            strPath,
            JSONRPCRequest(strMethod, jvParams, boost::json::value(1)),
            headers);
    }
};

//------------------------------------------------------------------------------

// Used internally by rpcClient.
static boost::json::value
rpcCmdLineToJson(
    std::vector<std::string> const& args,
    boost::json::value& retParams,
    beast::Journal j)
{
    boost::json::value jvRequest;

    RPCParser rpParser(j);
    boost::json::array jvRpcParams;

    for (int i = 1; i != args.size(); i++)
        jvRpcParams.emplace_back(args[i]);

    retParams.emplace_object();

    retParams.as_object()[jss::method.c_str()] = args[0];
    retParams.as_object()[jss::params.c_str()] = jvRpcParams;

    jvRequest = rpParser.parseCommand(args[0], jvRpcParams, true);

    auto insert_api_version = [](boost::json::value& jr) {
        if (jr.is_object() && !jr.as_object().contains(jss::error.c_str()) &&
            !jr.as_object().contains(jss::api_version.c_str()))
        {
            jr.as_object()[jss::api_version.c_str()] = RPC::apiMaximumSupportedVersion;
        }
    };

    if (jvRequest.is_object())
        insert_api_version(jvRequest);
    else if (jvRequest.is_array())
        std::for_each(jvRequest.as_array().begin(), jvRequest.as_array().end(), insert_api_version);

    JLOG(j.trace()) << "RPC Request: " << jvRequest << std::endl;
    return jvRequest;
}

boost::json::value
cmdLineToJSONRPC(std::vector<std::string> const& args, beast::Journal j)
{
    boost::json::value retParams;
    auto const paramsObj = rpcCmdLineToJson(args, retParams, j).as_object();

    boost::json::object jv;

    // Allow parser to rewrite method.
    jv[jss::method.c_str()] = paramsObj.contains(jss::method.c_str())
        ? std::string{paramsObj.at(jss::method.c_str()).as_string()}
        : args[0];

    // If paramsObj is not empty, put it in a [params] array.
    if (paramsObj.begin() != paramsObj.end())
    {
        auto& paramsArray = jv[jss::params.c_str()].emplace_array();
        paramsArray.emplace_back(paramsObj);
    }
    if (paramsObj.contains(jss::jsonrpc.c_str()))
        jv[jss::jsonrpc.c_str()] = paramsObj.at(jss::jsonrpc.c_str());
    if (paramsObj.contains(jss::ripplerpc.c_str()))
        jv[jss::ripplerpc.c_str()] = paramsObj.at(jss::ripplerpc.c_str());
    if (paramsObj.contains(jss::id.c_str()))
        jv[jss::id.c_str()] = paramsObj.at(jss::id.c_str());
    return jv;
}

//------------------------------------------------------------------------------

std::pair<int, boost::json::value>
rpcClient(
    std::vector<std::string> const& args,
    Config const& config,
    Logs& logs,
    std::unordered_map<std::string, std::string> const& headers)
{
    static_assert(
        rpcBAD_SYNTAX == 1 && rpcSUCCESS == 0,
        "Expect specific rpc enum values.");
    if (args.empty())
        return {rpcBAD_SYNTAX, {}};  // rpcBAD_SYNTAX = print usage

    int nRet = rpcSUCCESS;
    boost::json::value jvOutput;
    boost::json::value jvRequest;

    try
    {
        boost::json::value jvRpc;
        jvRequest = rpcCmdLineToJson(args, jvRpc, logs.journal("RPCParser"));


        if (jvRequest.is_object() && jvRequest.as_object().contains(jss::error.c_str()))
        {
            jvOutput = jvRequest.as_object();
            jvOutput.as_object()["rpc"] = jvRpc;
        }
        else
        {
            ServerHandler::Setup setup;
            try
            {
                setup = setup_ServerHandler(
                    config,
                    beast::logstream{logs.journal("HTTPClient").warn()});
            }
            catch (std::exception const&)
            {
                // ignore any exceptions, so the command
                // line client works without a config file
            }

            if (config.rpc_ip)
            {
                setup.client.ip = config.rpc_ip->address().to_string();
                setup.client.port = config.rpc_ip->port();
            }

            boost::json::array jvParams;

            if (!setup.client.admin_user.empty())
                jvRequest.as_object()["admin_user"] = setup.client.admin_user;

            if (!setup.client.admin_password.empty())
                jvRequest.as_object()["admin_password"] = setup.client.admin_password;

            if (jvRequest.is_object())
                jvParams.emplace_back(jvRequest);
            else if (jvRequest.is_array())
            {
                for (Json::UInt i = 0; i < jvRequest.as_array().size(); ++i)
                    jvParams.emplace_back(jvRequest.as_array()[i]);
            }

            {
                boost::asio::io_service isService;
                RPCCall::fromNetwork(
                    isService,
                    setup.client.ip,
                    setup.client.port,
                    setup.client.user,
                    setup.client.password,
                    "",
                    jvRequest.as_object().contains(
                        jss::method.c_str())  // Allow parser to rewrite method.
                        ? std::string{jvRequest.as_object()[jss::method.c_str()].as_string()}
                        : jvRequest.is_array() ? "batch" : args[0],
                    jvParams,                  // Parsed, execute.
                    setup.client.secure != 0,  // Use SSL
                    config.quiet(),
                    logs,
                    std::bind(
                        RPCCallImp::callRPCHandler,
                        &jvOutput,
                        std::placeholders::_1),
                    headers);
                isService.run();  // This blocks until there are no more
                                  // outstanding async calls.
            }
            if (jvOutput.as_object().contains("result"))
            {
                // Had a successful JSON-RPC 2.0 call.
                jvOutput = jvOutput.as_object()["result"];

                // jvOutput may report a server side error.
                // It should report "status".
            }
            else
            {
                // Transport error.
                boost::json::value jvRpcError = jvOutput;

                jvOutput = rpcError(rpcJSON_RPC);
                jvOutput.as_object()["result"] = jvRpcError;
            }

            // If had an error, supply invocation in result.
            if (jvOutput.as_object().contains(jss::error.c_str()))
            {
                jvOutput.as_object()["rpc"] =
                    jvRpc;  // How the command was seen as method + params.
                jvOutput.as_object()["request_sent"] =
                    jvRequest;  // How the command was translated.
            }
        }

        if (jvOutput.as_object().contains(jss::error.c_str()))
        {
            jvOutput.as_object()[jss::status.c_str()] = "error";
            if (jvOutput.as_object().contains(jss::error_code.c_str()))
                nRet = std::stoi(std::string{jvOutput.as_object()[jss::error_code.c_str()].as_string()});
            else if (jvOutput.as_object()[jss::error.c_str()].as_object().contains(jss::error_code.c_str()))
                nRet =
                    std::stoi(std::string{jvOutput.as_object()[jss::error.c_str()].as_object()[jss::error_code.c_str()].as_string()}); // Keshava: can I use std::string_view instead?
            else
                nRet = rpcBAD_SYNTAX;
        }

        // YYY We could have a command line flag for single line output for
        // scripts. YYY We would intercept output here and simplify it.
    }
    catch (RequestNotParseable& e)
    {
        jvOutput = rpcError(rpcINVALID_PARAMS);
        jvOutput.as_object()["error_what"] = e.what();
        nRet = rpcINVALID_PARAMS;
    }
    catch (std::exception& e)
    {
        jvOutput = rpcError(rpcINTERNAL);
        jvOutput.as_object()["error_what"] = e.what();
        nRet = rpcINTERNAL;
    }

    return {nRet, std::move(jvOutput)};
}

//------------------------------------------------------------------------------

namespace RPCCall {

int
fromCommandLine(
    Config const& config,
    const std::vector<std::string>& vCmd,
    Logs& logs)
{
    auto const result = rpcClient(vCmd, config, logs);

    std::cout << serialize(result.second);

    return result.first;
}

//------------------------------------------------------------------------------

void
fromNetwork(
    boost::asio::io_service& io_service,
    std::string const& strIp,
    const std::uint16_t iPort,
    std::string const& strUsername,
    std::string const& strPassword,
    std::string const& strPath,
    std::string const& strMethod,
    boost::json::value const& jvParams,
    const bool bSSL,
    const bool quiet,
    Logs& logs,
    std::function<void(boost::json::value const& jvInput)> callbackFuncP,
    std::unordered_map<std::string, std::string> headers)
{
    auto j = logs.journal("HTTPClient");

    // Connect to localhost
    if (!quiet)
    {
        JLOG(j.info()) << (bSSL ? "Securely connecting to " : "Connecting to ")
                       << strIp << ":" << iPort << std::endl;
    }

    // HTTP basic authentication
    headers["Authorization"] =
        std::string("Basic ") + base64_encode(strUsername + ":" + strPassword);

    // Send request

    // Number of bytes to try to receive if no
    // Content-Length header received
    constexpr auto RPC_REPLY_MAX_BYTES = megabytes(256);

    using namespace std::chrono_literals;
    auto constexpr RPC_NOTIFY = 10min;

    HTTPClient::request(
        bSSL,
        io_service,
        strIp,
        iPort,
        std::bind(
            &RPCCallImp::onRequest,
            strMethod,
            jvParams,
            headers,
            strPath,
            std::placeholders::_1,
            std::placeholders::_2,
            j),
        RPC_REPLY_MAX_BYTES,
        RPC_NOTIFY,
        std::bind(
            &RPCCallImp::onResponse,
            callbackFuncP,
            std::placeholders::_1,
            std::placeholders::_2,
            std::placeholders::_3,
            j),
        j);
}

}  // namespace RPCCall

}  // namespace ripple
