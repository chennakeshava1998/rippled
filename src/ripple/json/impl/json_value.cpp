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
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/json/impl/json_assert.h>
#include <ripple/json/json_writer.h>
#include <ripple/json/to_string.h>

namespace Json {

const Value Value::null;
const Int Value::minInt = Int(~(UInt(-1) / 2));
const Int Value::maxInt = Int(UInt(-1) / 2);
const UInt Value::maxUInt = UInt(-1);

class DefaultValueAllocator : public ValueAllocator
{
public:
    virtual ~DefaultValueAllocator() = default;

    char*
    makeMemberName(const char* memberName) override
    {
        return duplicateStringValue(memberName);
    }

    void
    releaseMemberName(char* memberName) override
    {
        releaseStringValue(memberName);
    }

    char*
    duplicateStringValue(const char* value, unsigned int length = unknown)
        override
    {
        //@todo investigate this old optimization
        // if ( !value  ||  value[0] == 0 )
        //   return 0;

        if (length == unknown)
            length = value ? (unsigned int)strlen(value) : 0;

        char* newString = static_cast<char*>(malloc(length + 1));
        if (value)
            memcpy(newString, value, length);
        newString[length] = 0;
        return newString;
    }

    void
    releaseStringValue(char* value) override
    {
        if (value)
            free(value);
    }
};

static ValueAllocator*&
valueAllocator()
{
    static ValueAllocator* valueAllocator = new DefaultValueAllocator;
    return valueAllocator;
}

static struct DummyValueAllocatorInitializer
{
    DummyValueAllocatorInitializer()
    {
        valueAllocator();  // ensure valueAllocator() statics are initialized
                           // before main().
    }
} dummyValueAllocatorInitializer;

// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// class Value::CZString
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////

// Notes: index_ indicates if the string was allocated when
// a string is stored.

Value::CZString::CZString(int index) : cstr_(0), index_(index)
{
}

Value::CZString::CZString(const char* cstr, DuplicationPolicy allocate)
    : cstr_(
          allocate == duplicate ? valueAllocator()->makeMemberName(cstr) : cstr)
    , index_(allocate)
{
}

Value::CZString::CZString(const CZString& other)
    : cstr_(
          other.index_ != noDuplication && other.cstr_ != 0
              ? valueAllocator()->makeMemberName(other.cstr_)
              : other.cstr_)
    , index_(
          other.cstr_
              ? (other.index_ == noDuplication ? noDuplication : duplicate)
              : other.index_)
{
}

Value::CZString::~CZString()
{
    if (cstr_ && index_ == duplicate)
        valueAllocator()->releaseMemberName(const_cast<char*>(cstr_));
}

bool
Value::CZString::operator<(const CZString& other) const
{
    if (cstr_ && other.cstr_)
        return strcmp(cstr_, other.cstr_) < 0;

    return index_ < other.index_;
}

bool
Value::CZString::operator==(const CZString& other) const
{
    if (cstr_ && other.cstr_)
        return strcmp(cstr_, other.cstr_) == 0;

    return index_ == other.index_;
}

int
Value::CZString::index() const
{
    return index_;
}

const char*
Value::CZString::c_str() const
{
    return cstr_;
}

bool
Value::CZString::isStaticString() const
{
    return index_ == noDuplication;
}

// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// class Value::Value
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////////

/*! \internal Default constructor initialization must be equivalent to:
 * memset( this, 0, sizeof(Value) )
 * This optimization is used in ValueInternalMap fast allocator.
 */
Value::Value(ValueType type) : type_(type), allocated_(0)
{
    switch (type)
    {
        case nullValue:
            jv_.emplace_null();
            break;

        case intValue:
            jv_.emplace_int64();
            value_.int_ = 0;
            break;

        case uintValue:
            jv_.emplace_uint64();
            value_.int_ = 0;
            break;

        case realValue:
            value_.real_ = 0.0;
            jv_.emplace_double();
            break;

        case stringValue:
            value_.string_ = 0;
            jv_.emplace_string();
            break;

        case arrayValue:
            jv_.emplace_array();
            value_.map_ = new ObjectValues();
            break;

        case objectValue:
            jv_.emplace_object();
            value_.map_ = new ObjectValues();
            break;

        case booleanValue:
            jv_.emplace_bool();
            value_.bool_ = false;
            break;

        default:
            JSON_ASSERT_UNREACHABLE;
    }
}

Value::Value(boost::json::value& other, ValueType type) {
    type_ = type;
    jv_ = other;
}

Value::Value(const boost::json::value& other, ValueType type) {
    type_ = type;
    jv_ = other;
}

Value::Value(Int value) : type_(intValue)
{
    value_.int_ = value;
    jv_.emplace_int64() = value;
}

Value::Value(UInt value) : type_(uintValue)
{
    value_.uint_ = value;
    jv_.emplace_uint64() = value;
}

Value::Value(double value) : type_(realValue)
{
    value_.real_ = value;
    jv_.emplace_double() = value;
}

// TODO: Use std::string_view or templates to remove the redundancy
Value::Value(const char* value) : type_(stringValue), allocated_(true)
{
    value_.string_ = valueAllocator()->duplicateStringValue(value);
    jv_.emplace_string() = value;
}

Value::Value(std::string const& value) : type_(stringValue), allocated_(true)
{
    value_.string_ = valueAllocator()->duplicateStringValue(
        value.c_str(), (unsigned int)value.length());
    jv_.emplace_string() = value;
}

Value::Value(const StaticString& value) : type_(stringValue), allocated_(false)
{
    value_.string_ = const_cast<char*>(value.c_str());
    jv_.emplace_string() = value;
}

Value::Value(bool value) : type_(booleanValue)
{
    value_.bool_ = value;
    jv_.emplace_bool() = value;
}

Value::Value(const Value& other) : type_(other.type_)
{
    jv_ = other.jv_;
    switch (type_)
    {
        case nullValue:
        case intValue:
        case uintValue:
        case realValue:
        case booleanValue:
            value_ = other.value_;
            break;

        case stringValue:
            if (other.value_.string_)
            {
                value_.string_ = valueAllocator()->duplicateStringValue(
                    other.value_.string_);
                allocated_ = true;
            }
            else
                value_.string_ = 0;

            break;

        case arrayValue:
        case objectValue:
            value_.map_ = new ObjectValues(*other.value_.map_);
            break;

        default:
            JSON_ASSERT_UNREACHABLE;
    }
}

Value::~Value()
{
    // no manual deletion steps required boost's json
    switch (type_)
    {
        case nullValue:
        case intValue:
        case uintValue:
        case realValue:
        case booleanValue:
            break;

        case stringValue:
            if (allocated_)
                valueAllocator()->releaseStringValue(value_.string_);

            break;

        case arrayValue:
        case objectValue:
            if (value_.map_)
                delete value_.map_;
            break;

        default:
            JSON_ASSERT_UNREACHABLE;
    }
}

Value&
Value::operator=(Value const& other)
{
    Value tmp(other);
    swap(tmp);
    return *this;
}

// Aliter: Consider using boost's pilfer operator instead of std::move
Value::Value(Value&& other) noexcept
    : value_(other.value_), type_(other.type_), allocated_(other.allocated_), jv_(std::move(other.jv_))
{
    other.type_ = nullValue;
    other.allocated_ = 0;
}

Value&
Value::operator=(Value&& other)
{
    Value tmp(std::move(other));
    swap(tmp);
    return *this;
}

void
Value::swap(Value& other) noexcept
{
    std::swap(value_, other.value_);
    std::swap(jv_, other.jv_);

    ValueType temp = type_;
    type_ = other.type_;
    other.type_ = temp;

    int temp2 = allocated_;
    allocated_ = other.allocated_;
    other.allocated_ = temp2;
}

ValueType
Value::type() const
{
    return type_;
}

// TODO: Is this semantically correct??
// boost::json compares two objects lexicographicaly
bool
operator<(const Value& x, const Value& y) {
    return boost::json::serialize(x.jv_) < boost::json::serialize(y.jv_);
}

bool
operator==(const Value& x, const Value& y) {
    return x.jv_ == y.jv_;
}

const char*
Value::asCString() const
{
    JSON_ASSERT(type_ == stringValue);
    JSON_ASSERT(jv_.is_string());
    return jv_.get_string().data();
}

std::string
Value::asString() const
{
//    return boost::json::serialize(jv_);
    switch (type_)
    {
        case nullValue:
            return "";

        case stringValue:
            return jv_.is_string() ? std::string{jv_.get_string()} : "";

        case booleanValue:
            return jv_.as_bool() ? "true" : "false";

        case intValue:
            return std::to_string(asInt());

        case uintValue:
            return std::to_string(asUInt());

        case realValue:
            return std::to_string(asDouble());

        case arrayValue:
        case objectValue:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to string");

        default:
            JSON_ASSERT_UNREACHABLE;
    }

    return "";  // unreachable
}

Value::Int
Value::asInt() const
{
    switch (type_)
    {
        case nullValue:
            return 0;

        case intValue:
            return jv_.get_int64();

        case uintValue:
            JSON_ASSERT_MESSAGE(
                    jv_.get_uint64() < (unsigned)maxInt,
                "integer out of signed integer range");
            return jv_.get_uint64();

        case realValue:
            JSON_ASSERT_MESSAGE(
                value_.real_ >= minInt && value_.real_ <= maxInt,
                "Real out of signed integer range");
            return Int(jv_.get_double());

        case booleanValue:
            return jv_.get_bool() ? 1 : 0;

        case stringValue: {
            char const* const str{jv_.is_string() ? jv_.get_string().data() : ""};
            return beast::lexicalCastThrow<int>(str);
        }

        case arrayValue:
        case objectValue:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to int");

        default:
            JSON_ASSERT_UNREACHABLE;
    }

    return 0;  // unreachable;
}

Value::UInt
Value::asUInt() const
{
    switch (type_)
    {
        case nullValue:
            return 0;

        case intValue:
            JSON_ASSERT_MESSAGE(
                    jv_.get_int64() >= 0,
                "Negative integer can not be converted to unsigned integer");
            return jv_.get_int64();

        case uintValue:
            return jv_.get_uint64();

        case realValue:
            JSON_ASSERT_MESSAGE(
                value_.real_ >= 0 && value_.real_ <= maxUInt,
                "Real out of unsigned integer range");
            return UInt(jv_.get_double());

        case booleanValue:
            return jv_.get_bool() ? 1 : 0;

        case stringValue: {
            char const* const str{jv_.is_string() ? jv_.get_string().data() : ""};
            return beast::lexicalCastThrow<unsigned int>(str);
        }

        case arrayValue:
        case objectValue:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to uint");

        default:
            JSON_ASSERT_UNREACHABLE;
    }

    return 0;  // unreachable;
}

double
Value::asDouble() const
{
    switch (type_)
    {
        case nullValue:
            return 0.0;

        case intValue:
            return jv_.get_int64();

        case uintValue:
            return jv_.get_uint64();

        case realValue:
            return jv_.get_double();

        case booleanValue:
            return jv_.get_bool() ? 1.0 : 0.0;

        case stringValue:
        case arrayValue:
        case objectValue:
            JSON_ASSERT_MESSAGE(false, "Type is not convertible to double");

        default:
            JSON_ASSERT_UNREACHABLE;
    }

    return 0;  // unreachable;
}

bool
Value::asBool() const
{
    switch (type_)
    {
        case nullValue:
            return false;

        case intValue:
            return jv_.get_int64() != 0;
        case uintValue:
            return jv_.get_uint64() != 0;

        case realValue:
            return jv_.get_double() != 0;

        case booleanValue:
            return jv_.get_bool();

        case stringValue:
            return jv_.is_string() && !jv_.as_string().empty();

        case arrayValue:
            return !jv_.as_array().empty();
        case objectValue:
            return !jv_.as_object().empty();

        default:
            JSON_ASSERT_UNREACHABLE;
    }

    return false;  // unreachable;
}

bool
Value::isConvertibleTo(ValueType other) const
{
    switch (type_)
    {
        case nullValue:
            return true;

        case intValue:
            return (other == nullValue && asInt() == 0) ||
                other == intValue || (other == uintValue && asInt() >= 0) ||
                other == realValue || other == stringValue ||
                other == booleanValue;

        case uintValue:
            return (other == nullValue && asUInt() == 0) ||
                (other == intValue && asUInt() <= (unsigned)maxInt) ||
                other == uintValue || other == realValue ||
                other == stringValue || other == booleanValue;

        case realValue:
            return (other == nullValue && asDouble() == 0.0) ||
                (other == intValue && asDouble() >= minInt &&
                 asDouble() <= maxInt) ||
                (other == uintValue && asDouble() >= 0 &&
                 asDouble() <= maxUInt) ||
                other == realValue || other == stringValue ||
                other == booleanValue;

        case booleanValue:
            return (other == nullValue && asBool() == false) ||
                other == intValue || other == uintValue || other == realValue ||
                other == stringValue || other == booleanValue;

        case stringValue:
            return other == stringValue ||
                (other == nullValue &&
                 (asString() != ""));

        case arrayValue:
            return other == arrayValue ||
                (other == nullValue && jv_.as_array().empty());

        case objectValue:
            return other == objectValue ||
                (other == nullValue && jv_.as_object().empty());

        default:
            JSON_ASSERT_UNREACHABLE;
    }

    return false;  // unreachable;
}

/// Number of values in array or object
Value::UInt
Value::size() const
{
    switch (type_)
    {
        case nullValue:
        case intValue:
        case uintValue:
        case realValue:
        case booleanValue:
        case stringValue:
            return 0;

        case arrayValue:  // size of the array is highest index + 1
//            if (!value_.map_->empty())
//            {
//                ObjectValues::const_iterator itLast = value_.map_->end();
//                --itLast;
//                return (*itLast).first.index() + 1;
//            }
//
//            return 0;
            return jv_.as_array().size();

        case objectValue:
            return jv_.as_object().size();
//            return Int(value_.map_->size());

        default:
            JSON_ASSERT_UNREACHABLE;
    }

    return 0;  // unreachable;
}

Value::operator bool() const
{
    if (isNull())
        return false;

    if (isString())
    {
        auto s = asCString();
        return s && s[0];
    }

    return !(isArray() || isObject()) || size();
}

void
Value::clear()
{
    JSON_ASSERT(
        type_ == nullValue || type_ == arrayValue || type_ == objectValue);

    switch (type_)
    {
        case arrayValue:
            value_.map_->clear();
            jv_.emplace_array();
            break;
        case objectValue:
            value_.map_->clear();
            jv_.emplace_object();
            break;

        default:
            break;
    }
}

// TODO: What is the expected behavior for out-of-bounds array access?

Value&
Value::operator[](UInt index)
{
    JSON_ASSERT(type_ == nullValue || type_ == arrayValue);

    if (type_ == nullValue)
        *this = Value(arrayValue);

//    CZString key(index);
//    ObjectValues::iterator it = value_.map_->lower_bound(key);
//
//    if (it != value_.map_->end() && (*it).first == key)
//        return (*it).second;
//
//    ObjectValues::value_type defaultValue(key, null);
//    it = value_.map_->insert(it, defaultValue);
//    return (*it).second;
    return boostJsonToJV(jv_.as_array()[index]);
}

const Value&
Value::operator[](UInt index) const
{
    JSON_ASSERT(type_ == nullValue || type_ == arrayValue);

//    if (type_ == nullValue)
//        return null;

//    CZString key(index);
//    ObjectValues::const_iterator it = value_.map_->find(key);
//
//    if (it == value_.map_->end())
//        return null;
//
//    return (*it).second;
    return boostJsonToJV(jv_.as_array()[index]);
}

Value&
Value::operator[](const char* key)
{
    return resolveReference(key, false);
}

Value&
Value::resolveReference(const char* key, bool isStatic)
{
    JSON_ASSERT(type_ == nullValue || type_ == objectValue);

    if (type_ == nullValue)
        *this = Value(objectValue);

//    CZString actualKey(
//        key, isStatic ? CZString::noDuplication : CZString::duplicateOnCopy);
//    ObjectValues::iterator it = value_.map_->lower_bound(actualKey);
//
//    if (it != value_.map_->end() && (*it).first == actualKey)
//        return (*it).second;
//
//    ObjectValues::value_type defaultValue(actualKey, null);
//    it = value_.map_->insert(it, defaultValue);
//    Value& value = (*it).second;
//    return value;
    return boostJsonToJV(jv_.as_object()[key]);
}

Value
Value::get(UInt index, const Value& defaultValue) const
{
    const Value* value = &((*this)[index]);
    return value == &null ? defaultValue : *value;
}

bool
Value::isValidIndex(UInt index) const
{
    return index < size();
}

const Value&
Value::operator[](const char* key) const
{
    JSON_ASSERT(type_ == nullValue || type_ == objectValue);

    if (type_ == nullValue)
        return null;

//    CZString actualKey(key, CZString::noDuplication);
//    ObjectValues::const_iterator it = value_.map_->find(actualKey);
//
//    if (it == value_.map_->end())
//        return null;
//
//    return (*it).second;
    return boostJsonToJV(jv_.as_object().at(key));
}

Value&
Value::operator[](std::string const& key)
{
    return (*this)[key.c_str()];
}

const Value&
Value::operator[](std::string const& key) const
{
    return (*this)[key.c_str()];
}

Value&
Value::operator[](const StaticString& key)
{
    return resolveReference(key, true);
}

Value&
Value::append(const Value& value)
{
    return (*this)[size()] = value;
}

Value&
Value::append(Value&& value)
{
    return (*this)[size()] = std::move(value);
}

Value
Value::get(const char* key, const Value& defaultValue) const
{
    const Value* value = &((*this)[key]);
    return value == &null ? defaultValue : *value;
}

Value
Value::get(std::string const& key, const Value& defaultValue) const
{
    return get(key.c_str(), defaultValue);
}

Value
Value::removeMember(const char* key)
{
    JSON_ASSERT(type_ == nullValue || type_ == objectValue);

    if (type_ == nullValue)
        return null;

//    CZString actualKey(key, CZString::noDuplication);
//    ObjectValues::iterator it = value_.map_->find(actualKey);

//    if (it == value_.map_->end())
//        return null;

    Value old(boostJsonToJV(jv_.as_object()[key]));
    jv_.as_object().erase(key);

    return old;
}

Value
Value::removeMember(std::string const& key)
{
    return removeMember(key.c_str());
}

bool
Value::isMember(const char* key) const
{
    if (type_ != objectValue)
        return false;

    const Value* value = &((*this)[key]);
    return value != &null;
}

bool
Value::isMember(std::string const& key) const
{
    return isMember(key.c_str());
}

Value::Members
Value::getMemberNames() const
{
    JSON_ASSERT(type_ == nullValue || type_ == objectValue);

    if (type_ == nullValue)
        return Value::Members();

    Members members;
//    members.reserve(value_.map_->size());
    members.reserve(jv_.as_object().size());
//    ObjectValues::const_iterator it = value_.map_->begin();
//    ObjectValues::const_iterator itEnd = value_.map_->end();


//    for (; it != itEnd; ++it)
//        members.push_back(std::string((*it).first.c_str()));

    boost::json::object::const_iterator it = jv_.as_object().begin();
    boost::json::object::const_iterator itEnd = jv_.as_object().end();

    for(; it != itEnd; it++)
        members.push_back(std::string{it->key()});


    return members;
}

bool
Value::isNull() const
{
    return type_ == nullValue;
}

bool
Value::isBool() const
{
    return type_ == booleanValue;
}

bool
Value::isInt() const
{
    return type_ == intValue;
}

bool
Value::isUInt() const
{
    return type_ == uintValue;
}

bool
Value::isIntegral() const
{
    return type_ == intValue || type_ == uintValue || type_ == booleanValue;
}

bool
Value::isDouble() const
{
    return type_ == realValue;
}

bool
Value::isNumeric() const
{
    return isIntegral() || isDouble();
}

bool
Value::isString() const
{
    return type_ == stringValue;
}

bool
Value::isArray() const
{
    return type_ == arrayValue;
}

bool
Value::isArrayOrNull() const
{
    return type_ == nullValue || type_ == arrayValue;
}

bool
Value::isObject() const
{
    return type_ == objectValue;
}

bool
Value::isObjectOrNull() const
{
    return type_ == nullValue || type_ == objectValue;
}

std::string
Value::toStyledString() const
{
    StyledWriter writer;
    return writer.write(*this);
}

Value::const_iterator
Value::begin() const
{
    switch (type_)
    {
        case arrayValue:
        case objectValue:
            if (value_.map_)
                return const_iterator(value_.map_->begin());

            break;
        default:
            break;
    }

    return const_iterator();
}

Value::const_iterator
Value::end() const
{
    switch (type_)
    {
        case arrayValue:
        case objectValue:
            if (value_.map_)
                return const_iterator(value_.map_->end());

            break;
        default:
            break;
    }

    return const_iterator();
}

Value::iterator
Value::begin()
{
    switch (type_)
    {
        case arrayValue:
        case objectValue:
            if (value_.map_)
                return iterator(value_.map_->begin());
            break;
        default:
            break;
    }

    return iterator();
}

Value::iterator
Value::end()
{
    switch (type_)
    {
        case arrayValue:
        case objectValue:
            if (value_.map_)
                return iterator(value_.map_->end());
            break;
        default:
            break;
    }

    return iterator();
}

}  // namespace Json
