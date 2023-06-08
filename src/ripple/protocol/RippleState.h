//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_RIPPLE_STATE_H_INCLUDED
#define RIPPLE_PROTOCOL_RIPPLE_STATE_H_INCLUDED

#include <ripple/protocol/LedgerEntryWrapper.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>

namespace ripple {

template <bool Writable>
class RippleStateImpl final : public LedgerEntryWrapper<Writable>
{
private:
    using Base = LedgerEntryWrapper<Writable>;
    using SleT = typename Base::SleT;
    using Base::wrapped_;

    // This constructor is private so only the factory functions can
    // construct an RippleStateImpl.
    RippleStateImpl(std::shared_ptr<SleT>&& w) : Base(std::move(w))
    {
    }

    // Friend declarations of factory functions.
    //
    // For classes that contain factories we must declare the entire class
    // as a friend unless the class declaration is visible at this point.
    friend class ReadView;
    friend class ApplyView;

public:
    // Conversion operator from RippleStateImpl<true> to RippleStateImpl<false>.
    operator RippleStateImpl<true>() const
    {
        return RippleStateImpl<false>(
            std::const_pointer_cast<std::shared_ptr<STLedgerEntry const>>(
                wrapped_));
    }

    [[nodiscard]] inline uint256 const&
    key()
    {
        return wrapped_->key();
    }

    [[nodiscard]] std::uint64_t
    getFieldU64(SField const& field) const
    {
        return wrapped_->getFieldU64(field);
    }

    [[nodiscard]] STAmount const&
    getFieldAmount(SField const& field) const
    {
        return wrapped_->getFieldAmount(field);
    }

    [[nodiscard]] std::uint32_t
    getFieldU32(SField const& field) const
    {
        return wrapped_->getFieldU32(field);
    }

    void
    setFieldU32(SField const& field, std::uint32_t v) {
        return wrapped_->setFieldU32(field, v);
    }

    void
    setFieldAmount(SField const& field, STAmount const& v)
    {
        return wrapped_->setFieldAmount(field, v);
    }

    void
    makeFieldAbsent(SField const& field)
    {
        return wrapped_->makeFieldAbsent(field);
    }



};

using RippleStateRd = RippleStateImpl<false>;
using RippleState = RippleStateImpl<true>;

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_RIPPLE_STATE_H_INCLUDED
