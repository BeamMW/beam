// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <boost/serialization/strong_typedef.hpp>
#include <utility/hex.h>

namespace beam::wallet
{
    using json = nlohmann::json;
    using JsonRpcId = json;

    template<typename T>
    const char* type_name();

    template<typename T>
    bool type_check(const json&);

    template<typename T>
    T type_get(const json&);

    template<>
    inline const char* type_name<bool>()
    {
        return "bool";
    }

    template<>
    inline bool type_check<bool>(const json& j)
    {
        return j.is_boolean();
    }

    template<>
    inline bool type_get<bool>(const json& j)
    {
        return j.get<bool>();
    }

    template<>
    inline const char* type_name<std::string>()
    {
        return "string";
    }

    template<>
    inline bool type_check<std::string>(const json& j)
    {
        return j.is_string();
    }

    template<>
    inline std::string type_get<std::string>(const json& j)
    {
        return j.get<std::string>();
    }

    template<>
    inline const char* type_name<uint32_t>()
    {
        return "32bit unsigned integer";
    }

    template<>
    inline bool type_check<uint32_t>(const json& j)
    {
        return j.is_number_unsigned() &&
               j.get<uint32_t>() == j.get<uint64_t>(); // json lib ignores 32bit overflow on cast
    }

    template<>
    inline uint32_t type_get<uint32_t>(const json& j)
    {
        return j.get<uint32_t>();
    }

    template<>
    inline const char* type_name<uint64_t>()
    {
        return "64bit unsigned integer";
    }

    template<>
    inline bool type_check<uint64_t>(const json& j)
    {
        return j.is_number_unsigned();
    }

    template<>
    inline uint64_t type_get<uint64_t>(const json& j)
    {
        return j.get<uint64_t>();
    }

    BOOST_STRONG_TYPEDEF(uint32_t, PositiveUint32)

    template<>
    inline const char* type_name<PositiveUint32>()
    {
        return "positive 32bit unsigned integer";
    }

    template<>
    inline bool type_check<PositiveUint32>(const json& j)
    {
        return type_check<uint32_t>(j) && type_get<uint32_t>(j) > 0;
    }

    template<>
    inline PositiveUint32 type_get<PositiveUint32>(const json& j)
    {
        return PositiveUint32(type_get<uint32_t>(j));
    }

    BOOST_STRONG_TYPEDEF(uint64_t, PositiveUint64)

    template<>
    inline const char* type_name<PositiveUint64>()
    {
        return "positive 64bit unsigned integer";
    }

    template<>
    inline bool type_check<PositiveUint64>(const json& j)
    {
        return type_check<uint64_t>(j) && type_get<uint64_t>(j) > 0;
    }

    template<>
    inline PositiveUint64 type_get<PositiveUint64>(const json& j)
    {
        return PositiveUint64(type_get<uint64_t>(j));
    }

    BOOST_STRONG_TYPEDEF(Amount, PositiveAmount)

    template<>
    inline const char* type_name<PositiveAmount>()
    {
        return type_name<PositiveUint64>();
    }

    template<>
    inline bool type_check<PositiveAmount>(const json& j)
    {
        return type_check<PositiveUint64>(j);
    }

    template<>
    inline PositiveAmount type_get<PositiveAmount>(const json& j)
    {
        return PositiveAmount(type_get<PositiveUint64>(j));
    }

    BOOST_STRONG_TYPEDEF(Height, PositiveHeight)

    template<>
    inline const char* type_name<PositiveHeight>()
    {
        return type_name<PositiveUint64>();
    }

    template<>
    inline bool type_check<PositiveHeight>(const json& j)
    {
        return type_check<PositiveUint64>(j);
    }

    template<>
    inline PositiveHeight type_get<PositiveHeight>(const json& j)
    {
        return PositiveHeight(type_get<PositiveUint64>(j));
    }

    BOOST_STRONG_TYPEDEF(std::string, NonEmptyString)

    template<>
    inline const char* type_name<NonEmptyString>()
    {
        return "non-empty string";
    }

    template<>
    inline bool type_check<NonEmptyString>(const json& j)
    {
        return type_check<std::string>(j) && !type_get<std::string>(j).empty();
    }

    template<>
    inline NonEmptyString type_get<NonEmptyString>(const json& j)
    {
        return NonEmptyString(type_get<std::string>(j));
    }

    BOOST_STRONG_TYPEDEF(TxID, ValidTxID)

    template<>
    inline const char* type_name<ValidTxID>()
    {
        return "valid transaction id";
    }

    template<>
    inline bool type_check<ValidTxID>(const json& j)
    {
        if(!type_check<NonEmptyString>(j))
        {
            return false;
        }

        bool isValid = false;
        const auto txid = beam::from_hex(type_get<std::string>(j), &isValid);
        return isValid && txid.size() == TxID().size();
    }

    template<>
    inline ValidTxID type_get<ValidTxID>(const json& j)
    {
        const auto txid = beam::from_hex(type_get<std::string>(j));

        TxID result;
        std::copy_n(txid.begin(), TxID().size(), result.begin());

        return ValidTxID(result);
    }

    BOOST_STRONG_TYPEDEF(ByteBuffer, ValidHexBuffer)

    template<>
    inline const char* type_name<ValidHexBuffer>()
    {
        return "valid hex encoded byte buffer";
    }

    template<>
    inline bool type_check<ValidHexBuffer>(const json& j)
    {
        if (!type_check<NonEmptyString>(j))
        {
            return false;
        }

        bool isValid = false;
        const auto buf = beam::from_hex(type_get<std::string>(j), &isValid);
        return isValid && !buf.empty();
    }

    template<>
    inline ValidHexBuffer type_get<ValidHexBuffer>(const json& j)
    {
        return ValidHexBuffer(beam::from_hex(type_get<std::string>(j)));
    }

    BOOST_STRONG_TYPEDEF(json, JsonArray)
    inline void to_json(json& j, const JsonArray& p) {
        j = p.t;
    }

    template<>
    inline const char* type_name<JsonArray>()
    {
        return "array";
    }

    template<>
    inline bool type_check<JsonArray>(const json& j)
    {
        return j.is_array();
    }

    template<>
    inline JsonArray type_get<JsonArray>(const json& j)
    {
        return JsonArray(j);
    }

    BOOST_STRONG_TYPEDEF(json, NonEmptyJsonArray)
    inline void to_json(json& j, const NonEmptyJsonArray& p) {
        j = p.t;
    }

    template<>
    inline const char* type_name<NonEmptyJsonArray>()
    {
        return "non-empty array";
    }

    template<>
    inline bool type_check<NonEmptyJsonArray>(const json& j)
    {
        return type_check<JsonArray>(j) && !j.empty();
    }

    template<>
    inline NonEmptyJsonArray type_get<NonEmptyJsonArray>(const json& j)
    {
        return NonEmptyJsonArray(j);
    }
}
