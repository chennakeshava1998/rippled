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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/reporting/P2pProxy.h>
#include <ripple/json/json_value.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/impl/TransactionSign.h>

namespace ripple {

boost::json::object
doServerInfo(RPC::JsonContext& context)
{
    boost::json::object ret;

    ret[jss::info.c_str()] = context.netOps.getServerInfo(
        true,
        context.role == Role::ADMIN,
        context.params.contains(jss::counters.c_str()) &&
            context.params[jss::counters.c_str()].as_bool());

    if (context.app.config().reporting())
    {
        boost::json::value const proxied = forwardToP2p(context);
        auto const lf = proxied.as_object().at(jss::result.c_str()).at(jss::info.c_str()).at(jss::load_factor.c_str());
        auto const vq = proxied.as_object().at(jss::result.c_str()).at(jss::info.c_str()).at(jss::validation_quorum.c_str());
        ret[jss::info.c_str()].as_object()[jss::validation_quorum.c_str()] = vq.is_null() ? 1 : vq;
        ret[jss::info.c_str()].as_object()[jss::load_factor.c_str()] = lf.is_null() ? 1 : lf;
    }
    return ret;
}

}  // namespace ripple
