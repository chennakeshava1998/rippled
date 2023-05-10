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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/net/RPCSub.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

boost::json::object
doSubscribe(RPC::JsonContext& context)
{
    InfoSub::pointer ispSub;
    boost::json::object jvResult;

    if (!context.infoSub && !context.params.contains(jss::url.c_str()))
    {
        // Must be a JSON-RPC call.
        JLOG(context.j.info()) << "doSubscribe: RPC subscribe requires a url";
        return rpcError(rpcINVALID_PARAMS);
    }

    if (context.params.contains(jss::url.c_str()))
    {
        if (context.role != Role::ADMIN)
            return rpcError(rpcNO_PERMISSION);

        std::string strUrl{context.params[jss::url.c_str()].as_string()};
        std::string strUsername = context.params.contains(jss::url_username.c_str())
            ? context.params[jss::url_username.c_str()].as_string().c_str()
            : "";
        std::string strPassword = context.params.contains(jss::url_password.c_str())
            ? context.params[jss::url_password.c_str()].as_string().c_str()
            : "";

        // DEPRECATED
        if (context.params.contains(jss::username.c_str()))
            strUsername = context.params[jss::username.c_str()].as_string();

        // DEPRECATED
        if (context.params.contains(jss::password.c_str()))
            strPassword = context.params[jss::password.c_str()].as_string();

        ispSub = context.netOps.findRpcSub(strUrl);
        if (!ispSub)
        {
            JLOG(context.j.debug()) << "doSubscribe: building: " << strUrl;
            try
            {
                auto rspSub = make_RPCSub(
                    context.app.getOPs(),
                    context.app.getIOService(),
                    context.app.getJobQueue(),
                    strUrl,
                    strUsername,
                    strPassword,
                    context.app.logs());
                ispSub = context.netOps.addRpcSub(
                    strUrl, std::dynamic_pointer_cast<InfoSub>(rspSub));
            }
            catch (std::runtime_error& ex)
            {
                return RPC::make_param_error(ex.what());
            }
        }
        else
        {
            JLOG(context.j.trace()) << "doSubscribe: reusing: " << strUrl;

            if (auto rpcSub = std::dynamic_pointer_cast<RPCSub>(ispSub))
            {
                // Why do we need to check contains against jss::username and
                // jss::password here instead of just setting the username and
                // the password? What about url_username and url_password?
                if (context.params.contains(jss::username.c_str()))
                    rpcSub->setUsername(strUsername);

                if (context.params.contains(jss::password.c_str()))
                    rpcSub->setPassword(strPassword);
            }
        }
    }
    else
    {
        ispSub = context.infoSub;
    }

    if (context.params.contains(jss::streams.c_str()))
    {
        if (!context.params[jss::streams.c_str()].is_array())
        {
            JLOG(context.j.info()) << "doSubscribe: streams requires an array.";
            return rpcError(rpcINVALID_PARAMS);
        }

        for (auto const& it : context.params[jss::streams.c_str()].as_array())
        {
            if (!it.is_string())
                return rpcError(rpcSTREAM_MALFORMED);

            std::string streamName{it.as_string()};
            if (streamName == "server")
            {
                if (context.app.config().reporting())
                    return rpcError(rpcREPORTING_UNSUPPORTED);
                context.netOps.subServer(
                    ispSub, jvResult, context.role == Role::ADMIN);
            }
            else if (streamName == "ledger")
            {
                context.netOps.subLedger(ispSub, jvResult);
            }
            else if (streamName == "book_changes")
            {
                context.netOps.subBookChanges(ispSub);
            }
            else if (streamName == "manifests")
            {
                context.netOps.subManifests(ispSub);
            }
            else if (streamName == "transactions")
            {
                context.netOps.subTransactions(ispSub);
            }
            else if (
                streamName == "transactions_proposed" ||
                streamName == "rt_transactions")  // DEPRECATED
            {
                context.netOps.subRTTransactions(ispSub);
            }
            else if (streamName == "validations")
            {
                context.netOps.subValidations(ispSub);
            }
            else if (streamName == "peer_status")
            {
                if (context.app.config().reporting())
                    return rpcError(rpcREPORTING_UNSUPPORTED);
                if (context.role != Role::ADMIN)
                    return rpcError(rpcNO_PERMISSION);
                context.netOps.subPeerStatus(ispSub);
            }
            else if (streamName == "consensus")
            {
                if (context.app.config().reporting())
                    return rpcError(rpcREPORTING_UNSUPPORTED);
                context.netOps.subConsensus(ispSub);
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
        context.netOps.subAccount(ispSub, ids, true);
    }

    if (context.params.contains(jss::accounts.c_str()))
    {
        if (!context.params[jss::accounts.c_str()].is_array())
            return rpcError(rpcINVALID_PARAMS);

        auto ids = RPC::parseAccountIds(context.params[jss::accounts.c_str()]);
        if (ids.empty())
            return rpcError(rpcACT_MALFORMED);
        context.netOps.subAccount(ispSub, ids, false);
        JLOG(context.j.debug()) << "doSubscribe: accounts: " << ids.size();
    }

    if (context.params.contains(jss::account_history_tx_stream.c_str()))
    {
        if (!context.app.config().useTxTables())
            return rpcError(rpcNOT_ENABLED);

        context.loadType = Resource::feeMediumBurdenRPC;
        auto const& req = context.params[jss::account_history_tx_stream.c_str()].as_object();
        if (!req.contains(jss::account.c_str()) || !req.at(jss::account.c_str()).is_string())
            return rpcError(rpcINVALID_PARAMS);

        auto const id = parseBase58<AccountID>(req.at(jss::account.c_str()).as_string().c_str());
        if (!id)
            return rpcError(rpcINVALID_PARAMS);

        if (auto result = context.netOps.subAccountHistory(ispSub, *id);
            result != rpcSUCCESS)
        {
            return rpcError(result);
        }

        jvResult[jss::warning.c_str()] =
            "account_history_tx_stream is an experimental feature and likely "
            "to be removed in the future";
        JLOG(context.j.debug())
            << "doSubscribe: account_history_tx_stream: " << toBase58(*id);
    }

    if (context.params.contains(jss::books.c_str()))
    {
        if (!context.params[jss::books.c_str()].is_array())
            return rpcError(rpcINVALID_PARAMS);

        for (auto& j : context.params[jss::books.c_str()].as_array())
        {
            if (!j.is_object() || !j.as_object().contains(jss::taker_pays.c_str()) ||
                !j.as_object().contains(jss::taker_gets.c_str()) ||
                !(j.as_object()[jss::taker_pays.c_str()].is_object() || j.as_object()[jss::taker_pays.c_str()].is_null()) ||
                !(j.as_object()[jss::taker_gets.c_str()].is_object() || j.as_object()[jss::taker_gets.c_str()].is_object()))
                return rpcError(rpcINVALID_PARAMS);

            Book book;
            boost::json::object taker_pays = j.as_object()[jss::taker_pays.c_str()].as_object();
            boost::json::object taker_gets = j.as_object()[jss::taker_gets.c_str()].as_object();

            // Parse mandatory currency.
            if (!taker_pays.contains(jss::currency.c_str()) ||
                !to_currency(
                    book.in.currency, taker_pays[jss::currency.c_str()].as_string().c_str()))
            {
                JLOG(context.j.info()) << "Bad taker_pays currency.";
                return rpcError(rpcSRC_CUR_MALFORMED);
            }

            // Parse optional issuer.
            if (((taker_pays.contains(jss::issuer.c_str())) &&
                 (!taker_pays[jss::issuer.c_str()].is_string() ||
                  !to_issuer(
                      book.in.account, taker_pays[jss::issuer.c_str()].as_string().c_str())))
                // Don't allow illegal issuers.
                || (!book.in.currency != !book.in.account) ||
                noAccount() == book.in.account)
            {
                JLOG(context.j.info()) << "Bad taker_pays issuer.";
                return rpcError(rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (!taker_gets.contains(jss::currency.c_str()) ||
                !to_currency(
                    book.out.currency, taker_gets[jss::currency.c_str()].as_string().c_str()))
            {
                JLOG(context.j.info()) << "Bad taker_gets currency.";
                return rpcError(rpcDST_AMT_MALFORMED);
            }

            // Parse optional issuer.
            if (((taker_gets.contains(jss::issuer.c_str())) &&
                 (!taker_gets[jss::issuer.c_str()].is_string() ||
                  !to_issuer(
                      book.out.account, taker_gets[jss::issuer.c_str()].as_string().c_str())))
                // Don't allow illegal issuers.
                || (!book.out.currency != !book.out.account) ||
                noAccount() == book.out.account)
            {
                JLOG(context.j.info()) << "Bad taker_gets issuer.";
                return rpcError(rpcDST_ISR_MALFORMED);
            }

            if (book.in.currency == book.out.currency &&
                book.in.account == book.out.account)
            {
                JLOG(context.j.info()) << "taker_gets same as taker_pays.";
                return rpcError(rpcBAD_MARKET);
            }

            std::optional<AccountID> takerID;

            if (j.as_object().contains(jss::taker.c_str()))
            {
                takerID = parseBase58<AccountID>(j.as_object()[jss::taker.c_str()].as_string().c_str());
                if (!takerID)
                    return rpcError(rpcBAD_ISSUER);
            }

            if (!isConsistent(book))
            {
                JLOG(context.j.warn()) << "Bad market: " << book;
                return rpcError(rpcBAD_MARKET);
            }

            context.netOps.subBook(ispSub, book);

            // both_sides is deprecated.
            bool const both =
                (j.as_object().contains(jss::both.c_str()) && j.as_object()[jss::both.c_str()].as_bool()) ||
                (j.as_object().contains(jss::both_sides.c_str()) && j.as_object()[jss::both_sides.c_str()].as_bool());

            if (both)
                context.netOps.subBook(ispSub, reversed(book));

            // state_now is deprecated.
            if ((j.as_object().contains(jss::snapshot.c_str()) && j.as_object()[jss::snapshot.c_str()].as_bool()) ||
                (j.as_object().contains(jss::state_now.c_str()) && j.as_object()[jss::state_now.c_str()].as_bool()))
            {
                context.loadType = Resource::feeMediumBurdenRPC;
                std::shared_ptr<ReadView const> lpLedger =
                    context.app.getLedgerMaster().getPublishedLedger();
                if (lpLedger)
                {
                    const boost::json::value jvMarker;
                    boost::json::object jvOffers;

                    auto add = [&](Json::StaticString field) {
                        context.netOps.getBookPage(
                            lpLedger,
                            field == jss::asks ? reversed(book) : book,
                            takerID ? *takerID : noAccount(),
                            false,
                            RPC::Tuning::bookOffers.rdefault,
                            jvMarker,
                            jvOffers);

                        if (jvResult.contains(field.c_str()))
                        {
                            jvResult[field.c_str()].emplace_array();
                            for (auto const& e : jvOffers[jss::offers.c_str()].as_array())
                                jvResult[field.c_str()].as_array().emplace_back(e);
                        }
                        else
                        {
                            jvResult[field.c_str()] = jvOffers[jss::offers.c_str()];
                        }
                    };

                    if (both)
                    {
                        add(jss::bids);
                        add(jss::asks);
                    }
                    else
                    {
                        add(jss::offers);
                    }
                }
            }
        }
    }

    return jvResult;
}

}  // namespace ripple
