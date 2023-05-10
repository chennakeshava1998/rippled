//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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
#include <ripple/json/json_value.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/ledger/View.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {

static void
appendNftOfferJson(
    Application const& app,
    std::shared_ptr<SLE const> const& offer,
    boost::json::array& offers)
{
    boost::json::object& obj(offers.emplace_back(boost::json::object()).as_object());

    obj[jss::nft_offer_index.c_str()] = to_string(offer->key());
    obj[jss::flags.c_str()] = (*offer)[sfFlags];
    obj[jss::owner.c_str()] = toBase58(offer->getAccountID(sfOwner));

    if (offer->isFieldPresent(sfDestination))
        obj[jss::destination.c_str()] = toBase58(offer->getAccountID(sfDestination));

    if (offer->isFieldPresent(sfExpiration))
        obj[jss::expiration.c_str()] = offer->getFieldU32(sfExpiration);

    offer->getFieldAmount(sfAmount).setJson(obj[jss::amount.c_str()]);
}

// {
//   nft_id: <token hash>
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   limit: integer                 // optional
//   marker: opaque                 // optional, resume previous query
// }
static boost::json::object
enumerateNFTOffers(
    RPC::JsonContext& context,
    uint256 const& nftId,
    Keylet const& directory)
{
    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::nftOffers, context))
        return *err;

    std::shared_ptr<ReadView const> ledger;

    if (auto result = RPC::lookupLedger(ledger, context); !ledger)
        return result;

    if (!ledger->exists(directory))
        return rpcError(rpcOBJECT_NOT_FOUND);

    boost::json::object result;
    result[jss::nft_id.c_str()] = to_string(nftId);

    boost::json::array& jsonOffers(result[jss::offers.c_str()].emplace_array());

    std::vector<std::shared_ptr<SLE const>> offers;
    unsigned int reserve(limit);
    uint256 startAfter;
    std::uint64_t startHint = 0;

    if (context.params.contains(jss::marker.c_str()))
    {
        // We have a start point. Use limit - 1 from the result and use the
        // very last one for the resume.
        boost::json::value const& marker(context.params[jss::marker.c_str()]);

        if (!marker.is_string())
            return RPC::expected_field_error(jss::marker, "string");

        if (!startAfter.parseHex(marker.as_string()))
            return rpcError(rpcINVALID_PARAMS);

        auto const sle = ledger->read(keylet::nftoffer(startAfter));

        if (!sle || nftId != sle->getFieldH256(sfNFTokenID))
            return rpcError(rpcINVALID_PARAMS);

        startHint = sle->getFieldU64(sfNFTokenOfferNode);
        appendNftOfferJson(context.app, sle, jsonOffers);
        offers.reserve(reserve);
    }
    else
    {
        // We have no start point, limit should be one higher than requested.
        offers.reserve(++reserve);
    }

    if (!forEachItemAfter(
            *ledger,
            directory,
            startAfter,
            startHint,
            reserve,
            [&offers](std::shared_ptr<SLE const> const& offer) {
                if (offer->getType() == ltNFTOKEN_OFFER)
                {
                    offers.emplace_back(offer);
                    return true;
                }

                return false;
            }))
    {
        return rpcError(rpcINVALID_PARAMS);
    }

    if (offers.size() == reserve)
    {
        result[jss::limit.c_str()] = limit;
        result[jss::marker.c_str()] = to_string(offers.back()->key());
        offers.pop_back();
    }

    for (auto const& offer : offers)
        appendNftOfferJson(context.app, offer, jsonOffers);

    context.loadType = Resource::feeMediumBurdenRPC;
    return result;
}

boost::json::object
doNFTSellOffers(RPC::JsonContext& context)
{
    if (!context.params.contains(jss::nft_id.c_str()))
        return RPC::missing_field_error(jss::nft_id);

    uint256 nftId;

    if (!nftId.parseHex(context.params[jss::nft_id.c_str()].as_string()))
        return RPC::invalid_field_error(jss::nft_id);

    return enumerateNFTOffers(context, nftId, keylet::nft_sells(nftId));
}

boost::json::object
doNFTBuyOffers(RPC::JsonContext& context)
{
    if (!context.params.contains(jss::nft_id.c_str()))
        return RPC::missing_field_error(jss::nft_id);

    uint256 nftId;

    if (!nftId.parseHex(context.params[jss::nft_id.c_str()].as_string()))
        return RPC::invalid_field_error(jss::nft_id);

    return enumerateNFTOffers(context, nftId, keylet::nft_buys(nftId));
}

}  // namespace ripple
