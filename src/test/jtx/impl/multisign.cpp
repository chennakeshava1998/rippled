//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/basics/contract.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <optional>
#include <sstream>
#include <test/jtx/multisign.h>
#include <test/jtx/utility.h>

namespace ripple {
namespace test {
namespace jtx {

boost::json::object
signers(
    Account const& account,
    std::uint32_t quorum,
    std::vector<signer> const& v)
{
    boost::json::object jv;
    jv[jss::Account.c_str()] = account.human();
    jv[jss::TransactionType.c_str()] = jss::SignerListSet;
    jv[sfSignerQuorum.getJsonName().c_str()] = quorum;
    auto& ja = jv[sfSignerEntries.getJsonName().c_str()];
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        auto const& e = v[i];
        boost::json::object& je = ja.as_array()[i].as_object()[sfSignerEntry.getJsonName().c_str()].as_object();
        je[jss::Account.c_str()] = e.account.human();
        je[sfSignerWeight.getJsonName().c_str()] = e.weight;
        if (e.tag)
            je[sfWalletLocator.getJsonName().c_str()] = to_string(*e.tag);
    }
    return jv;
}

boost::json::object
signers(Account const& account, none_t)
{
    boost::json::object jv;
    jv[jss::Account.c_str()] = account.human();
    jv[jss::TransactionType.c_str()] = jss::SignerListSet;
    jv[sfSignerQuorum.getJsonName().c_str()] = 0;
    return jv;
}

//------------------------------------------------------------------------------

msig::msig(std::vector<msig::Reg> signers_) : signers(std::move(signers_))
{
    // Signatures must be applied in sorted order.
    std::sort(
        signers.begin(),
        signers.end(),
        [](msig::Reg const& lhs, msig::Reg const& rhs) {
            return lhs.acct.id() < rhs.acct.id();
        });
}

void
msig::operator()(Env& env, JTx& jt) const
{
    auto const mySigners = signers;
    jt.signer = [mySigners, &env](Env&, JTx& jtx) {
        jtx[sfSigningPubKey.getJsonName().c_str()] = "";
        std::optional<STObject> st;
        try
        {
            st = parse(jtx.jv);
        }
        catch (parse_error const&)
        {
            env.test.log << serialize(jtx.jv) << std::endl;
            Rethrow();
        }
        auto& js = jtx[sfSigners.getJsonName().c_str()];
        for (std::size_t i = 0; i < mySigners.size(); ++i)
        {
            auto const& e = mySigners[i];
            boost::json::object& jo = js.as_array()[i].as_object()[sfSigner.getJsonName().c_str()].as_object();
            jo[jss::Account.c_str()] = e.acct.human();
            jo[jss::SigningPubKey.c_str()] = strHex(e.sig.pk().slice());

            Serializer ss{buildMultiSigningData(*st, e.acct.id())};
            auto const sig = ripple::sign(
                *publicKeyType(e.sig.pk().slice()), e.sig.sk(), ss.slice());
            jo[sfTxnSignature.getJsonName().c_str()] =
                strHex(Slice{sig.data(), sig.size()});
        }
    };
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
