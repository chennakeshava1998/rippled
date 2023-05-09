//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <ripple/app/ledger/LedgerToJson.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/base_uint.h>
#include <ripple/core/Pg.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/DeliveredAmount.h>

namespace ripple {

namespace {

bool
isFull(LedgerFill const& fill)
{
    return fill.options & LedgerFill::full;
}

bool
isExpanded(LedgerFill const& fill)
{
    return isFull(fill) || (fill.options & LedgerFill::expand);
}

bool
isBinary(LedgerFill const& fill)
{
    return fill.options & LedgerFill::binary;
}

template <class Object>
void
fillJson(Object& json, bool closed, LedgerInfo const& info, bool bFull)
{
    json.as_object()[jss::parent_hash.c_str()] = to_string(info.parentHash);
    json.as_object()[jss::ledger_index.c_str()] = to_string(info.seq);
    json.as_object()[jss::seqNum.c_str()] = to_string(info.seq);  // DEPRECATED

    if (closed)
    {
        json.as_object()[jss::closed.c_str()] = true;
    }
    else if (!bFull)
    {
        json.as_object()[jss::closed.c_str()] = false;
        return;
    }

    json.as_object()[jss::ledger_hash.c_str()] = to_string(info.hash);
    json.as_object()[jss::transaction_hash.c_str()] = to_string(info.txHash);
    json.as_object()[jss::account_hash.c_str()] = to_string(info.accountHash);
    json.as_object()[jss::total_coins.c_str()] = to_string(info.drops);

    // These next three are DEPRECATED.
    json.as_object()[jss::hash.c_str()] = to_string(info.hash);
    json.as_object()[jss::totalCoins.c_str()] = to_string(info.drops);
    json.as_object()[jss::accepted.c_str()] = closed;
    json.as_object()[jss::close_flags.c_str()] = info.closeFlags;

    // Always show fields that contribute to the ledger hash
    json.as_object()[jss::parent_close_time.c_str()] =
        info.parentCloseTime.time_since_epoch().count();
    json.as_object()[jss::close_time.c_str()] = info.closeTime.time_since_epoch().count();
    json.as_object()[jss::close_time_resolution.c_str()] = info.closeTimeResolution.count();

    if (info.closeTime != NetClock::time_point{})
    {
        json.as_object()[jss::close_time_human.c_str()] = to_string(info.closeTime);
        if (!getCloseAgree(info))
            json.as_object()[jss::close_time_estimated.c_str()] = true;
    }
}

// Keshava: This template might be instantiated for non-boost::json::values. That will cause trouble
template <class Object>
void
fillJsonBinary(Object& json, bool closed, LedgerInfo const& info)
{
    if (!closed)
        json.as_object()[jss::closed.c_str()] = false;
    else
    {
        json.as_object()[jss::closed.c_str()] = true;

        Serializer s;
        addRaw(info, s);
        json.as_object()[jss::ledger_data.c_str()] = strHex(s.peekData());
    }
}

boost::json::value
fillJsonTx(
    LedgerFill const& fill,
    bool bBinary,
    bool bExpanded,
    std::shared_ptr<STTx const> const& txn,
    std::shared_ptr<STObject const> const& stMeta)
{
    if (!bExpanded)
        return boost::json::string(to_string(txn->getTransactionID()));

    boost::json::value txJson;
    auto const txnType = txn->getTxnType();
    if (bBinary)
    {
        txJson.as_object()[jss::tx_blob.c_str()] = serializeHex(*txn);
        if (stMeta)
            txJson.as_object()[jss::meta.c_str()] = serializeHex(*stMeta);
    }
    else
    {
        txJson = txn->getJson(JsonOptions::none);
        if (stMeta)
        {
            txJson.as_object()[jss::metaData.c_str()] = stMeta->getJson(JsonOptions::none);

            // If applicable, insert delivered amount
            if (txnType == ttPAYMENT || txnType == ttCHECK_CASH)
                RPC::insertDeliveredAmount(
                    txJson.as_object()[jss::metaData.c_str()].as_object(),
                    fill.ledger,
                    txn,
                    {txn->getTransactionID(), fill.ledger.seq(), *stMeta});
        }
    }

    if ((fill.options & LedgerFill::ownerFunds) &&
        txn->getTxnType() == ttOFFER_CREATE)
    {
        auto const account = txn->getAccountID(sfAccount);
        auto const amount = txn->getFieldAmount(sfTakerGets);

        // If the offer create is not self funded then add the
        // owner balance
        if (account != amount.getIssuer())
        {
            auto const ownerFunds = accountFunds(
                fill.ledger,
                account,
                amount,
                fhIGNORE_FREEZE,
                beast::Journal{beast::Journal::getNullSink()});
            txJson.as_object()[jss::owner_funds.c_str()] = ownerFunds.getText();
        }
    }

    return txJson;
}

template <class Object>
void
fillJsonTx(Object& json, LedgerFill const& fill)
{
    boost::json::array& txns = json.as_object()[jss::transactions.c_str()].emplace_array();
    auto bBinary = isBinary(fill);
    auto bExpanded = isExpanded(fill);

    try
    {
        auto appendAll = [&](auto const& txs) {
            for (auto& i : txs)
            {
                txns.emplace_back(
                    fillJsonTx(fill, bBinary, bExpanded, i.first, i.second));
            }
        };

        if (fill.context && fill.context->app.config().reporting())
        {
            appendAll(flatFetchTransactions(fill.ledger, fill.context->app));
        }
        else
        {
            appendAll(fill.ledger.txs);
        }
    }
    catch (std::exception const& ex)
    {
        // Nothing the user can do about this.
        if (fill.context)
        {
            JLOG(fill.context->j.error())
                << "Exception in " << __func__ << ": " << ex.what();
        }
    }
}

template <class Object>
void
fillJsonState(Object& json, LedgerFill const& fill)
{
    auto& ledger = fill.ledger;
    boost::json::array& array = json.as_object()[jss::accountState.c_str()].emplace_array();
    auto expanded = isExpanded(fill);
    auto binary = isBinary(fill);

    for (auto const& sle : ledger.sles)
    {
        if (fill.type == ltANY || sle->getType() == fill.type)
        {
            if (binary)
            {
                boost::json::object& obj = array.emplace_back(boost::json::object()).as_object();
                obj[jss::hash.c_str()] = to_string(sle->key());
                obj[jss::tx_blob.c_str()] = serializeHex(*sle);
            }
            else if (expanded)
                array.emplace_back(sle->getJson(JsonOptions::none));
            else
                array.emplace_back(to_string(sle->key()));
        }
    }
}

template <class Object>
void
fillJsonQueue(Object& json, LedgerFill const& fill)
{
    boost::json::array& queueData = json[jss::queue_data.c_str()].emplace_array();
    auto bBinary = isBinary(fill);
    auto bExpanded = isExpanded(fill);

    for (auto const& tx : fill.txQueue)
    {

        boost::json::object& txJson = queueData.emplace_back(boost::json::object()).as_object();
        txJson[jss::fee_level.c_str()] = to_string(tx.feeLevel);
        if (tx.lastValid)
            txJson[jss::LastLedgerSequence.c_str()] = *tx.lastValid;

        txJson[jss::fee.c_str()] = to_string(tx.consequences.fee());
        auto const spend =
            tx.consequences.potentialSpend() + tx.consequences.fee();
        txJson[jss::max_spend_drops.c_str()] = to_string(spend);
        txJson[jss::auth_change.c_str()] = tx.consequences.isBlocker();

        txJson[jss::account.c_str()] = to_string(tx.account);
        txJson["retries_remaining"] = tx.retriesRemaining;
        txJson["preflight_result"] = transToken(tx.preflightResult);
        if (tx.lastResult)
            txJson["last_result"] = transToken(*tx.lastResult);

        txJson[jss::tx.c_str()] = fillJsonTx(fill, bBinary, bExpanded, tx.txn, nullptr);
    }
}

template <class Object>
void
fillJson(Object& json, LedgerFill const& fill)
{
    // TODO: what happens if bBinary and bExtracted are both set?
    // Is there a way to report this back?
    auto bFull = isFull(fill);
    if (isBinary(fill))
        fillJsonBinary(json, !fill.ledger.open(), fill.ledger.info());
    else
        fillJson(json, !fill.ledger.open(), fill.ledger.info(), bFull);

    if (bFull || fill.options & LedgerFill::dumpTxrp)
        fillJsonTx(json, fill);

    if (bFull || fill.options & LedgerFill::dumpState)
        fillJsonState(json, fill);
}

}  // namespace

void
addJson(boost::json::object& json, LedgerFill const& fill)
{
    json[jss::ledger.c_str()].emplace_object();
    auto&& object = json[jss::ledger.c_str()];
    fillJson(object, fill);

    if ((fill.options & LedgerFill::dumpQueue) && !fill.txQueue.empty())
        fillJsonQueue(json, fill);
}

boost::json::value
getJson(LedgerFill const& fill)
{
    boost::json::value json;
    fillJson(json, fill);
    return json;
}

}  // namespace ripple
