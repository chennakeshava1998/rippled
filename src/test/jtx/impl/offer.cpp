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

#include <ripple/protocol/jss.h>
#include <test/jtx/offer.h>

namespace ripple {
namespace test {
namespace jtx {

boost::json::object
offer(
    Account const& account,
    STAmount const& takerPays,
    STAmount const& takerGets,
    std::uint32_t flags)
{
    boost::json::object jv;
    jv[jss::Account.c_str()] = account.human();
    jv[jss::TakerPays.c_str()] = takerPays.getJson(JsonOptions::none);
    jv[jss::TakerGets.c_str()] = takerGets.getJson(JsonOptions::none);
    if (flags)
        jv[jss::Flags.c_str()] = flags;
    jv[jss::TransactionType.c_str()] = jss::OfferCreate;
    return jv;
}

boost::json::object
offer_cancel(Account const& account, std::uint32_t offerSeq)
{
    boost::json::object jv;
    jv[jss::Account.c_str()] = account.human();
    jv[jss::OfferSequence.c_str()] = offerSeq;
    jv[jss::TransactionType.c_str()] = jss::OfferCancel;
    return jv;
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
