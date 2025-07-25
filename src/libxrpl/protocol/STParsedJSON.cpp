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

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/detail/STVar.h>

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace ripple {

namespace STParsedJSONDetail {
template <typename U, typename S>
constexpr std::
    enable_if_t<std::is_unsigned<U>::value && std::is_signed<S>::value, U>
    to_unsigned(S value)
{
    if (value < 0 || std::numeric_limits<U>::max() < value)
        Throw<std::runtime_error>("Value out of range");
    return static_cast<U>(value);
}

template <typename U1, typename U2>
constexpr std::
    enable_if_t<std::is_unsigned<U1>::value && std::is_unsigned<U2>::value, U1>
    to_unsigned(U2 value)
{
    if (std::numeric_limits<U1>::max() < value)
        Throw<std::runtime_error>("Value out of range");
    return static_cast<U1>(value);
}

static std::string
make_name(std::string const& object, std::string const& field)
{
    if (field.empty())
        return object;

    return object + "." + field;
}

static Json::Value
not_an_object(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' is not a JSON object.");
}

static Json::Value
not_an_object(std::string const& object)
{
    return not_an_object(object, "");
}

static Json::Value
not_an_array(std::string const& object)
{
    return RPC::make_error(
        rpcINVALID_PARAMS, "Field '" + object + "' is not a JSON array.");
}

static Json::Value
unknown_field(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' is unknown.");
}

static Json::Value
out_of_range(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' is out of range.");
}

static Json::Value
bad_type(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' has bad type.");
}

static Json::Value
invalid_data(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' has invalid data.");
}

static Json::Value
invalid_data(std::string const& object)
{
    return invalid_data(object, "");
}

static Json::Value
array_expected(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' must be a JSON array.");
}

static Json::Value
string_expected(std::string const& object, std::string const& field)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + make_name(object, field) + "' must be a string.");
}

static Json::Value
too_deep(std::string const& object)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + object + "' exceeds nesting depth limit.");
}

static Json::Value
singleton_expected(std::string const& object, unsigned int index)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Field '" + object + "[" + std::to_string(index) +
            "]' must be an object with a single key/object value.");
}

static Json::Value
template_mismatch(SField const& sField)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Object '" + sField.getName() +
            "' contents did not meet requirements for that type.");
}

static Json::Value
non_object_in_array(std::string const& item, Json::UInt index)
{
    return RPC::make_error(
        rpcINVALID_PARAMS,
        "Item '" + item + "' at index " + std::to_string(index) +
            " is not an object.  Arrays may only contain objects.");
}

// This function is used by parseObject to parse any JSON type that doesn't
// recurse.  Everything represented here is a leaf-type.
static std::optional<detail::STVar>
parseLeaf(
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    std::optional<detail::STVar> ret;

    auto const& field = SField::getField(fieldName);

    if (field == sfInvalid)
    {
        error = unknown_field(json_name, fieldName);
        return ret;
    }

    switch (field.fieldType)
    {
        case STI_UINT8:
            try
            {
                constexpr auto minValue =
                    std::numeric_limits<std::uint8_t>::min();
                constexpr auto maxValue =
                    std::numeric_limits<std::uint8_t>::max();
                if (value.isString())
                {
                    std::string const strValue = value.asString();

                    if (!strValue.empty() &&
                        ((strValue[0] < '0') || (strValue[0] > '9')))
                    {
                        if (field == sfTransactionResult)
                        {
                            auto ter = transCode(strValue);

                            if (!ter || TERtoInt(*ter) < minValue ||
                                TERtoInt(*ter) > maxValue)
                            {
                                error = out_of_range(json_name, fieldName);
                                return ret;
                            }

                            ret = detail::make_stvar<STUInt8>(
                                field,
                                static_cast<std::uint8_t>(TERtoInt(*ter)));
                        }
                        else
                        {
                            error = bad_type(json_name, fieldName);
                            return ret;
                        }
                    }
                    else
                    {
                        ret = detail::make_stvar<STUInt8>(
                            field,
                            beast::lexicalCastThrow<std::uint8_t>(strValue));
                    }
                }
                else if (value.isInt())
                {
                    if (value.asInt() < minValue || value.asInt() > maxValue)
                    {
                        error = out_of_range(json_name, fieldName);
                        return ret;
                    }

                    ret = detail::make_stvar<STUInt8>(
                        field, static_cast<std::uint8_t>(value.asInt()));
                }
                else if (value.isUInt())
                {
                    if (value.asUInt() > maxValue)
                    {
                        error = out_of_range(json_name, fieldName);
                        return ret;
                    }

                    ret = detail::make_stvar<STUInt8>(
                        field, static_cast<std::uint8_t>(value.asUInt()));
                }
                else
                {
                    error = bad_type(json_name, fieldName);
                    return ret;
                }
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }
            break;

        case STI_UINT16:
            try
            {
                if (value.isString())
                {
                    std::string const strValue = value.asString();

                    if (!strValue.empty() &&
                        ((strValue[0] < '0') || (strValue[0] > '9')))
                    {
                        if (field == sfTransactionType)
                        {
                            ret = detail::make_stvar<STUInt16>(
                                field,
                                static_cast<std::uint16_t>(
                                    TxFormats::getInstance().findTypeByName(
                                        strValue)));

                            if (*name == sfGeneric)
                                name = &sfTransaction;
                        }
                        else if (field == sfLedgerEntryType)
                        {
                            ret = detail::make_stvar<STUInt16>(
                                field,
                                static_cast<std::uint16_t>(
                                    LedgerFormats::getInstance().findTypeByName(
                                        strValue)));

                            if (*name == sfGeneric)
                                name = &sfLedgerEntry;
                        }
                        else
                        {
                            error = invalid_data(json_name, fieldName);
                            return ret;
                        }
                    }
                    else
                    {
                        ret = detail::make_stvar<STUInt16>(
                            field,
                            beast::lexicalCastThrow<std::uint16_t>(strValue));
                    }
                }
                else if (value.isInt())
                {
                    ret = detail::make_stvar<STUInt16>(
                        field, to_unsigned<std::uint16_t>(value.asInt()));
                }
                else if (value.isUInt())
                {
                    ret = detail::make_stvar<STUInt16>(
                        field, to_unsigned<std::uint16_t>(value.asUInt()));
                }
                else
                {
                    error = bad_type(json_name, fieldName);
                    return ret;
                }
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }

            break;

        case STI_UINT32:
            try
            {
                if (value.isString())
                {
                    if (field == sfPermissionValue)
                    {
                        std::string const strValue = value.asString();
                        auto const granularPermission =
                            Permission::getInstance().getGranularValue(
                                strValue);
                        if (granularPermission)
                        {
                            ret = detail::make_stvar<STUInt32>(
                                field, *granularPermission);
                        }
                        else
                        {
                            auto const& txType =
                                TxFormats::getInstance().findTypeByName(
                                    strValue);
                            ret = detail::make_stvar<STUInt32>(
                                field,
                                Permission::getInstance().txToPermissionType(
                                    txType));
                        }
                    }
                    else
                    {
                        ret = detail::make_stvar<STUInt32>(
                            field,
                            beast::lexicalCastThrow<std::uint32_t>(
                                value.asString()));
                    }
                }
                else if (value.isInt())
                {
                    ret = detail::make_stvar<STUInt32>(
                        field, to_unsigned<std::uint32_t>(value.asInt()));
                }
                else if (value.isUInt())
                {
                    ret = detail::make_stvar<STUInt32>(
                        field, safe_cast<std::uint32_t>(value.asUInt()));
                }
                else
                {
                    error = bad_type(json_name, fieldName);
                    return ret;
                }
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }

            break;

        case STI_UINT64:
            try
            {
                if (value.isString())
                {
                    auto const str = value.asString();

                    std::uint64_t val;

                    bool const useBase10 =
                        field.shouldMeta(SField::sMD_BaseTen);

                    // if the field is amount, serialize as base 10
                    auto [p, ec] = std::from_chars(
                        str.data(),
                        str.data() + str.size(),
                        val,
                        useBase10 ? 10 : 16);

                    if (ec != std::errc() || (p != str.data() + str.size()))
                        Throw<std::invalid_argument>("invalid data");

                    ret = detail::make_stvar<STUInt64>(field, val);
                }
                else if (value.isInt())
                {
                    ret = detail::make_stvar<STUInt64>(
                        field, to_unsigned<std::uint64_t>(value.asInt()));
                }
                else if (value.isUInt())
                {
                    ret = detail::make_stvar<STUInt64>(
                        field, safe_cast<std::uint64_t>(value.asUInt()));
                }
                else
                {
                    error = bad_type(json_name, fieldName);
                    return ret;
                }
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }

            break;

        case STI_UINT128: {
            if (!value.isString())
            {
                error = bad_type(json_name, fieldName);
                return ret;
            }

            uint128 num;

            if (auto const s = value.asString(); !num.parseHex(s))
            {
                if (!s.empty())
                {
                    error = invalid_data(json_name, fieldName);
                    return ret;
                }

                num.zero();
            }

            ret = detail::make_stvar<STUInt128>(field, num);
            break;
        }

        case STI_UINT192: {
            if (!value.isString())
            {
                error = bad_type(json_name, fieldName);
                return ret;
            }

            uint192 num;

            if (auto const s = value.asString(); !num.parseHex(s))
            {
                if (!s.empty())
                {
                    error = invalid_data(json_name, fieldName);
                    return ret;
                }

                num.zero();
            }

            ret = detail::make_stvar<STUInt192>(field, num);
            break;
        }

        case STI_UINT160: {
            if (!value.isString())
            {
                error = bad_type(json_name, fieldName);
                return ret;
            }

            uint160 num;

            if (auto const s = value.asString(); !num.parseHex(s))
            {
                if (!s.empty())
                {
                    error = invalid_data(json_name, fieldName);
                    return ret;
                }

                num.zero();
            }

            ret = detail::make_stvar<STUInt160>(field, num);
            break;
        }

        case STI_UINT256: {
            if (!value.isString())
            {
                error = bad_type(json_name, fieldName);
                return ret;
            }

            uint256 num;

            if (auto const s = value.asString(); !num.parseHex(s))
            {
                if (!s.empty())
                {
                    error = invalid_data(json_name, fieldName);
                    return ret;
                }

                num.zero();
            }

            ret = detail::make_stvar<STUInt256>(field, num);
            break;
        }

        case STI_VL:
            if (!value.isString())
            {
                error = bad_type(json_name, fieldName);
                return ret;
            }

            try
            {
                if (auto vBlob = strUnHex(value.asString()))
                {
                    ret = detail::make_stvar<STBlob>(
                        field, vBlob->data(), vBlob->size());
                }
                else
                {
                    Throw<std::invalid_argument>("invalid data");
                }
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }

            break;

        case STI_AMOUNT:
            try
            {
                ret =
                    detail::make_stvar<STAmount>(amountFromJson(field, value));
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }

            break;

        case STI_NUMBER:
            try
            {
                ret =
                    detail::make_stvar<STNumber>(numberFromJson(field, value));
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }

            break;

        case STI_VECTOR256:
            if (!value.isArrayOrNull())
            {
                error = array_expected(json_name, fieldName);
                return ret;
            }

            try
            {
                STVector256 tail(field);
                for (Json::UInt i = 0; value.isValidIndex(i); ++i)
                {
                    uint256 s;
                    if (!s.parseHex(value[i].asString()))
                        Throw<std::invalid_argument>("invalid data");
                    tail.push_back(s);
                }
                ret = detail::make_stvar<STVector256>(std::move(tail));
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }

            break;

        case STI_PATHSET:
            if (!value.isArrayOrNull())
            {
                error = array_expected(json_name, fieldName);
                return ret;
            }

            try
            {
                STPathSet tail(field);

                for (Json::UInt i = 0; value.isValidIndex(i); ++i)
                {
                    STPath p;

                    if (!value[i].isArrayOrNull())
                    {
                        std::stringstream ss;
                        ss << fieldName << "[" << i << "]";
                        error = array_expected(json_name, ss.str());
                        return ret;
                    }

                    for (Json::UInt j = 0; value[i].isValidIndex(j); ++j)
                    {
                        std::stringstream ss;
                        ss << fieldName << "[" << i << "][" << j << "]";
                        std::string const element_name(
                            json_name + "." + ss.str());

                        // each element in this path has some combination of
                        // account, currency, or issuer

                        Json::Value pathEl = value[i][j];

                        if (!pathEl.isObject())
                        {
                            error = not_an_object(element_name);
                            return ret;
                        }

                        Json::Value const& account = pathEl["account"];
                        Json::Value const& currency = pathEl["currency"];
                        Json::Value const& issuer = pathEl["issuer"];
                        bool hasCurrency = false;
                        AccountID uAccount, uIssuer;
                        Currency uCurrency;

                        if (account)
                        {
                            // human account id
                            if (!account.isString())
                            {
                                error =
                                    string_expected(element_name, "account");
                                return ret;
                            }

                            // If we have what looks like a 160-bit hex value,
                            // we set it, otherwise, we assume it's an AccountID
                            if (!uAccount.parseHex(account.asString()))
                            {
                                auto const a =
                                    parseBase58<AccountID>(account.asString());
                                if (!a)
                                {
                                    error =
                                        invalid_data(element_name, "account");
                                    return ret;
                                }
                                uAccount = *a;
                            }
                        }

                        if (currency)
                        {
                            // human currency
                            if (!currency.isString())
                            {
                                error =
                                    string_expected(element_name, "currency");
                                return ret;
                            }

                            hasCurrency = true;

                            if (!uCurrency.parseHex(currency.asString()))
                            {
                                if (!to_currency(
                                        uCurrency, currency.asString()))
                                {
                                    error =
                                        invalid_data(element_name, "currency");
                                    return ret;
                                }
                            }
                        }

                        if (issuer)
                        {
                            // human account id
                            if (!issuer.isString())
                            {
                                error = string_expected(element_name, "issuer");
                                return ret;
                            }

                            if (!uIssuer.parseHex(issuer.asString()))
                            {
                                auto const a =
                                    parseBase58<AccountID>(issuer.asString());
                                if (!a)
                                {
                                    error =
                                        invalid_data(element_name, "issuer");
                                    return ret;
                                }
                                uIssuer = *a;
                            }
                        }

                        p.emplace_back(
                            uAccount, uCurrency, uIssuer, hasCurrency);
                    }

                    tail.push_back(p);
                }
                ret = detail::make_stvar<STPathSet>(std::move(tail));
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }

            break;

        case STI_ACCOUNT: {
            if (!value.isString())
            {
                error = bad_type(json_name, fieldName);
                return ret;
            }

            std::string const strValue = value.asString();

            try
            {
                if (AccountID account; account.parseHex(strValue))
                    return detail::make_stvar<STAccount>(field, account);

                if (auto result = parseBase58<AccountID>(strValue))
                    return detail::make_stvar<STAccount>(field, *result);

                error = invalid_data(json_name, fieldName);
                return ret;
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }
        }
        break;

        case STI_ISSUE:
            try
            {
                ret = detail::make_stvar<STIssue>(issueFromJson(field, value));
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }
            break;

        case STI_XCHAIN_BRIDGE:
            try
            {
                ret = detail::make_stvar<STXChainBridge>(
                    STXChainBridge(field, value));
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }
            break;

        case STI_CURRENCY:
            try
            {
                ret = detail::make_stvar<STCurrency>(
                    currencyFromJson(field, value));
            }
            catch (std::exception const&)
            {
                error = invalid_data(json_name, fieldName);
                return ret;
            }
            break;

        default:
            error = bad_type(json_name, fieldName);
            return ret;
    }

    return ret;
}

static int const maxDepth = 64;

// Forward declaration since parseObject() and parseArray() call each other.
static std::optional<detail::STVar>
parseArray(
    std::string const& json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error);

static std::optional<STObject>
parseObject(
    std::string const& json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error)
{
    if (!json.isObjectOrNull())
    {
        error = not_an_object(json_name);
        return std::nullopt;
    }

    if (depth > maxDepth)
    {
        error = too_deep(json_name);
        return std::nullopt;
    }

    try
    {
        STObject data(inName);

        for (auto const& fieldName : json.getMemberNames())
        {
            Json::Value const& value = json[fieldName];

            auto const& field = SField::getField(fieldName);

            if (field == sfInvalid)
            {
                error = unknown_field(json_name, fieldName);
                return std::nullopt;
            }

            switch (field.fieldType)
            {
                // Object-style containers (which recurse).
                case STI_OBJECT:
                case STI_TRANSACTION:
                case STI_LEDGERENTRY:
                case STI_VALIDATION:
                    if (!value.isObject())
                    {
                        error = not_an_object(json_name, fieldName);
                        return std::nullopt;
                    }

                    try
                    {
                        auto ret = parseObject(
                            json_name + "." + fieldName,
                            value,
                            field,
                            depth + 1,
                            error);
                        if (!ret)
                            return std::nullopt;
                        data.emplace_back(std::move(*ret));
                    }
                    catch (std::exception const&)
                    {
                        error = invalid_data(json_name, fieldName);
                        return std::nullopt;
                    }

                    break;

                // Array-style containers (which recurse).
                case STI_ARRAY:
                    try
                    {
                        auto array = parseArray(
                            json_name + "." + fieldName,
                            value,
                            field,
                            depth + 1,
                            error);
                        if (!array.has_value())
                            return std::nullopt;
                        data.emplace_back(std::move(*array));
                    }
                    catch (std::exception const&)
                    {
                        error = invalid_data(json_name, fieldName);
                        return std::nullopt;
                    }

                    break;

                // Everything else (types that don't recurse).
                default: {
                    auto leaf =
                        parseLeaf(json_name, fieldName, &inName, value, error);

                    if (!leaf)
                        return std::nullopt;

                    data.emplace_back(std::move(*leaf));
                }

                break;
            }
        }

        // Some inner object types have templates.  Attempt to apply that.
        data.applyTemplateFromSField(inName);  // May throw

        return data;
    }
    catch (STObject::FieldErr const& e)
    {
        std::cerr << "template_mismatch: " << e.what() << "\n";
        error = template_mismatch(inName);
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name);
    }
    return std::nullopt;
}

static std::optional<detail::STVar>
parseArray(
    std::string const& json_name,
    Json::Value const& json,
    SField const& inName,
    int depth,
    Json::Value& error)
{
    if (!json.isArrayOrNull())
    {
        error = not_an_array(json_name);
        return std::nullopt;
    }

    if (depth > maxDepth)
    {
        error = too_deep(json_name);
        return std::nullopt;
    }

    try
    {
        STArray tail(inName);

        for (Json::UInt i = 0; json.isValidIndex(i); ++i)
        {
            bool const isObjectOrNull(json[i].isObjectOrNull());
            bool const singleKey(isObjectOrNull ? json[i].size() == 1 : true);

            if (!isObjectOrNull || !singleKey)
            {
                // null values are !singleKey
                error = singleton_expected(json_name, i);
                return std::nullopt;
            }

            // TODO: There doesn't seem to be a nice way to get just the
            // first/only key in an object without copying all keys into
            // a vector
            std::string const objectName(json[i].getMemberNames()[0]);
            ;
            auto const& nameField(SField::getField(objectName));

            if (nameField == sfInvalid)
            {
                error = unknown_field(json_name, objectName);
                return std::nullopt;
            }

            Json::Value const objectFields(json[i][objectName]);

            std::stringstream ss;
            ss << json_name << "."
               << "[" << i << "]." << objectName;

            auto ret = parseObject(
                ss.str(), objectFields, nameField, depth + 1, error);
            if (!ret)
            {
                std::string errMsg = error["error_message"].asString();
                error["error_message"] =
                    "Error at '" + ss.str() + "'. " + errMsg;
                return std::nullopt;
            }

            if (ret->getFName().fieldType != STI_OBJECT)
            {
                ss << "Field type: " << ret->getFName().fieldType << " ";
                error = non_object_in_array(ss.str(), i);
                return std::nullopt;
            }

            tail.push_back(std::move(*ret));
        }

        return detail::make_stvar<STArray>(std::move(tail));
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name);
        return std::nullopt;
    }
}

}  // namespace STParsedJSONDetail

//------------------------------------------------------------------------------

STParsedJSONObject::STParsedJSONObject(
    std::string const& name,
    Json::Value const& json)
{
    using namespace STParsedJSONDetail;
    object = parseObject(name, json, sfGeneric, 0, error);
}

//------------------------------------------------------------------------------

STParsedJSONArray::STParsedJSONArray(
    std::string const& name,
    Json::Value const& json)
{
    using namespace STParsedJSONDetail;
    auto arr = parseArray(name, json, sfGeneric, 0, error);
    if (!arr)
        array.reset();
    else
    {
        auto p = dynamic_cast<STArray*>(&arr->get());
        if (p == nullptr)
            array.reset();
        else
            array = std::move(*p);
    }
}

}  // namespace ripple
