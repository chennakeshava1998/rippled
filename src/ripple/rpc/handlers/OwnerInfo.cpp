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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>

namespace ripple {

// {
//   'ident' : <indent>,
// }
boost::json::object
doOwnerInfo(RPC::JsonContext& context)
{
    if (!context.params.contains(jss::account.c_str()) &&
        !context.params.contains(jss::ident.c_str()))
    {
        return RPC::missing_field_error(jss::account);
    }

    std::string strIdent = context.params.contains(jss::account.c_str())
        ? context.params[jss::account.c_str()].as_string().c_str()
        : context.params[jss::ident.c_str()].as_string().c_str();
    boost::json::object ret;

    // Get info on account.

    auto const& closedLedger = context.ledgerMaster.getClosedLedger();
    AccountID accountID;
    auto jAccepted = RPC::accountFromString(accountID, strIdent);

    ret[jss::accepted.c_str()] = !jAccepted.empty()
        ? context.netOps.getOwnerInfo(closedLedger, accountID)
        : jAccepted;

    auto const& currentLedger = context.ledgerMaster.getCurrentLedger();
    auto jCurrent = RPC::accountFromString(accountID, strIdent);

    ret[jss::current.c_str()] = !jCurrent.empty()
        ? context.netOps.getOwnerInfo(currentLedger, accountID)
        : jCurrent;
    return ret;
}

}  // namespace ripple
