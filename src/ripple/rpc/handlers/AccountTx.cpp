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
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/rdb/backend/PostgresDatabase.h>
#include <ripple/app/rdb/backend/SQLiteDatabase.h>
#include <ripple/core/Pg.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/DeliveredAmount.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/impl/RPCHelpers.h>

#include <grpcpp/grpcpp.h>

namespace ripple {

using TxnsData = RelationalDatabase::AccountTxs;
using TxnsDataBinary = RelationalDatabase::MetaTxsList;
using TxnDataBinary = RelationalDatabase::txnMetaLedgerType;
using AccountTxArgs = RelationalDatabase::AccountTxArgs;
using AccountTxResult = RelationalDatabase::AccountTxResult;

using LedgerShortcut = RelationalDatabase::LedgerShortcut;
using LedgerSpecifier = RelationalDatabase::LedgerSpecifier;

// parses args into a ledger specifier, or returns a Json object on error
std::variant<std::optional<LedgerSpecifier>, boost::json::object>
parseLedgerArgs(boost::json::object const& params)
{
    boost::json::object response;
    if (params.contains(jss::ledger_index_min.c_str()) ||
        params.contains(jss::ledger_index_max.c_str()))
    {
        uint32_t min = params.contains(jss::ledger_index_min.c_str()) &&
                params.at(jss::ledger_index_min.c_str()).as_int64() >= 0
            ? params.at(jss::ledger_index_min.c_str()).as_int64()
            : 0;
        uint32_t max = params.contains(jss::ledger_index_max.c_str()) &&
                params.at(jss::ledger_index_max.c_str()).as_int64() >= 0
            ? params.at(jss::ledger_index_max.c_str()).as_uint64()
            : UINT32_MAX;

        return LedgerRange{min, max};
    }
    else if (params.contains(jss::ledger_hash.c_str()))
    {
        auto& hashValue = params.at(jss::ledger_hash.c_str());
        if (!hashValue.is_string())
        {
            RPC::Status status{rpcINVALID_PARAMS, "ledgerHashNotString"};
            status.inject(response);
            return response;
        }

        LedgerHash hash;
        if (!hash.parseHex(hashValue.as_string()))
        {
            RPC::Status status{rpcINVALID_PARAMS, "ledgerHashMalformed"};
            status.inject(response);
            return response;
        }
        return hash;
    }
    else if (params.contains(jss::ledger_index.c_str()))
    {
        LedgerSpecifier ledger;
        if (params.at(jss::ledger_index.c_str()).is_number())
            // Keshava: Why do I need to do this cast? why is boost::json's uint_64 type not compatible with LedgerSpecifier?
            ledger = static_cast<unsigned int>(params.at(jss::ledger_index.c_str()).as_uint64());
        else
        {
            std::string ledgerStr{params.at(jss::ledger_index.c_str()).as_string()};

            if (ledgerStr == "current" || ledgerStr.empty())
                ledger = LedgerShortcut::CURRENT;
            else if (ledgerStr == "closed")
                ledger = LedgerShortcut::CLOSED;
            else if (ledgerStr == "validated")
                ledger = LedgerShortcut::VALIDATED;
            else
            {
                RPC::Status status{
                    rpcINVALID_PARAMS, "ledger_index string malformed"};
                status.inject(response);
                return response;
            }
        }
        return ledger;
    }
    return std::optional<LedgerSpecifier>{};
}

std::variant<LedgerRange, RPC::Status>
getLedgerRange(
    RPC::Context& context,
    std::optional<LedgerSpecifier> const& ledgerSpecifier)
{
    std::uint32_t uValidatedMin;
    std::uint32_t uValidatedMax;
    bool bValidated =
        context.ledgerMaster.getValidatedRange(uValidatedMin, uValidatedMax);

    if (!bValidated)
    {
        // Don't have a validated ledger range.
        if (context.apiVersion == 1)
            return rpcLGR_IDXS_INVALID;
        return rpcNOT_SYNCED;
    }

    std::uint32_t uLedgerMin = uValidatedMin;
    std::uint32_t uLedgerMax = uValidatedMax;
    // Does request specify a ledger or ledger range?
    if (ledgerSpecifier)
    {
        auto const status = std::visit(
            [&](auto const& ls) -> RPC::Status {
                using T = std::decay_t<decltype(ls)>;
                if constexpr (std::is_same_v<T, LedgerRange>)
                {
                    if (ls.min > uValidatedMin)
                    {
                        uLedgerMin = ls.min;
                    }
                    if (ls.max < uValidatedMax)
                    {
                        uLedgerMax = ls.max;
                    }
                    if (uLedgerMax < uLedgerMin)
                    {
                        if (context.apiVersion == 1)
                            return rpcLGR_IDXS_INVALID;
                        return rpcINVALID_LGR_RANGE;
                    }
                }
                else
                {
                    std::shared_ptr<ReadView const> ledgerView;
                    auto const status = getLedger(ledgerView, ls, context);
                    if (!ledgerView)
                    {
                        return status;
                    }

                    bool validated = RPC::isValidated(
                        context.ledgerMaster, *ledgerView, context.app);

                    if (!validated || ledgerView->info().seq > uValidatedMax ||
                        ledgerView->info().seq < uValidatedMin)
                    {
                        return rpcLGR_NOT_VALIDATED;
                    }
                    uLedgerMin = uLedgerMax = ledgerView->info().seq;
                }
                return RPC::Status::OK;
            },
            *ledgerSpecifier);

        if (status)
            return status;
    }
    return LedgerRange{uLedgerMin, uLedgerMax};
}

std::pair<AccountTxResult, RPC::Status>
doAccountTxHelp(RPC::Context& context, AccountTxArgs const& args)
{
    context.loadType = Resource::feeMediumBurdenRPC;
    if (context.app.config().reporting())
    {
        auto const db = dynamic_cast<PostgresDatabase*>(
            &context.app.getRelationalDatabase());

        if (!db)
            Throw<std::runtime_error>("Failed to get relational database");

        return db->getAccountTx(args);
    }

    AccountTxResult result;

    auto lgrRange = getLedgerRange(context, args.ledger);
    if (auto stat = std::get_if<RPC::Status>(&lgrRange))
    {
        // An error occurred getting the requested ledger range
        return {result, *stat};
    }

    result.ledgerRange = std::get<LedgerRange>(lgrRange);

    result.marker = args.marker;

    RelationalDatabase::AccountTxPageOptions options = {
        args.account,
        result.ledgerRange.min,
        result.ledgerRange.max,
        result.marker,
        args.limit,
        isUnlimited(context.role)};

    auto const db =
        dynamic_cast<SQLiteDatabase*>(&context.app.getRelationalDatabase());

    if (!db)
        Throw<std::runtime_error>("Failed to get relational database");

    if (args.binary)
    {
        if (args.forward)
        {
            auto [tx, marker] = db->oldestAccountTxPageB(options);
            result.transactions = tx;
            result.marker = marker;
        }
        else
        {
            auto [tx, marker] = db->newestAccountTxPageB(options);
            result.transactions = tx;
            result.marker = marker;
        }
    }
    else
    {
        if (args.forward)
        {
            auto [tx, marker] = db->oldestAccountTxPage(options);
            result.transactions = tx;
            result.marker = marker;
        }
        else
        {
            auto [tx, marker] = db->newestAccountTxPage(options);
            result.transactions = tx;
            result.marker = marker;
        }
    }

    result.limit = args.limit;
    JLOG(context.j.debug()) << __func__ << " : finished";

    return {result, rpcSUCCESS};
}

boost::json::object
populateJsonResponse(
    std::pair<AccountTxResult, RPC::Status> const& res,
    AccountTxArgs const& args,
    RPC::JsonContext const& context)
{
    boost::json::object response;
    RPC::Status const& error = res.second;
    if (error.toErrorCode() != rpcSUCCESS)
    {
        error.inject(response);
    }
    else
    {
        AccountTxResult const& result = res.first;
        response[jss::validated.c_str()] = true;
        response[jss::limit.c_str()] = result.limit;
        response[jss::account.c_str()] = context.params.at(jss::account.c_str()).as_string();
        response[jss::ledger_index_min.c_str()] = result.ledgerRange.min;
        response[jss::ledger_index_max.c_str()] = result.ledgerRange.max;

        boost::json::array& jvTxns = (response[jss::transactions.c_str()].emplace_array());

        if (auto txnsData = std::get_if<TxnsData>(&result.transactions))
        {
            assert(!args.binary);
            for (auto const& [txn, txnMeta] : *txnsData)
            {
                if (txn)
                {
                    boost::json::object& jvObj = jvTxns.emplace_back(boost::json::object()).as_object();

                    jvObj[jss::tx.c_str()] = txn->getJson(JsonOptions::include_date);
                    if (txnMeta)
                    {
                        jvObj[jss::meta.c_str()] =
                            txnMeta->getJson(JsonOptions::include_date);
                        jvObj[jss::validated.c_str()] = true;
                        insertDeliveredAmount(
                            jvObj[jss::meta.c_str()].as_object(), context, txn, *txnMeta);
                    }
                }
            }
        }
        else
        {
            assert(args.binary);

            for (auto const& binaryData :
                 std::get<TxnsDataBinary>(result.transactions))
            {
                boost::json::object& jvObj = jvTxns.emplace_back(boost::json::object()).as_object();

                jvObj[jss::tx_blob.c_str()] = strHex(std::get<0>(binaryData));
                jvObj[jss::meta.c_str()] = strHex(std::get<1>(binaryData));
                jvObj[jss::ledger_index.c_str()] = std::get<2>(binaryData);
                jvObj[jss::validated.c_str()] = true;
            }
        }

        if (result.marker)
        {
            response[jss::marker.c_str()].emplace_object();
            response[jss::marker.c_str()].as_object()[jss::ledger.c_str()] = result.marker->ledgerSeq;
            response[jss::marker.c_str()].as_object()[jss::seq.c_str()] = result.marker->txnSeq;
        }
        if (context.app.config().reporting())
            response["used_postgres"] = true;
    }

    JLOG(context.j.debug()) << __func__ << " : finished";
    return response;
}

// {
//   account: account,
//   ledger_index_min: ledger_index  // optional, defaults to earliest
//   ledger_index_max: ledger_index, // optional, defaults to latest
//   binary: boolean,                // optional, defaults to false
//   forward: boolean,               // optional, defaults to false
//   limit: integer,                 // optional
//   marker: object {ledger: ledger_index, seq: txn_sequence} // optional,
//   resume previous query
// }
boost::json::object
doAccountTxJson(RPC::JsonContext& context)
{
    if (!context.app.config().useTxTables())
        return rpcError(rpcNOT_ENABLED);

    auto& params = context.params;
    AccountTxArgs args;
    boost::json::object response;

    args.limit = params.contains(jss::limit.c_str()) ? params.at(jss::limit.c_str()).as_uint64() : 0;
    args.binary = params.contains(jss::binary.c_str()) && params.at(jss::binary.c_str()).as_bool();
    args.forward =
        params.contains(jss::forward.c_str()) && params.at(jss::forward.c_str()).as_bool();

    if (!params.contains(jss::account.c_str()))
        return rpcError(rpcINVALID_PARAMS);

    auto const account =
        parseBase58<AccountID>(std::string{params.at(jss::account.c_str()).as_string()});
    if (!account)
        return rpcError(rpcACT_MALFORMED);

    args.account = *account;

    auto parseRes = parseLedgerArgs(params);
    if (auto jv = std::get_if<boost::json::object>(&parseRes))
    {
        return *jv;
    }
    else
    {
        args.ledger = std::get<std::optional<LedgerSpecifier>>(parseRes);
    }

    if (params.contains(jss::marker.c_str()))
    {
        auto& token = params.at(jss::marker.c_str()).as_object();
        if (!token.contains(jss::ledger.c_str()) || !token.contains(jss::seq.c_str()) ||
            !token[jss::ledger.c_str()].is_uint64() ||
            !token[jss::seq.c_str()].is_uint64())
        {
            RPC::Status status{
                rpcINVALID_PARAMS,
                "invalid marker. Provide ledger index via ledger field, and "
                "transaction sequence number via seq field"};
            status.inject(response);
            return response;
        }
        args.marker = {token[jss::ledger.c_str()].as_uint64(), token[jss::seq.c_str()].as_uint64()};
    }

    auto res = doAccountTxHelp(context, args);
    JLOG(context.j.debug()) << __func__ << " populating response";
    return populateJsonResponse(res, args, context);
}

}  // namespace ripple
