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

#include <ripple/protocol/jss.h>
#include <test/jtx/ticket.h>

namespace ripple {
namespace test {
namespace jtx {

namespace ticket {

boost::json::object
create(Account const& account, std::uint32_t count)
{
    boost::json::object jv;
    jv[jss::Account.c_str()] = account.human();
    jv[jss::TransactionType.c_str()] = jss::TicketCreate;
    jv[sfTicketCount.jsonName.c_str()] = count;
    return jv;
}

void
use::operator()(Env&, JTx& jt) const
{
    jt.fill_seq = false;
    jt[sfSequence.jsonName.c_str()] = 0u;
    jt[sfTicketSequence.jsonName.c_str()] = ticketSeq_;
}

}  // namespace ticket

}  // namespace jtx
}  // namespace test
}  // namespace ripple
