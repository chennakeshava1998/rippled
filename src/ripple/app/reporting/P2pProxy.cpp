//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/app/reporting/P2pProxy.h>
#include <ripple/app/reporting/ReportingETL.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/json_writer.h>

namespace ripple {

boost::json::value
forwardToP2p(RPC::JsonContext& context)
{
    return context.app.getReportingETL().getETLLoadBalancer().forwardToP2p(
        context);
}

std::unique_ptr<org::xrpl::rpc::v1::XRPLedgerAPIService::Stub>
getP2pForwardingStub(RPC::Context& context)
{
    return context.app.getReportingETL()
        .getETLLoadBalancer()
        .getP2pForwardingStub();
}

// We only forward requests where ledger_index is "current" or "closed"
// otherwise, attempt to handle here
bool
shouldForwardToP2p(RPC::JsonContext& context)
{
    if (!context.app.config().reporting())
        return false;

    boost::json::object& params = context.params;
    std::string strCommand = std::string{params.contains(jss::command.c_str())
        ? params[jss::command.c_str()].as_string()
        : params[jss::method.c_str()].as_string()};

    JLOG(context.j.trace()) << "COMMAND:" << strCommand;
    JLOG(context.j.trace()) << "REQUEST:" << params;
    auto handler = RPC::getHandler(
        context.apiVersion, context.app.config().BETA_RPC_API, strCommand);
    if (!handler)
    {
        JLOG(context.j.error())
            << "Error getting handler. command = " << strCommand;
        return false;
    }

    if (handler->condition_ == RPC::NEEDS_CURRENT_LEDGER ||
        handler->condition_ == RPC::NEEDS_CLOSED_LEDGER)
    {
        return true;
    }

    if (params.contains(jss::ledger_index.c_str()))
    {
        auto indexValue = params[jss::ledger_index.c_str()];
        if (indexValue.is_string())
        {
            auto index = indexValue.as_string();
            return index == "current" || index == "closed";
        }
    }
    return false;
}

}  // namespace ripple
