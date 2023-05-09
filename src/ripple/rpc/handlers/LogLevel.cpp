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
#include <ripple/basics/Log.h>
#include <ripple/json/json_value.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <boost/algorithm/string/predicate.hpp>

namespace ripple {

boost::json::object
doLogLevel(RPC::JsonContext& context)
{
    // log_level
    if (!context.params.contains(jss::severity.c_str()))
    {
        // get log severities
        boost::json::object ret;
        boost::json::object lev;

        lev[jss::base.c_str()] =
            Logs::toString(Logs::fromSeverity(context.app.logs().threshold()));
        std::vector<std::pair<std::string, std::string>> logTable(
            context.app.logs().partition_severities());
        using stringPair = std::map<std::string, std::string>::value_type;
        for (auto const& [k, v] : logTable)
            lev[k] = v;

        ret[jss::levels.c_str()] = lev;
        return ret;
    }

    LogSeverity const sv(
        Logs::fromString(context.params[jss::severity.c_str()].as_string().c_str()));

    if (sv == lsINVALID)
        return rpcError(rpcINVALID_PARAMS);

    auto severity = Logs::toSeverity(sv);
    // log_level severity
    if (!context.params.contains(jss::partition.c_str()))
    {
        // set base log threshold
        context.app.logs().threshold(severity);
        return Json::objectValue;
    }

    // log_level partition severity base?
    if (context.params.contains(jss::partition.c_str()))
    {
        // set partition threshold
        std::string partition(context.params[jss::partition.c_str()].as_string().c_str());

        if (boost::iequals(partition, "base"))
            context.app.logs().threshold(severity);
        else
            context.app.logs().get(partition).threshold(severity);

        return boost::json::object();
    }

    return rpcError(rpcINVALID_PARAMS);
}

}  // namespace ripple
