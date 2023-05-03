//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/jtx/check.h>

namespace ripple {
namespace test {
namespace jtx {

namespace check {

// Create a check.
boost::json::value
create(
    jtx::Account const& account,
    jtx::Account const& dest,
    STAmount const& sendMax)
{
    boost::json::object jv;
    jv[std::string{sfAccount.jsonName}] = account.human();
    jv[std::string{sfSendMax.jsonName}] = sendMax.getJson(JsonOptions::none);
    jv[std::string{sfDestination.jsonName}] = dest.human();
    jv[std::string{sfTransactionType.jsonName}] = jss::CheckCreate;
    jv[std::string{sfFlags.jsonName}] = tfUniversal;
    return jv;
}

// Cash a check requiring that a specific amount be delivered.
boost::json::value
cash(jtx::Account const& dest, uint256 const& checkId, STAmount const& amount)
{
    boost::json::object jv;
    jv[std::string{sfAccount.jsonName}] = dest.human();
    jv[std::string{sfAmount.jsonName}] = amount.getJson(JsonOptions::none);
    jv[std::string{sfCheckID.jsonName}] = to_string(checkId);
    jv[std::string{sfTransactionType.jsonName}] = jss::CheckCash;
    jv[std::string{sfFlags.jsonName}] = tfUniversal;
    return jv;
}

// Cash a check requiring that at least a minimum amount be delivered.
boost::json::value
cash(
    jtx::Account const& dest,
    uint256 const& checkId,
    DeliverMin const& atLeast)
{
    boost::json::object jv;
    jv[std::string{sfAccount.jsonName}] = dest.human();
    jv[std::string{sfDeliverMin.jsonName}] = atLeast.value.getJson(JsonOptions::none);
    jv[std::string{sfCheckID.jsonName}] = to_string(checkId);
    jv[std::string{sfTransactionType.jsonName}] = jss::CheckCash;
    jv[std::string{sfFlags.jsonName}] = tfUniversal;
    return jv;
}

// Cancel a check.
boost::json::value
cancel(jtx::Account const& dest, uint256 const& checkId)
{
    boost::json::object jv;
    jv[std::string{sfAccount.jsonName}] = dest.human();
    jv[std::string{sfCheckID.jsonName}] = to_string(checkId);
    jv[std::string{sfTransactionType.jsonName}] = jss::CheckCancel;
    jv[std::string{sfFlags.jsonName}] = tfUniversal;
    return jv;
}

}  // namespace check

}  // namespace jtx
}  // namespace test
}  // namespace ripple
