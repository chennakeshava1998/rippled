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

#include <ripple/json/json_value.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/handlers/Handlers.h>

#include <optional>
#include <string>
#include <utility>

namespace ripple {

boost::json::object
doPeerReservationsAdd(RPC::JsonContext& context)
{
    if (context.app.config().reporting())
        return rpcError(rpcREPORTING_UNSUPPORTED);

    auto const& params = context.params;

    if (!params.contains(jss::public_key.c_str()))
        return RPC::missing_field_error(jss::public_key);

    // Returning JSON from every function ruins any attempt to encapsulate
    // the pattern of "get field F as type T, and diagnose an error if it is
    // missing or malformed":
    // - It is costly to copy whole JSON objects around just to check whether an
    //   error code is present.
    // - It is not as easy to read when cluttered by code to pack and unpack the
    //   JSON object.
    // - It is not as easy to write when you have to include all the packing and
    //   unpacking code.
    // Exceptions would be easier to use, but have a terrible cost for control
    // flow. An error monad is purpose-built for this situation; it is
    // essentially an optional (the "maybe monad" in Haskell) with a non-unit
    // type for the failure case to capture more information.
    if (!params.at(jss::public_key.c_str()).is_string())
        return RPC::expected_field_error(jss::public_key, "a string");

    // Same for the pattern of "if field F is present, make sure it has type T
    // and get it".
    std::string desc;
    if (params.contains(jss::description.c_str()))
    {
        if (!params.at(jss::description.c_str()).is_string())
            return RPC::expected_field_error(jss::description, "a string");
        desc = params.at(jss::description.c_str()).as_string();
    }

    // channel_verify takes a key in both base58 and hex.
    // @nikb prefers that we take only base58.
    std::optional<PublicKey> optPk = parseBase58<PublicKey>(
        TokenType::NodePublic, params.at(jss::public_key.c_str()).as_string().c_str());
    if (!optPk)
        return rpcError(rpcPUBLIC_MALFORMED);
    PublicKey const& nodeId = *optPk;

    auto const previous = context.app.peerReservations().insert_or_assign(
        PeerReservation{nodeId, desc});

    boost::json::object result;
    if (previous)
    {
        result[jss::previous.c_str()] = previous->toJson();
    }
    return result;
}

boost::json::object
doPeerReservationsDel(RPC::JsonContext& context)
{
    if (context.app.config().reporting())
        return rpcError(rpcREPORTING_UNSUPPORTED);

    auto const& params = context.params;

    // We repeat much of the parameter parsing from `doPeerReservationsAdd`.
    if (!params.contains(jss::public_key.c_str()))
        return RPC::missing_field_error(jss::public_key);
    if (!params.at(jss::public_key.c_str()).is_string())
        return RPC::expected_field_error(jss::public_key, "a string");

    std::optional<PublicKey> optPk = parseBase58<PublicKey>(
        TokenType::NodePublic, params.at(jss::public_key.c_str()).as_string().c_str());
    if (!optPk)
        return rpcError(rpcPUBLIC_MALFORMED);
    PublicKey const& nodeId = *optPk;

    auto const previous = context.app.peerReservations().erase(nodeId);

    boost::json::object result;
    if (previous)
    {
        result[jss::previous.c_str()] = previous->toJson();
    }
    return result;
}

boost::json::object
doPeerReservationsList(RPC::JsonContext& context)
{
    if (context.app.config().reporting())
        return rpcError(rpcREPORTING_UNSUPPORTED);

    auto const& reservations = context.app.peerReservations().list();
    // Enumerate the reservations in context.app.peerReservations()
    // as a Json::Value.
    boost::json::object result;
    boost::json::array& jaReservations = result[jss::reservations.c_str()].emplace_array();
    for (auto const& reservation : reservations)
    {
        jaReservations.emplace_back(reservation.toJson());
    }
    return result;
}

}  // namespace ripple
