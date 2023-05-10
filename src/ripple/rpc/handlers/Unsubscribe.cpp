//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

boost::json::object
doUnsubscribe(RPC::JsonContext& context)
{
    InfoSub::pointer ispSub;
    boost::json::object jvResult;
    bool removeUrl{false};

    if (!context.infoSub && !context.params.contains(jss::url.c_str()))
    {
        // Must be a JSON-RPC call.
        return rpcError(rpcINVALID_PARAMS);
    }

    if (context.params.contains(jss::url.c_str()))
    {
        if (context.role != Role::ADMIN)
            return rpcError(rpcNO_PERMISSION);

        std::string strUrl = context.params[jss::url].asString();
        ispSub = context.netOps.findRpcSub(strUrl);
        if (!ispSub)
            return jvResult;
        removeUrl = true;
    }
    else
    {
        ispSub = context.infoSub;
    }

    if (context.params.contains(jss::streams.c_str()))
    {
        if (!context.params[jss::streams.c_str()].is_array())
            return rpcError(rpcINVALID_PARAMS);

        for (auto& it : context.params[jss::streams.c_str()].as_array())
        {
            if (!it.is_string())
                return rpcError(rpcSTREAM_MALFORMED);

            std::string streamName{it.as_string()};
            if (streamName == "server")
            {
                context.netOps.unsubServer(ispSub->getSeq());
            }
            else if (streamName == "ledger")
            {
                context.netOps.unsubLedger(ispSub->getSeq());
            }
            else if (streamName == "manifests")
            {
                context.netOps.unsubManifests(ispSub->getSeq());
            }
            else if (streamName == "transactions")
            {
                context.netOps.unsubTransactions(ispSub->getSeq());
            }
            else if (
                streamName == "transactions_proposed" ||
                streamName == "rt_transactions")  // DEPRECATED
            {
                context.netOps.unsubRTTransactions(ispSub->getSeq());
            }
            else if (streamName == "validations")
            {
                context.netOps.unsubValidations(ispSub->getSeq());
            }
            else if (streamName == "peer_status")
            {
                context.netOps.unsubPeerStatus(ispSub->getSeq());
            }
            else if (streamName == "consensus")
            {
                context.netOps.unsubConsensus(ispSub->getSeq());
            }
            else
            {
                return rpcError(rpcSTREAM_MALFORMED);
            }
        }
    }

    auto accountsProposed = context.params.contains(jss::accounts_proposed.c_str())
        ? jss::accounts_proposed
        : jss::rt_accounts;  // DEPRECATED
    if (context.params.contains(accountsProposed.c_str()))
    {
        if (!context.params[accountsProposed.c_str()].is_array())
            return rpcError(rpcINVALID_PARAMS);

        auto ids = RPC::parseAccountIds(context.params[accountsProposed.c_str()]);
        if (ids.empty())
            return rpcError(rpcACT_MALFORMED);
        context.netOps.unsubAccount(ispSub, ids, true);
    }

    if (context.params.contains(jss::accounts.c_str()))
    {
        if (!context.params[jss::accounts.c_str()].is_array())
            return rpcError(rpcINVALID_PARAMS);

        auto ids = RPC::parseAccountIds(context.params[jss::accounts.c_str()]);
        if (ids.empty())
            return rpcError(rpcACT_MALFORMED);
        context.netOps.unsubAccount(ispSub, ids, false);
    }

    if (context.params.contains(jss::account_history_tx_stream.c_str()))
    {
        auto const& req = context.params[jss::account_history_tx_stream.c_str()].as_object();
        if (!req.contains(jss::account.c_str()) || !req.at(jss::account.c_str()).is_string())
            return rpcError(rpcINVALID_PARAMS);

        auto const id = parseBase58<AccountID>(req[jss::account.c_str()].asString());
        if (!id)
            return rpcError(rpcINVALID_PARAMS);

        bool stopHistoryOnly = false;
        if (req.contains(jss::stop_history_tx_only.c_str()))
        {
            if (!req.at(jss::stop_history_tx_only.c_str()).is_bool())
                return rpcError(rpcINVALID_PARAMS);
            stopHistoryOnly = req.at(jss::stop_history_tx_only.c_str()).as_bool();
        }
        context.netOps.unsubAccountHistory(ispSub, *id, stopHistoryOnly);

        JLOG(context.j.debug())
            << "doUnsubscribe: account_history_tx_stream: " << toBase58(*id)
            << " stopHistoryOnly=" << (stopHistoryOnly ? "true" : "false");
    }

    if (context.params.contains(jss::books.c_str()))
    {
        if (!context.params[jss::books.c_str()].is_array())
            return rpcError(rpcINVALID_PARAMS);

        for (auto& jv : context.params[jss::books.c_str()].as_array())
        {
            if (!jv.isObject() || !jv.contains(jss::taker_pays) ||
                !jv.contains(jss::taker_gets) ||
                !jv[jss::taker_pays.c_str()].isObjectOrNull() ||
                !jv[jss::taker_gets.c_str()].isObjectOrNull())
            {
                return rpcError(rpcINVALID_PARAMS);
            }

            Json::Value taker_pays = jv[jss::taker_pays.c_str()];
            Json::Value taker_gets = jv[jss::taker_gets.c_str()];

            Book book;

            // Parse mandatory currency.
            if (!taker_pays.contains(jss::currency) ||
                !to_currency(
                    book.in.currency, taker_pays[jss::currency.c_str()].asString()))
            {
                JLOG(context.j.info()) << "Bad taker_pays currency.";
                return rpcError(rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (
                ((taker_pays.contains(jss::issuer)) &&
                 (!taker_pays[jss::issuer.c_str()].isString() ||
                  !to_issuer(
                      book.in.account, taker_pays[jss::issuer.c_str()].asString())))
                // Don't allow illegal issuers.
                || !isConsistent(book.in) || noAccount() == book.in.account)
            {
                JLOG(context.j.info()) << "Bad taker_pays issuer.";

                return rpcError(rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (!taker_gets.contains(jss::currency) ||
                !to_currency(
                    book.out.currency, taker_gets[jss::currency.c_str()].asString()))
            {
                JLOG(context.j.info()) << "Bad taker_gets currency.";

                return rpcError(rpcDST_AMT_MALFORMED);
            }
            // Parse optional issuer.
            else if (
                ((taker_gets.contains(jss::issuer)) &&
                 (!taker_gets[jss::issuer.c_str()].isString() ||
                  !to_issuer(
                      book.out.account, taker_gets[jss::issuer.c_str()].asString())))
                // Don't allow illegal issuers.
                || !isConsistent(book.out) || noAccount() == book.out.account)
            {
                JLOG(context.j.info()) << "Bad taker_gets issuer.";

                return rpcError(rpcDST_ISR_MALFORMED);
            }

            if (book.in == book.out)
            {
                JLOG(context.j.info()) << "taker_gets same as taker_pays.";
                return rpcError(rpcBAD_MARKET);
            }

            context.netOps.unsubBook(ispSub->getSeq(), book);

            // both_sides is deprecated.
            if ((jv.contains(jss::both) && jv[jss::both.c_str()].asBool()) ||
                (jv.contains(jss::both_sides) && jv[jss::both_sides.c_str()].asBool()))
            {
                context.netOps.unsubBook(ispSub->getSeq(), reversed(book));
            }
        }
    }

    if (removeUrl)
    {
        context.netOps.tryRemoveRpcSub(context.params[jss::url.c_str()].asString());
    }

    return jvResult;
}

}  // namespace ripple
