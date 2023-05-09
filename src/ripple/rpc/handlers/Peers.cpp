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

#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/net/RPCErr.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>

namespace ripple {

boost::json::object
doPeers(RPC::JsonContext& context)
{
    if (context.app.config().reporting())
        return rpcError(rpcREPORTING_UNSUPPORTED);

    boost::json::object jvResult;

    jvResult[jss::peers.c_str()] = context.app.overlay().json();

    // Legacy support
    if (context.apiVersion == 1)
    {
        for (auto& p : jvResult[jss::peers.c_str()].as_array())
        {
            if (p.as_object().contains(jss::track.c_str()))
            {
                auto const s = p.as_object()[jss::track.c_str()].as_string();

                if (s == "diverged")
                    p.as_object()["sanity"] = "insane";
                else if (s == "unknown")
                    p.as_object()["sanity"] = "unknown";
            }
        }
    }

    auto const now = context.app.timeKeeper().now();
    auto const self = context.app.nodeIdentity().first;

    boost::json::object& cluster = jvResult[jss::cluster.c_str()].emplace_object();
    std::uint32_t ref = context.app.getFeeTrack().getLoadBase();

    context.app.cluster().for_each(
        [&cluster, now, ref, &self](ClusterNode const& node) {
            if (node.identity() == self)
                return;

            boost::json::object& json =
                cluster[toBase58(TokenType::NodePublic, node.identity())].as_object();

            if (!node.name().empty())
                json[jss::tag.c_str()] = node.name();

            if ((node.getLoadFee() != ref) && (node.getLoadFee() != 0))
                json[jss::fee.c_str()] = static_cast<double>(node.getLoadFee()) / ref;

            if (node.getReportTime() != NetClock::time_point{})
                json[jss::age.c_str()] = (node.getReportTime() >= now)
                    ? 0
                    : (now - node.getReportTime()).count();
        });

    return jvResult;
}

}  // namespace ripple
