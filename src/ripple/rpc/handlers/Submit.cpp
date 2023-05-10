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
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/Transaction.h>
#include <ripple/app/tx/apply.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/TransactionSign.h>

namespace ripple {

static NetworkOPs::FailHard
getFailHard(RPC::JsonContext const& context)
{
    return NetworkOPs::doFailHard(
        context.params.contains("fail_hard") &&
        context.params.at("fail_hard").as_bool());
}

// {
//   tx_json: <object>,
//   secret: <secret>
// }
boost::json::object
doSubmit(RPC::JsonContext& context)
{
    context.loadType = Resource::feeMediumBurdenRPC;

    if (!context.params.contains(jss::tx_blob.c_str()))
    {
        auto const failType = getFailHard(context);

        if (context.role != Role::ADMIN && !context.app.config().canSign())
            return RPC::make_error(
                rpcNOT_SUPPORTED, "Signing is not supported by this server.");

        auto ret = RPC::transactionSubmit(
            context.params,
            failType,
            context.role,
            context.ledgerMaster.getValidatedLedgerAge(),
            context.app,
            RPC::getProcessTxnFn(context.netOps));

        ret[jss::deprecated.c_str()] =
            "Signing support in the 'submit' command has been "
            "deprecated and will be removed in a future version "
            "of the server. Please migrate to a standalone "
            "signing tool.";

        return ret;
    }

    boost::json::object jvResult;

    auto ret = strUnHex(context.params[jss::tx_blob.c_str()].as_string().c_str());

    if (!ret || !ret->size())
        return rpcError(rpcINVALID_PARAMS);

    SerialIter sitTrans(makeSlice(*ret));

    std::shared_ptr<STTx const> stpTrans;

    try
    {
        stpTrans = std::make_shared<STTx const>(std::ref(sitTrans));
    }
    catch (std::exception& e)
    {
        jvResult[jss::error.c_str()] = "invalidTransaction";
        jvResult[jss::error_exception.c_str()] = e.what();

        return jvResult;
    }

    {
        if (!context.app.checkSigs())
            forceValidity(
                context.app.getHashRouter(),
                stpTrans->getTransactionID(),
                Validity::SigGoodOnly);
        auto [validity, reason] = checkValidity(
            context.app.getHashRouter(),
            *stpTrans,
            context.ledgerMaster.getCurrentLedger()->rules(),
            context.app.config());
        if (validity != Validity::Valid)
        {
            jvResult[jss::error.c_str()] = "invalidTransaction";
            jvResult[jss::error_exception.c_str()] = "fails local checks: " + reason;

            return jvResult;
        }
    }

    std::string reason;
    auto tpTrans = std::make_shared<Transaction>(stpTrans, reason, context.app);
    if (tpTrans->getStatus() != NEW)
    {
        jvResult[jss::error.c_str()] = "invalidTransaction";
        jvResult[jss::error_exception.c_str()] = "fails local checks: " + reason;

        return jvResult;
    }

    try
    {
        auto const failType = getFailHard(context);

        context.netOps.processTransaction(
            tpTrans, isUnlimited(context.role), true, failType);
    }
    catch (std::exception& e)
    {
        jvResult[jss::error.c_str()] = "internalSubmit";
        jvResult[jss::error_exception.c_str()] = e.what();

        return jvResult;
    }

    try
    {
        jvResult[jss::tx_json.c_str()] = tpTrans->getJson(JsonOptions::none);
        jvResult[jss::tx_blob.c_str()] =
            strHex(tpTrans->getSTransaction()->getSerializer().peekData());

        if (temUNCERTAIN != tpTrans->getResult())
        {
            std::string sToken;
            std::string sHuman;

            transResultInfo(tpTrans->getResult(), sToken, sHuman);

            jvResult[jss::engine_result.c_str()] = sToken;
            jvResult[jss::engine_result_code.c_str()] = tpTrans->getResult();
            jvResult[jss::engine_result_message.c_str()] = sHuman;

            auto const submitResult = tpTrans->getSubmitResult();

            jvResult[jss::accepted.c_str()] = submitResult.any();
            jvResult[jss::applied.c_str()] = submitResult.applied;
            jvResult[jss::broadcast.c_str()] = submitResult.broadcast;
            jvResult[jss::queued.c_str()] = submitResult.queued;
            jvResult[jss::kept.c_str()] = submitResult.kept;

            if (auto currentLedgerState = tpTrans->getCurrentLedgerState())
            {
                jvResult[jss::account_sequence_next.c_str()] =
                    safe_cast<unsigned int>(
                        currentLedgerState->accountSeqNext);
                jvResult[jss::account_sequence_available.c_str()] =
                    safe_cast<unsigned int>(
                        currentLedgerState->accountSeqAvail);
                jvResult[jss::open_ledger_cost.c_str()] =
                    to_string(currentLedgerState->minFeeRequired);
                jvResult[jss::validated_ledger_index.c_str()] =
                    safe_cast<unsigned int>(
                        currentLedgerState->validatedLedger);
            }
        }

        return jvResult;
    }
    catch (std::exception& e)
    {
        jvResult[jss::error.c_str()] = "internalJson";
        jvResult[jss::error_exception.c_str()] = e.what();

        return jvResult;
    }
}

}  // namespace ripple
