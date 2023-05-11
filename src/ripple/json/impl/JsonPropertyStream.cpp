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

#include <ripple/json/JsonPropertyStream.h>
#include <ripple/json/json_value.h>
#include <iostream>

namespace ripple {

JsonPropertyStream::JsonPropertyStream()
{
    m_stack.reserve(64);
    m_stack.push_back(&m_top);
}

boost::json::object const&
JsonPropertyStream::top() const
{
    // Keshava: How is the m_top variable set? It seems to be set only in the default ctor.
    return m_top.as_object(); // Keshava: What if the top-most element is not an object??
}

void
JsonPropertyStream::map_begin()
{
    // top is array
    boost::json::array& top(m_stack.back()->as_array());
    boost::json::value& map(top.emplace_back(boost::json::object()));
    m_stack.push_back(&map);
}

void
JsonPropertyStream::map_begin(std::string const& key)
{
    // top is a map
    boost::json::object& top(m_stack.back()->as_object());
    boost::json::value& map(top[key] = boost::json::object());
    m_stack.push_back(&map);
}

void
JsonPropertyStream::map_end()
{
    m_stack.pop_back();
}

void
JsonPropertyStream::add(std::string const& key, short v)
{
    (*m_stack.back()).as_object()[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, unsigned short v)
{
    (*m_stack.back()).as_object()[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, int v)
{
    (*m_stack.back()).as_object()[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, unsigned int v)
{
    (*m_stack.back()).as_object()[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, long v)
{
    (*m_stack.back()).as_object()[key] = int(v);
}

void
JsonPropertyStream::add(std::string const& key, float v)
{
    (*m_stack.back()).as_object()[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, double v)
{
    (*m_stack.back()).as_object()[key] = v;
}

void
JsonPropertyStream::add(std::string const& key, std::string const& v)
{
    (*m_stack.back()).as_object()[key] = v;
}

void
JsonPropertyStream::array_begin()
{
    // top is array
    boost::json::array& top(m_stack.back()->as_array());
    boost::json::value& vec(top.emplace_back(boost::json::array()));
    m_stack.push_back(&vec);
}

void
JsonPropertyStream::array_begin(std::string const& key)
{
    // top is a map
    boost::json::value& top(*m_stack.back());
    boost::json::value& vec(top.as_object()[key] = boost::json::array());
    m_stack.push_back(&vec);
}

void
JsonPropertyStream::array_end()
{
    m_stack.pop_back();
}

void
JsonPropertyStream::add(short v)
{
    m_stack.back()->as_array().emplace_back(v);
}

void
JsonPropertyStream::add(unsigned short v)
{
    m_stack.back()->as_array().emplace_back(v);
}

void
JsonPropertyStream::add(int v)
{
    m_stack.back()->as_array().emplace_back(v);
}

void
JsonPropertyStream::add(unsigned int v)
{
    m_stack.back()->as_array().emplace_back(v);
}

void
JsonPropertyStream::add(long v)
{
    m_stack.back()->as_array().emplace_back(int(v));
}

void
JsonPropertyStream::add(float v)
{
    m_stack.back()->as_array().emplace_back(v);
}

void
JsonPropertyStream::add(double v)
{
    m_stack.back()->as_array().emplace_back(v);
}

void
JsonPropertyStream::add(std::string const& v)
{
    m_stack.back()->as_array().emplace_back(v);
}

}  // namespace ripple
