// Copyright 2026 The Beam Team
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

#include <cassert>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>

#include "test_helpers.h"
#include "utility/hex.h"
#include "wallet/transactions/swaps/bridges/ethereum/common.h"

WALLET_TEST_INIT

using namespace beam;

namespace
{
    // 32 bytes: 0x01, 0x02, ..., 0x20
    const std::string kHex64 = "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";

    constexpr uint8_t kGuardByte = 0xa5;

    // Places the converted value between two guard areas, so that a write outside of
    // ECC::uintBig::m_pData is detectable even without sanitizers.
    struct GuardedUintBig
    {
        uint8_t m_Before[16];
        ECC::uintBig m_Value;
        uint8_t m_After[16];

        GuardedUintBig()
            : m_Value(ECC::Zero)
        {
            memset(m_Before, kGuardByte, sizeof(m_Before));
            memset(m_After, kGuardByte, sizeof(m_After));
        }

        void Convert(const std::string& number)
        {
            // Guaranteed copy elision (C++17) + NRVO: the result is built directly in m_Value
            new (&m_Value) ECC::uintBig(ethereum::ConvertStrToUintBig(number));
        }

        bool IsIntact() const
        {
            for (auto b : m_Before)
                if (b != kGuardByte)
                    return false;
            for (auto b : m_After)
                if (b != kGuardByte)
                    return false;
            return true;
        }
    };

    bool HasBytes1To32(const ECC::uintBig& value)
    {
        uint8_t expected[ECC::uintBig::nBytes];
        for (uint32_t i = 0; i < ECC::uintBig::nBytes; ++i)
            expected[i] = static_cast<uint8_t>(i + 1);

        return memcmp(value.m_pData, expected, sizeof(expected)) == 0
            && to_hex(value.m_pData, sizeof(value.m_pData)) == kHex64;
    }

    void TestConvertExactly256Bits()
    {
        {
            GuardedUintBig g;
            g.Convert("0x" + kHex64);
            WALLET_CHECK(g.IsIntact());
            WALLET_CHECK(HasBytes1To32(g.m_Value));
        }
        {
            // ethereum_side passes the value without the prefix
            GuardedUintBig g;
            g.Convert(kHex64);
            WALLET_CHECK(g.IsIntact());
            WALLET_CHECK(HasBytes1To32(g.m_Value));
        }
    }

    void TestConvertLeadingZeros()
    {
        {
            // 33 bytes, the leading one is zero
            GuardedUintBig g;
            g.Convert("0x00" + kHex64);
            WALLET_CHECK(g.IsIntact());
            WALLET_CHECK(HasBytes1To32(g.m_Value));
        }
        {
            // 40 zero bytes
            GuardedUintBig g;
            g.Convert("0x" + std::string(80, '0'));
            WALLET_CHECK(g.IsIntact());
            WALLET_CHECK(g.m_Value == ECC::Zero);
        }
    }

    void TestConvertSmall()
    {
        {
            GuardedUintBig g;
            g.Convert("0x");
            WALLET_CHECK(g.IsIntact());
            WALLET_CHECK(g.m_Value == ECC::Zero);
        }
        {
            GuardedUintBig g;
            g.Convert("0x1");
            WALLET_CHECK(g.IsIntact());
            WALLET_CHECK(g.m_Value == ECC::uintBig(1u));
        }

        const ECC::uintBig kOneEther(1'000'000'000'000'000'000ull);
        WALLET_CHECK(ethereum::ConvertStrToUintBig("0xde0b6b3a7640000") == kOneEther);
        WALLET_CHECK(ethereum::ConvertStrToUintBig("1000000000000000000", false) == kOneEther);
    }

    void CheckTooBig(const std::string& number)
    {
        GuardedUintBig g;
        bool thrown = false;
        try
        {
            g.Convert(number);
        }
        catch (const std::runtime_error&)
        {
            thrown = true;
        }
        WALLET_CHECK(thrown);
        WALLET_CHECK(g.IsIntact());
        WALLET_CHECK(g.m_Value == ECC::Zero);
    }

    void TestConvertTooBig()
    {
        // 40 bytes (80 hex digits)
        CheckTooBig("0x" + kHex64 + "2122232425262728");
        // 33 significant bytes
        CheckTooBig("0x01" + kHex64);
        CheckTooBig("ff" + kHex64);
    }
}

int main()
{
    TestConvertExactly256Bits();
    TestConvertLeadingZeros();
    TestConvertSmall();
    TestConvertTooBig();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
