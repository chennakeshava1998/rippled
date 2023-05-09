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
#include <ripple/app/misc/TxQ.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <grpc/status.h>

namespace ripple {

// {
//   account: <ident>,
//   strict: <bool>        // optional (default false)
//                         //   if true only allow public keys and addresses.
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   signer_lists : <bool> // optional (default false)
//                         //   if true return SignerList(s).
//   queue : <bool>        // optional (default false)
//                         //   if true return information about transactions
//                         //   in the current TxQ, only if the requested
//                         //   ledger is open. Otherwise if true, returns an
//                         //   error.
// }

// TODO(tom): what is that "default"?
boost::json::object
doAccountInfo(RPC::JsonContext& context)
{
    auto& params = context.params;

    std::string strIdent;
    if (params.contains(jss::account.c_str()))
        strIdent = params[jss::account.c_str()].as_string();
    else if (params.contains(jss::ident.c_str()))
        strIdent = params[jss::ident.c_str()].as_string();
    else
        return RPC::missing_field_error(jss::account);

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    bool bStrict = params.contains(jss::strict.c_str()) && params[jss::strict.c_str()].as_bool();
    AccountID accountID;

    // Get info on account.

    boost::json::object jvAccepted = RPC::accountFromString(accountID, strIdent, bStrict);

    if (!jvAccepted.empty())
        return jvAccepted;

    static constexpr std::
        array<std::pair<std::string_view, LedgerSpecificFlags>, 9>
            lsFlags{
                {{"defaultRipple", lsfDefaultRipple},
                 {"depositAuth", lsfDepositAuth},
                 {"disableMasterKey", lsfDisableMaster},
                 {"disallowIncomingXRP", lsfDisallowXRP},
                 {"globalFreeze", lsfGlobalFreeze},
                 {"noFreeze", lsfNoFreeze},
                 {"passwordSpent", lsfPasswordSpent},
                 {"requireAuthorization", lsfRequireAuth},
                 {"requireDestinationTag", lsfRequireDestTag}}};

    static constexpr std::
        array<std::pair<std::string_view, LedgerSpecificFlags>, 4>
            disallowIncomingFlags{
                {{"disallowIncomingNFTokenOffer",
                  lsfDisallowIncomingNFTokenOffer},
                 {"disallowIncomingCheck", lsfDisallowIncomingCheck},
                 {"disallowIncomingPayChan", lsfDisallowIncomingPayChan},
                 {"disallowIncomingTrustline", lsfDisallowIncomingTrustline}}};

    auto const sleAccepted = ledger->read(keylet::account(accountID));
    if (sleAccepted)
    {
        auto const queue =
            params.contains(jss::queue.c_str()) && params[jss::queue.c_str()].as_bool();

        if (queue && !ledger->open())
        {
            // It doesn't make sense to request the queue
            // with any closed or validated ledger.
            RPC::inject_error(rpcINVALID_PARAMS, result);
            return result;
        }

        RPC::injectSLE(jvAccepted, *sleAccepted);
        result[jss::account_data.c_str()] = jvAccepted;

        boost::json::object acctFlags;
        for (auto const& lsf : lsFlags)
            acctFlags[lsf.first.data()] = sleAccepted->isFlag(lsf.second);

        if (ledger->rules().enabled(featureDisallowIncoming))
        {
            for (auto const& lsf : disallowIncomingFlags)
                acctFlags[lsf.first.data()] = sleAccepted->isFlag(lsf.second);
        }
        result[jss::account_flags.c_str()] = std::move(acctFlags);

        // Return SignerList(s) if that is requested.
        if (params.contains(jss::signer_lists.c_str()) &&
            params[jss::signer_lists.c_str()].as_bool())
        {
            // We put the SignerList in an array because of an anticipated
            // future when we support multiple signer lists on one account.
            boost::json::array jvSignerList;

            // This code will need to be revisited if in the future we support
            // multiple SignerLists on one account.
            auto const sleSigners = ledger->read(keylet::signers(accountID));
            if (sleSigners)
                jvSignerList.emplace_back(sleSigners->getJson(JsonOptions::none));

            // Documentation states this is returned as part of the account_info
            // response, but previously the code put it under account_data. We
            // can move this to the documentated location from apiVersion 2
            // onwards.
            if (context.apiVersion == 1)
            {
                result[jss::account_data.c_str()].as_object()[jss::signer_lists.c_str()] =
                    std::move(jvSignerList);
            }
            else
            {
                result[jss::signer_lists.c_str()] = std::move(jvSignerList);
            }
        }
        // Return queue info if that is requested
        if (queue)
        {
            boost::json::object jvQueueData;

            auto const txs = context.app.getTxQ().getAccountTxs(accountID);
            if (!txs.empty())
            {
                jvQueueData[jss::txn_count.c_str()] =
                    static_cast<std::uint64_t>(txs.size());

                boost::json::array& jvQueueTx = jvQueueData[jss::transactions.c_str()].emplace_array();

                std::uint32_t seqCount = 0;
                std::uint32_t ticketCount = 0;
                std::optional<std::uint32_t> lowestSeq;
                std::optional<std::uint32_t> highestSeq;
                std::optional<std::uint32_t> lowestTicket;
                std::optional<std::uint32_t> highestTicket;
                bool anyAuthChanged = false;
                XRPAmount totalSpend(0);

                // We expect txs to be returned sorted by SeqProxy.  Verify
                // that with a couple of asserts.
                SeqProxy prevSeqProxy = SeqProxy::sequence(0);
                for (auto const& tx : txs)
                {
                    boost::json::object jvTx;

                    if (tx.seqProxy.isSeq())
                    {
                        assert(prevSeqProxy < tx.seqProxy);
                        prevSeqProxy = tx.seqProxy;
                        jvTx[jss::seq.c_str()] = tx.seqProxy.value();
                        ++seqCount;
                        if (!lowestSeq)
                            lowestSeq = tx.seqProxy.value();
                        highestSeq = tx.seqProxy.value();
                    }
                    else
                    {
                        assert(prevSeqProxy < tx.seqProxy);
                        prevSeqProxy = tx.seqProxy;
                        jvTx[jss::ticket.c_str()] = tx.seqProxy.value();
                        ++ticketCount;
                        if (!lowestTicket)
                            lowestTicket = tx.seqProxy.value();
                        highestTicket = tx.seqProxy.value();
                    }

                    jvTx[jss::fee_level.c_str()] = to_string(tx.feeLevel);
                    if (tx.lastValid)
                        jvTx[jss::LastLedgerSequence.c_str()] = *tx.lastValid;

                    jvTx[jss::fee.c_str()] = to_string(tx.consequences.fee());
                    auto const spend = tx.consequences.potentialSpend() +
                        tx.consequences.fee();
                    jvTx[jss::max_spend_drops.c_str()] = to_string(spend);
                    totalSpend += spend;
                    bool const authChanged = tx.consequences.isBlocker();
                    if (authChanged)
                        anyAuthChanged = authChanged;
                    jvTx[jss::auth_change.c_str()] = authChanged;

                    jvQueueTx.emplace_back(std::move(jvTx));
                }

                if (seqCount)
                    jvQueueData[jss::sequence_count.c_str()] = seqCount;
                if (ticketCount)
                    jvQueueData[jss::ticket_count.c_str()] = ticketCount;
                if (lowestSeq)
                    jvQueueData[jss::lowest_sequence.c_str()] = *lowestSeq;
                if (highestSeq)
                    jvQueueData[jss::highest_sequence.c_str()] = *highestSeq;
                if (lowestTicket)
                    jvQueueData[jss::lowest_ticket.c_str()] = *lowestTicket;
                if (highestTicket)
                    jvQueueData[jss::highest_ticket.c_str()] = *highestTicket;

                jvQueueData[jss::auth_change_queued.c_str()] = anyAuthChanged;
                jvQueueData[jss::max_spend_drops_total.c_str()] = to_string(totalSpend);
            }
            else
                jvQueueData[jss::txn_count.c_str()] = 0u;

            result[jss::queue_data.c_str()] = std::move(jvQueueData);
        }
    }
    else
    {
        result[jss::account.c_str()] = toBase58(accountID);
        RPC::inject_error(rpcACT_NOT_FOUND, result);
    }

    return result;
}

}  // namespace ripple
