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

#ifndef RIPPLE_TEST_JTX_TER_H_INCLUDED
#define RIPPLE_TEST_JTX_TER_H_INCLUDED

#include <ripple/protocol/ErrorCodes.h>
#include <test/jtx/Env.h>
#include <tuple>

namespace ripple {
namespace test {
namespace jtx {

/** Set the expected result code for a JTx
    The test will fail if the code doesn't match.
*/
class ter
{
private:
    std::optional<TER> v_;

public:
    explicit ter(decltype(std::ignore))
    {
    }

    explicit ter(TER v) : v_(v)
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt.ter = v_;
    }
};

class envRpcErr
{
    // All JTx might not have an expected rpcError (they might be expected to
    // succeed with rpcSUCCESS). Hence using an optional
    std::optional<error_code_i> v_;

public:

    explicit envRpcErr(error_code_i e_) : v_(e_)
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt.rpcError = v_;
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
