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

#include <ripple/app/ledger/AcceptedLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/rdb/backend/SQLiteDatabase.h>
#include <ripple/basics/UptimeClock.h>
#include <ripple/json/json_value.h>
#include <ripple/ledger/CachedSLEs.h>
#include <ripple/net/RPCErr.h>
#include <ripple/nodestore/Database.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/shamap/ShardFamily.h>

namespace ripple {

static void
textTime(
    std::string& text,
    UptimeClock::time_point& seconds,
    const char* unitName,
    std::chrono::seconds unitVal)
{
    auto i = seconds.time_since_epoch() / unitVal;

    if (i == 0)
        return;

    seconds -= unitVal * i;

    if (!text.empty())
        text += ", ";

    text += std::to_string(i);
    text += " ";
    text += unitName;

    if (i > 1)
        text += "s";
}

boost::json::object
getCountsJson(Application& app, int minObjectCount)
{
    auto objectCounts = CountedObjects::getInstance().getCounts(minObjectCount);

    boost::json::object ret;

    for (auto const& [k, v] : objectCounts)
    {
        ret[k] = v;
    }

    if (!app.config().reporting() && app.config().useTxTables())
    {
        auto const db =
            dynamic_cast<SQLiteDatabase*>(&app.getRelationalDatabase());

        if (!db)
            Throw<std::runtime_error>("Failed to get relational database");

        auto dbKB = db->getKBUsedAll();

        if (dbKB > 0)
            ret[jss::dbKBTotal.c_str()] = dbKB;

        dbKB = db->getKBUsedLedger();

        if (dbKB > 0)
            ret[jss::dbKBLedger.c_str()] = dbKB;

        dbKB = db->getKBUsedTransaction();

        if (dbKB > 0)
            ret[jss::dbKBTransaction.c_str()] = dbKB;

        {
            std::size_t c = app.getOPs().getLocalTxCount();
            if (c > 0)
                ret[jss::local_txs.c_str()] = static_cast<Json::UInt>(c);
        }
    }

    ret[jss::write_load.c_str()] = app.getNodeStore().getWriteLoad();

    ret[jss::historical_perminute.c_str()] =
        static_cast<int>(app.getInboundLedgers().fetchRate());
    ret[jss::SLE_hit_rate.c_str()] = app.cachedSLEs().rate();
    ret[jss::ledger_hit_rate.c_str()] = app.getLedgerMaster().getCacheHitRate();
    ret[jss::AL_size.c_str()] = Json::UInt(app.getAcceptedLedgerCache().size());
    ret[jss::AL_hit_rate.c_str()] = app.getAcceptedLedgerCache().getHitRate();

    ret[jss::fullbelow_size.c_str()] =
        static_cast<int>(app.getNodeFamily().getFullBelowCache(0)->size());
    ret[jss::treenode_cache_size.c_str()] =
        app.getNodeFamily().getTreeNodeCache(0)->getCacheSize();
    ret[jss::treenode_track_size.c_str()] =
        app.getNodeFamily().getTreeNodeCache(0)->getTrackSize();

    std::string uptime;
    auto s = UptimeClock::now();
    using namespace std::chrono_literals;
    textTime(uptime, s, "year", 365 * 24h);
    textTime(uptime, s, "day", 24h);
    textTime(uptime, s, "hour", 1h);
    textTime(uptime, s, "minute", 1min);
    textTime(uptime, s, "second", 1s);
    ret[jss::uptime.c_str()] = uptime;

    if (auto shardStore = app.getShardStore())
    {
        auto shardFamily{dynamic_cast<ShardFamily*>(app.getShardFamily())};
        auto const [cacheSz, trackSz] = shardFamily->getTreeNodeCacheSize();
        boost::json::object& jv = ret[jss::shards.c_str()].emplace_object();

        jv[jss::fullbelow_size.c_str()] = shardFamily->getFullBelowCacheSize();
        jv[jss::treenode_cache_size.c_str()] = cacheSz;
        jv[jss::treenode_track_size.c_str()] = trackSz;
        ret[jss::write_load.c_str()] = shardStore->getWriteLoad();
        jv[jss::node_writes.c_str()] = std::to_string(shardStore->getStoreCount());
        jv[jss::node_reads_total.c_str()] = shardStore->getFetchTotalCount();
        jv[jss::node_reads_hit.c_str()] = shardStore->getFetchHitCount();
        jv[jss::node_written_bytes.c_str()] =
            std::to_string(shardStore->getStoreSize());
        jv[jss::node_read_bytes.c_str()] = shardStore->getFetchSize();
    }
    else
    {
        app.getNodeStore().getCountsJson(ret);
    }

    return ret;
}

// {
//   min_count: <number>  // optional, defaults to 10
// }
boost::json::object
doGetCounts(RPC::JsonContext& context)
{
    int minCount = 10;

    if (context.params.contains(jss::min_count.c_str()))
        minCount = context.params[jss::min_count.c_str()].as_uint64();

    return getCountsJson(context.app, minCount);
}

}  // namespace ripple
