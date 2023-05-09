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
#include <ripple/basics/StringUtilities.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/PayChan.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/Tuning.h>

#include <optional>

namespace ripple {

// {
//   secret_key: <signing_secret_key>
//   key_type: optional; either ed25519 or secp256k1 (default to secp256k1)
//   channel_id: 256-bit channel id
//   drops: 64-bit uint (as string)
// }
boost::json::object
doChannelAuthorize(RPC::JsonContext& context)
{
    auto const& params(context.params);
    for (auto const& p : {jss::channel_id, jss::amount})
        if (!params.contains(p.c_str()))
            return RPC::missing_field_error(p);

    // Compatibility if a key type isn't specified. If it is, the
    // keypairForSignature code will validate parameters and return
    // the appropriate error.
    if (!params.contains(jss::key_type.c_str()) && !params.contains(jss::secret.c_str()))
        return RPC::missing_field_error(jss::secret);

    boost::json::object result;
    auto const [pk, sk] = RPC::keypairForSignature(params, result);
    if (RPC::contains_error(result))
        return result;

    uint256 channelId;
    if (!channelId.parseHex(params.at(jss::channel_id.c_str()).as_string()))
        return rpcError(rpcCHANNEL_MALFORMED);

    std::optional<std::uint64_t> const optDrops = params.at(jss::amount.c_str()).is_string()
        ? to_uint64(params.at(jss::amount.c_str()).as_string().c_str())
        : std::nullopt;

    if (!optDrops)
        return rpcError(rpcCHANNEL_AMT_MALFORMED);

    std::uint64_t const drops = *optDrops;

    Serializer msg;
    serializePayChanAuthorization(msg, channelId, XRPAmount(drops));

    try
    {
        auto const buf = sign(pk, sk, msg.slice());
        result[jss::signature.c_str()] = strHex(buf);
    }
    catch (std::exception const& ex)
    {
        result = RPC::make_error(
            rpcINTERNAL,
            "Exception occurred during signing: " + std::string(ex.what()));
    }
    return result;
}

// {
//   public_key: <public_key>
//   channel_id: 256-bit channel id
//   drops: 64-bit uint (as string)
//   signature: signature to verify
// }
boost::json::object
doChannelVerify(RPC::JsonContext& context)
{
    auto const& params(context.params);
    for (auto const& p :
         {jss::public_key, jss::channel_id, jss::amount, jss::signature})
        if (!params.contains(p.c_str()))
            return RPC::missing_field_error(p);

    std::optional<PublicKey> pk;
    {
        std::string const strPk = params.at(jss::public_key.c_str()).as_string().c_str();
        pk = parseBase58<PublicKey>(TokenType::AccountPublic, strPk);

        if (!pk)
        {
            auto pkHex = strUnHex(strPk);
            if (!pkHex)
                return rpcError(rpcPUBLIC_MALFORMED);
            auto const pkType = publicKeyType(makeSlice(*pkHex));
            if (!pkType)
                return rpcError(rpcPUBLIC_MALFORMED);
            pk.emplace(makeSlice(*pkHex));
        }
    }

    uint256 channelId;
    if (!channelId.parseHex(params.at(jss::channel_id.c_str()).as_string()))
        return rpcError(rpcCHANNEL_MALFORMED);

    std::optional<std::uint64_t> const optDrops = params.at(jss::amount.c_str()).is_string()
        ? to_uint64(params.at(jss::amount.c_str()).as_string().c_str())
        : std::nullopt;

    if (!optDrops)
        return rpcError(rpcCHANNEL_AMT_MALFORMED);

    std::uint64_t const drops = *optDrops;

    auto sig = strUnHex(params.at(jss::signature.c_str()).as_string().c_str());
    if (!sig || !sig->size())
        return rpcError(rpcINVALID_PARAMS);

    Serializer msg;
    serializePayChanAuthorization(msg, channelId, XRPAmount(drops));

    boost::json::object result;
    result[jss::signature_verified.c_str()] =
        verify(*pk, msg.slice(), makeSlice(*sig), /*canonical*/ true);
    return result;
}

}  // namespace ripple
