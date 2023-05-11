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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/paths/TrustLine.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {

static void
fillTransaction(
    RPC::JsonContext& context,
    boost::json::object& txArray,
    AccountID const& accountID,
    std::uint32_t& sequence,
    ReadView const& ledger)
{
    txArray["Sequence"] = Json::UInt(sequence++);
    txArray["Account"] = toBase58(accountID);
    auto& fees = ledger.fees();
    // Convert the reference transaction cost in fee units to drops
    // scaled to represent the current fee load.
    txArray["Fee"] =
        scaleFeeLoad(fees.base, context.app.getFeeTrack(), fees, false)
            .jsonClipped();
}

// {
//   account: <account>|<account_public_key>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional, number of problems
//   role: gateway|user             // account role to assume
//   transactions: true             // optional, reccommend transactions
// }
boost::json::value
doNoRippleCheck(RPC::JsonContext& context)
{
    auto const& params(context.params);
    if (!params.contains(jss::account.c_str()))
        return RPC::missing_field_error("account");

    if (!params.contains("role"))
        return RPC::missing_field_error("role");
    bool roleGateway = false;
    {
        std::string const role = std::string{params.at("role").as_string()};
        if (role == "gateway")
            roleGateway = true;
        else if (role != "user")
            return RPC::invalid_field_error("role");
    }

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::noRippleCheck, context))
        return *err;

    bool transactions = false;
    if (params.contains(jss::transactions.c_str()))
        transactions = params.at("transactions").as_bool();

    std::shared_ptr<ReadView const> ledger;
    auto resultJson = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return resultJson;

    boost::json::object result = resultJson;

    boost::json::array dummy;
    boost::json::array& jvTransactions =
        transactions ? (result[jss::transactions.c_str()].emplace_array()) : dummy;

    std::string strIdent(params.at(jss::account.c_str()).as_string());
    AccountID accountID;

    auto jv = RPC::accountFromString(accountID, strIdent);
    if (!jv.empty())
    {
        return jv;
    }

    auto const sle = ledger->read(keylet::account(accountID));
    if (!sle)
        return rpcError(rpcACT_NOT_FOUND);

    std::uint32_t seq = sle->getFieldU32(sfSequence);

    boost::json::array& problems = (result["problems"].emplace_array());

    bool bDefaultRipple = sle->getFieldU32(sfFlags) & lsfDefaultRipple;

    if (bDefaultRipple & !roleGateway)
    {
        problems.emplace_back(
            "You appear to have set your default ripple flag even though you "
            "are not a gateway. This is not recommended unless you are "
            "experimenting");
    }
    else if (roleGateway & !bDefaultRipple)
    {
        problems.emplace_back("You should immediately set your default ripple flag");
        if (transactions)
        {
            boost::json::object& tx = jvTransactions.emplace_back(boost::json::object()).as_object();
            tx["TransactionType"] = jss::AccountSet;
            tx["SetFlag"] = 8;
            fillTransaction(context, tx, accountID, seq, *ledger);
        }
    }

    forEachItemAfter(
        *ledger,
        accountID,
        uint256(),
        0,
        limit,
        [&](std::shared_ptr<SLE const> const& ownedItem) {
            if (ownedItem->getType() == ltRIPPLE_STATE)
            {
                bool const bLow = accountID ==
                    ownedItem->getFieldAmount(sfLowLimit).getIssuer();

                bool const bNoRipple = ownedItem->getFieldU32(sfFlags) &
                    (bLow ? lsfLowNoRipple : lsfHighNoRipple);

                std::string problem;
                bool needFix = false;
                if (bNoRipple & roleGateway)
                {
                    problem = "You should clear the no ripple flag on your ";
                    needFix = true;
                }
                else if (!roleGateway & !bNoRipple)
                {
                    problem =
                        "You should probably set the no ripple flag on your ";
                    needFix = true;
                }
                if (needFix)
                {
                    AccountID peer =
                        ownedItem
                            ->getFieldAmount(bLow ? sfHighLimit : sfLowLimit)
                            .getIssuer();
                    STAmount peerLimit = ownedItem->getFieldAmount(
                        bLow ? sfHighLimit : sfLowLimit);
                    problem += to_string(peerLimit.getCurrency());
                    problem += " line to ";
                    problem += to_string(peerLimit.getIssuer());
                    problems.emplace_back(problem);

                    STAmount limitAmount(ownedItem->getFieldAmount(
                        bLow ? sfLowLimit : sfHighLimit));
                    limitAmount.setIssuer(peer);

                    boost::json::object& tx = jvTransactions.emplace_back(boost::json::object()).as_object();
                    tx["TransactionType"] = jss::TrustSet;
                    tx["LimitAmount"] = limitAmount.getJson(JsonOptions::none);
                    tx["Flags"] = bNoRipple ? tfClearNoRipple : tfSetNoRipple;
                    fillTransaction(context, tx, accountID, seq, *ledger);

                    return true;
                }
            }
            return false;
        });

    return result;
}

}  // namespace ripple
