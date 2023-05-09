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
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/strHex.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

// {
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   ...
// }
boost::json::object
doLedgerEntry(RPC::JsonContext& context)
{
    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    uint256 uNodeIndex;
    bool bNodeBinary = false;
    LedgerEntryType expectedType = ltANY;

    if (context.params.contains(jss::index.c_str()))
    {
        if (!uNodeIndex.parseHex(context.params[jss::index.c_str()].as_string()))
        {
            uNodeIndex = beast::zero;
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
    }
    else if (context.params.contains(jss::account_root.c_str()))
    {
        expectedType = ltACCOUNT_ROOT;
        auto const account = parseBase58<AccountID>(
            context.params[jss::account_root.c_str()].as_string().c_str());
        if (!account || account->isZero())
            jvResult[jss::error.c_str()] = "malformedAddress";
        else
            uNodeIndex = keylet::account(*account).key;
    }
    else if (context.params.contains(jss::check.c_str()))
    {
        expectedType = ltCHECK;

        if (!uNodeIndex.parseHex(context.params[jss::check.c_str()].as_string()))
        {
            uNodeIndex = beast::zero;
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
    }
    else if (context.params.contains(jss::deposit_preauth.c_str()))
    {
        expectedType = ltDEPOSIT_PREAUTH;

        if (!context.params[jss::deposit_preauth.c_str()].is_object())
        {
            if (!context.params[jss::deposit_preauth.c_str()].is_string() ||
                !uNodeIndex.parseHex(
                    context.params[jss::deposit_preauth.c_str()].as_string()))
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error.c_str()] = "malformedRequest";
            }
        }
        else if (
            !context.params[jss::deposit_preauth.c_str()].as_object().contains(jss::owner.c_str()) ||
            !context.params[jss::deposit_preauth.c_str()].as_object()[jss::owner.c_str()].is_string() ||
            !context.params[jss::deposit_preauth.c_str()].as_object().contains(jss::authorized.c_str()) ||
            !context.params[jss::deposit_preauth.c_str()].as_object()[jss::authorized.c_str()].is_string())
        {
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
        else
        {
            auto const owner = parseBase58<AccountID>(
                context.params[jss::deposit_preauth.c_str()].as_object()[jss::owner.c_str()].as_string().c_str());

            auto const authorized = parseBase58<AccountID>(
                context.params[jss::deposit_preauth.c_str()].as_object()[jss::authorized.c_str()]
                    .as_string().c_str());

            if (!owner)
                jvResult[jss::error.c_str()] = "malformedOwner";
            else if (!authorized)
                jvResult[jss::error.c_str()] = "malformedAuthorized";
            else
                uNodeIndex = keylet::depositPreauth(*owner, *authorized).key;
        }
    }
    else if (context.params.contains(jss::directory.c_str()))
    {
        expectedType = ltDIR_NODE;
        if (context.params[jss::directory.c_str()].is_null())
        {
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
        else if (!context.params[jss::directory.c_str()].is_object())
        {
            if (!uNodeIndex.parseHex(context.params[jss::directory.c_str()].as_string()))
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error.c_str()] = "malformedRequest";
            }
        }
        else if (
            context.params[jss::directory.c_str()].as_object().contains(jss::sub_index.c_str()) &&
            !context.params[jss::directory.c_str()].as_object()[jss::sub_index.c_str()].is_number())
        {
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
        else
        {
            std::uint64_t uSubIndex =
                context.params[jss::directory.c_str()].as_object().contains(jss::sub_index.c_str())
                ? context.params[jss::directory.c_str()].as_object()[jss::sub_index.c_str()].as_uint64()
                : 0;

            if (context.params[jss::directory.c_str()].as_object().contains(jss::dir_root.c_str()))
            {
                uint256 uDirRoot;

                if (context.params[jss::directory.c_str()].as_object().contains(jss::owner.c_str()))
                {
                    // May not specify both dir_root and owner.
                    jvResult[jss::error.c_str()] = "malformedRequest";
                }
                else if (!uDirRoot.parseHex(
                             context.params[jss::directory.c_str()].as_object()[jss::dir_root.c_str()]
                                 .as_string()))
                {
                    uNodeIndex = beast::zero;
                    jvResult[jss::error.c_str()] = "malformedRequest";
                }
                else
                {
                    uNodeIndex = keylet::page(uDirRoot, uSubIndex).key;
                }
            }
            else if (context.params[jss::directory.c_str()].as_object().contains(jss::owner.c_str()))
            {
                auto const ownerID = parseBase58<AccountID>(
                    context.params[jss::directory.c_str()].as_object()[jss::owner.c_str()].as_string().c_str());

                if (!ownerID)
                {
                    jvResult[jss::error.c_str()] = "malformedAddress";
                }
                else
                {
                    uNodeIndex =
                        keylet::page(keylet::ownerDir(*ownerID), uSubIndex).key;
                }
            }
            else
            {
                jvResult[jss::error.c_str()] = "malformedRequest";
            }
        }
    }
    else if (context.params.contains(jss::escrow.c_str()))
    {
        expectedType = ltESCROW;
        if (!context.params[jss::escrow.c_str()].is_object())
        {
            if (!uNodeIndex.parseHex(context.params[jss::escrow.c_str()].as_string()))
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error.c_str()] = "malformedRequest";
            }
        }
        else if (
            !context.params[jss::escrow.c_str()].as_object().contains(jss::owner.c_str()) ||
            !context.params[jss::escrow.c_str()].as_object().contains(jss::seq.c_str()) ||
            !context.params[jss::escrow.c_str()].as_object()[jss::seq.c_str()].is_number())
        {
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
        else
        {
            auto const id = parseBase58<AccountID>(
                context.params[jss::escrow.c_str()].as_object()[jss::owner.c_str()].as_string().c_str());
            if (!id)
                jvResult[jss::error.c_str()] = "malformedOwner";
            else
                uNodeIndex =
                    keylet::escrow(
                        *id, context.params[jss::escrow.c_str()].as_object()[jss::seq.c_str()].as_uint64())
                        .key;
        }
    }
    else if (context.params.contains(jss::offer.c_str()))
    {
        expectedType = ltOFFER;
        if (!context.params[jss::offer.c_str()].is_object())
        {
            if (!uNodeIndex.parseHex(context.params[jss::offer.c_str()].as_string()))
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error.c_str()] = "malformedRequest";
            }
        }
        else if (
            !context.params[jss::offer.c_str()].as_object().contains(jss::account.c_str()) ||
            !context.params[jss::offer.c_str()].as_object().contains(jss::seq.c_str()) ||
            !context.params[jss::offer.c_str()].as_object()[jss::seq.c_str()].is_number())
        {
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
        else
        {
            auto const id = parseBase58<AccountID>(
                context.params[jss::offer.c_str()].as_object()[jss::account.c_str()].as_string().c_str());
            if (!id)
                jvResult[jss::error.c_str()] = "malformedAddress";
            else
                uNodeIndex =
                    keylet::offer(
                        *id, context.params[jss::offer.c_str()].as_object()[jss::seq.c_str()].as_uint64())
                        .key;
        }
    }
    else if (context.params.contains(jss::payment_channel.c_str()))
    {
        expectedType = ltPAYCHAN;

        if (!uNodeIndex.parseHex(
                context.params[jss::payment_channel.c_str()].as_string()))
        {
            uNodeIndex = beast::zero;
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
    }
    else if (context.params.contains(jss::ripple_state.c_str()))
    {
        expectedType = ltRIPPLE_STATE;
        Currency uCurrency;
        boost::json::value jvRippleState = context.params[jss::ripple_state.c_str()];

        if (!jvRippleState.is_object() ||
            !jvRippleState.as_object().contains(jss::currency.c_str()) ||
            !jvRippleState.as_object().contains(jss::accounts.c_str()) ||
            !jvRippleState.as_object()[jss::accounts.c_str()].is_array() ||
            2 != jvRippleState.as_object()[jss::accounts.c_str()].as_array().size() ||
            !jvRippleState.as_object()[jss::accounts.c_str()].as_array()[0u].is_string() ||
            !jvRippleState.as_object()[jss::accounts.c_str()].as_array()[1u].is_string() ||
            (jvRippleState.as_object()[jss::accounts.c_str()].as_array()[0u].as_string() ==
             jvRippleState.as_object()[jss::accounts.c_str()].as_array()[1u].as_string()))
        {
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
        else
        {
            auto const id1 = parseBase58<AccountID>(
                jvRippleState.as_object()[jss::accounts.c_str()].as_array()[0u].as_string().c_str());
            auto const id2 = parseBase58<AccountID>(
                jvRippleState.as_object()[jss::accounts.c_str()].as_array()[1u].as_string().c_str());
            if (!id1 || !id2)
            {
                jvResult[jss::error.c_str()] = "malformedAddress";
            }
            else if (!to_currency(
                         uCurrency, jvRippleState.as_object()[jss::currency.c_str()].as_string().c_str()))
            {
                jvResult[jss::error.c_str()] = "malformedCurrency";
            }
            else
            {
                uNodeIndex = keylet::line(*id1, *id2, uCurrency).key;
            }
        }
    }
    else if (context.params.contains(jss::ticket.c_str()))
    {
        expectedType = ltTICKET;
        if (!context.params[jss::ticket.c_str()].is_object())
        {
            if (!uNodeIndex.parseHex(context.params[jss::ticket.c_str()].as_string()))
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error.c_str()] = "malformedRequest";
            }
        }
        else if (
            !context.params[jss::ticket.c_str()].as_object().contains(jss::account.c_str()) ||
            !context.params[jss::ticket.c_str()].as_object().contains(jss::ticket_seq.c_str()) ||
            !context.params[jss::ticket.c_str()].as_object()[jss::ticket_seq.c_str()].is_number())
        {
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
        else
        {
            auto const id = parseBase58<AccountID>(
                context.params[jss::ticket.c_str()].as_object()[jss::account.c_str()].as_string().c_str());
            if (!id)
                jvResult[jss::error.c_str()] = "malformedAddress";
            else
                uNodeIndex = getTicketIndex(
                    *id, context.params[jss::ticket.c_str()].as_object()[jss::ticket_seq.c_str()].as_uint64());
        }
    }
    else if (context.params.contains(jss::nft_page.c_str()))
    {
        expectedType = ltNFTOKEN_PAGE;

        if (context.params[jss::nft_page.c_str()].is_string())
        {
            if (!uNodeIndex.parseHex(context.params[jss::nft_page.c_str()].as_string()))
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error.c_str()] = "malformedRequest";
            }
        }
        else
        {
            jvResult[jss::error.c_str()] = "malformedRequest";
        }
    }
    else
    {
        if (context.params.contains("params") &&
            context.params["params"].is_array() &&
            context.params["params"].as_array().size() == 1 &&
            context.params["params"].as_array()[0u].is_string())
        {
            if (!uNodeIndex.parseHex(context.params["params"].as_array()[0u].as_string()))
            {
                uNodeIndex = beast::zero;
                jvResult[jss::error.c_str()] = "malformedRequest";
            }
        }
        else
            jvResult[jss::error.c_str()] = "unknownOption";
    }

    if (uNodeIndex.isNonZero())
    {
        auto const sleNode = lpLedger->read(keylet::unchecked(uNodeIndex));
        if (context.params.contains(jss::binary.c_str()))
            bNodeBinary = context.params[jss::binary.c_str()].as_bool();

        if (!sleNode)
        {
            // Not found.
            jvResult[jss::error.c_str()] = "entryNotFound";
        }
        else if (
            (expectedType != ltANY) && (expectedType != sleNode->getType()))
        {
            jvResult[jss::error.c_str()] = "unexpectedLedgerType";
        }
        else if (bNodeBinary)
        {
            Serializer s;

            sleNode->add(s);

            jvResult[jss::node_binary.c_str()] = strHex(s.peekData());
            jvResult[jss::index.c_str()] = to_string(uNodeIndex);
        }
        else
        {
            jvResult[jss::node.c_str()] = sleNode->getJson(JsonOptions::none);
            jvResult[jss::index.c_str()] = to_string(uNodeIndex);
        }
    }

    return jvResult;
}

std::pair<org::xrpl::rpc::v1::GetLedgerEntryResponse, grpc::Status>
doLedgerEntryGrpc(
    RPC::GRPCContext<org::xrpl::rpc::v1::GetLedgerEntryRequest>& context)
{
    org::xrpl::rpc::v1::GetLedgerEntryRequest& request = context.params;
    org::xrpl::rpc::v1::GetLedgerEntryResponse response;
    grpc::Status status = grpc::Status::OK;

    std::shared_ptr<ReadView const> ledger;
    if (auto status = RPC::ledgerFromRequest(ledger, context))
    {
        grpc::Status errorStatus;
        if (status.toErrorCode() == rpcINVALID_PARAMS)
        {
            errorStatus = grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT, status.message());
        }
        else
        {
            errorStatus =
                grpc::Status(grpc::StatusCode::NOT_FOUND, status.message());
        }
        return {response, errorStatus};
    }

    auto key = uint256::fromVoidChecked(request.key());
    if (!key)
    {
        grpc::Status errorStatus{
            grpc::StatusCode::INVALID_ARGUMENT, "index malformed"};
        return {response, errorStatus};
    }

    auto const sleNode = ledger->read(keylet::unchecked(*key));
    if (!sleNode)
    {
        grpc::Status errorStatus{
            grpc::StatusCode::NOT_FOUND, "object not found"};
        return {response, errorStatus};
    }
    else
    {
        Serializer s;
        sleNode->add(s);

        auto& stateObject = *response.mutable_ledger_object();
        stateObject.set_data(s.peekData().data(), s.getLength());
        stateObject.set_key(request.key());
        *(response.mutable_ledger()) = request.ledger();
        return {response, status};
    }
}
}  // namespace ripple
