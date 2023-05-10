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

#include <ripple/basics/Log.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Seed.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>

namespace ripple {

static std::optional<Seed>
validationSeed(boost::json::object const& params)
{
    if (!params.contains(jss::secret.c_str()))
        return randomSeed();

    return parseGenericSeed(params.at(jss::secret.c_str()).as_string().c_str());
}

// {
//   secret: <string>   // optional
// }
//
// This command requires Role::ADMIN access because it makes
// no sense to ask an untrusted server for this.
boost::json::object
doValidationCreate(RPC::JsonContext& context)
{
    boost::json::object obj;

    auto seed = validationSeed(context.params);

    if (!seed)
        return rpcError(rpcBAD_SEED);

    auto const private_key = generateSecretKey(KeyType::secp256k1, *seed);

    obj[jss::validation_public_key.c_str()] = toBase58(
        TokenType::NodePublic,
        derivePublicKey(KeyType::secp256k1, private_key));

    obj[jss::validation_private_key.c_str()] =
        toBase58(TokenType::NodePrivate, private_key);

    obj[jss::validation_seed.c_str()] = toBase58(*seed);
    obj[jss::validation_key.c_str()] = seedAs1751(*seed);

    return obj;
}

}  // namespace ripple
