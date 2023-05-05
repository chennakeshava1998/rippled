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

#include <test/jtx/flags.h>
#include <test/jtx/token.h>

#include <ripple/app/tx/impl/NFTokenMint.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {
namespace token {

boost::json::object
mint(jtx::Account const& account, std::uint32_t nfTokenTaxon)
{
    boost::json::object jv;
    jv[sfAccount.jsonName.c_str()] = account.human();
    jv[sfNFTokenTaxon.jsonName.c_str()] = nfTokenTaxon;
    jv[sfTransactionType.jsonName.c_str()] = jss::NFTokenMint;
    return jv;
}

void
xferFee::operator()(Env& env, JTx& jt) const
{
    jt.jv.as_object()[sfTransferFee.jsonName.c_str()] = xferFee_;
}

void
issuer::operator()(Env& env, JTx& jt) const
{
    jt.jv.as_object()[sfIssuer.jsonName.c_str()] = issuer_;
}

void
uri::operator()(Env& env, JTx& jt) const
{
    jt.jv.as_object()[sfURI.jsonName.c_str()] = uri_;
}

uint256
getNextID(
    jtx::Env const& env,
    jtx::Account const& issuer,
    std::uint32_t nfTokenTaxon,
    std::uint16_t flags,
    std::uint16_t xferFee)
{
    // Get the nftSeq from the account root of the issuer.
    std::uint32_t const nftSeq = {
        env.le(issuer)->at(~sfMintedNFTokens).value_or(0)};
    return token::getID(env, issuer, nfTokenTaxon, nftSeq, flags, xferFee);
}

uint256
getID(
    jtx::Env const& env,
    jtx::Account const& issuer,
    std::uint32_t nfTokenTaxon,
    std::uint32_t nftSeq,
    std::uint16_t flags,
    std::uint16_t xferFee)
{
    if (env.current()->rules().enabled(fixNFTokenRemint))
    {
        // If fixNFTokenRemint is enabled, we must add issuer's
        // FirstNFTokenSequence to offset the starting NFT sequence number.
        nftSeq += env.le(issuer)
                      ->at(~sfFirstNFTokenSequence)
                      .value_or(env.seq(issuer));
    }
    return ripple::NFTokenMint::createNFTokenID(
        flags, xferFee, issuer, nft::toTaxon(nfTokenTaxon), nftSeq);
}

boost::json::object
burn(jtx::Account const& account, uint256 const& nftokenID)
{
    boost::json::object jv;
    jv[sfAccount.jsonName.c_str()] = account.human();
    jv[sfNFTokenID.jsonName.c_str()] = to_string(nftokenID);
    jv[jss::TransactionType.c_str()] = jss::NFTokenBurn;
    return jv;
}

boost::json::object
createOffer(
    jtx::Account const& account,
    uint256 const& nftokenID,
    STAmount const& amount)
{
    boost::json::object jv;
    jv[sfAccount.jsonName.c_str()] = account.human();
    jv[sfNFTokenID.jsonName.c_str()] = to_string(nftokenID);
    jv[sfAmount.jsonName.c_str()] = amount.getJson(JsonOptions::none);
    jv[jss::TransactionType.c_str()] = jss::NFTokenCreateOffer;
    return jv;
}

void
owner::operator()(Env& env, JTx& jt) const
{
    jt.jv.as_object()[sfOwner.jsonName.c_str()] = owner_;
}

void
expiration::operator()(Env& env, JTx& jt) const
{
    jt.jv.as_object()[sfExpiration.jsonName.c_str()] = expires_;
}

void
destination::operator()(Env& env, JTx& jt) const
{
    jt.jv.as_object()[sfDestination.jsonName.c_str()] = dest_;
}

template <typename T>
static boost::json::object
cancelOfferImpl(jtx::Account const& account, T const& nftokenOffers)
{
    boost::json::object jv;
    jv[sfAccount.jsonName.c_str()] = account.human();
    if (!empty(nftokenOffers))
    {
        jv[sfNFTokenOffers.jsonName.c_str()].emplace_array();
        for (uint256 const& nftokenOffer : nftokenOffers)
            jv[sfNFTokenOffers.jsonName.c_str()].as_array().emplace_back(to_string(nftokenOffer));
    }
    jv[jss::TransactionType.c_str()] = jss::NFTokenCancelOffer;
    return jv;
}

boost::json::object
cancelOffer(
    jtx::Account const& account,
    std::initializer_list<uint256> const& nftokenOffers)
{
    return cancelOfferImpl(account, nftokenOffers);
}

boost::json::object
cancelOffer(
    jtx::Account const& account,
    std::vector<uint256> const& nftokenOffers)
{
    return cancelOfferImpl(account, nftokenOffers);
}

void
rootIndex::operator()(Env& env, JTx& jt) const
{
    jt.jv.as_object()[sfRootIndex.jsonName.c_str()] = rootIndex_;
}

boost::json::object
acceptBuyOffer(jtx::Account const& account, uint256 const& offerIndex)
{
    boost::json::object jv;
    jv[sfAccount.jsonName.c_str()] = account.human();
    jv[sfNFTokenBuyOffer.jsonName.c_str()] = to_string(offerIndex);
    jv[jss::TransactionType.c_str()] = jss::NFTokenAcceptOffer;
    return jv;
}

boost::json::object
acceptSellOffer(jtx::Account const& account, uint256 const& offerIndex)
{
    boost::json::object jv;
    jv[sfAccount.jsonName.c_str()] = account.human();
    jv[sfNFTokenSellOffer.jsonName.c_str()] = to_string(offerIndex);
    jv[jss::TransactionType.c_str()] = jss::NFTokenAcceptOffer;
    return jv;
}

boost::json::object
brokerOffers(
    jtx::Account const& account,
    uint256 const& buyOfferIndex,
    uint256 const& sellOfferIndex)
{
    boost::json::object jv;
    jv[sfAccount.jsonName.c_str()] = account.human();
    jv[sfNFTokenBuyOffer.jsonName.c_str()] = to_string(buyOfferIndex);
    jv[sfNFTokenSellOffer.jsonName.c_str()] = to_string(sellOfferIndex);
    jv[jss::TransactionType.c_str()] = jss::NFTokenAcceptOffer;
    return jv;
}

void
brokerFee::operator()(Env& env, JTx& jt) const
{
    jt.jv.as_object()[sfNFTokenBrokerFee.jsonName.c_str()] = brokerFee_.getJson(JsonOptions::none);
}

boost::json::object
setMinter(jtx::Account const& account, jtx::Account const& minter)
{
    boost::json::object jt = fset(account, asfAuthorizedNFTokenMinter).as_object();
    jt[sfNFTokenMinter.fieldName.c_str()] = minter.human();
    return jt;
}

boost::json::value
clearMinter(jtx::Account const& account)
{
    return fclear(account, asfAuthorizedNFTokenMinter);
}

}  // namespace token
}  // namespace jtx
}  // namespace test
}  // namespace ripple
