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
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>

namespace ripple {

// {
//   feature : <feature>
//   vetoed : true/false
// }
boost::json::object
doFeature(RPC::JsonContext& context)
{
    if (context.app.config().reporting())
        return rpcError(rpcREPORTING_UNSUPPORTED);

    // Get majority amendment status
    majorityAmendments_t majorities;

    if (auto const valLedger = context.ledgerMaster.getValidatedLedger())
        majorities = getMajorityAmendments(*valLedger);

    auto& table = context.app.getAmendmentTable();

    if (!context.params.contains(jss::feature.c_str()))
    {
        auto features = table.getJson();

        for (auto const& [h, t] : majorities)
        {
            features[to_string(h)].as_object()[jss::majority.c_str()] =
                t.time_since_epoch().count();
        }

        boost::json::object jvReply;
        jvReply[jss::features.c_str()] = features;
        return jvReply;
    }

    // Keshava: what are the tradeoffs between the .c_str() method versus explicitly type-casting into std::string?
    auto feature = table.find(context.params[jss::feature.c_str()].as_string().c_str());

    // If the feature is not found by name, try to parse the `feature` param as
    // a feature ID. If that fails, return an error.
    if (!feature && !feature.parseHex(context.params[jss::feature.c_str()].as_string()))
        return rpcError(rpcBAD_FEATURE);

    if (context.params.contains(jss::vetoed.c_str()))
    {
        if (context.params[jss::vetoed.c_str()].as_bool())
            table.veto(feature);
        else
            table.unVeto(feature);
    }

    boost::json::object jvReply = table.getJson(feature);

    auto m = majorities.find(feature);
    if (m != majorities.end())
        jvReply[jss::majority.c_str()] = m->second.time_since_epoch().count();

    return jvReply;
}

}  // namespace ripple
