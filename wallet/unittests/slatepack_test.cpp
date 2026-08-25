// Copyright 2018-2026 The Beam Team
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

#include "wallet/core/slatepack.h"
#include "core/ecc_native.h" // ECC::GenRandom
#include "test_helpers.h"     // WALLET_TEST_INIT / WALLET_CHECK / WALLET_CHECK_RESULT
#include <cstring>

WALLET_TEST_INIT

using namespace beam;
using namespace beam::wallet;
using namespace std;

namespace
{
    ByteBuffer RandomBytes(size_t n)
    {
        ByteBuffer b(n);
        if (n)
            ECC::GenRandom(&b.front(), static_cast<uint32_t>(b.size()));
        return b;
    }

    void TestArmorRoundTrip()
    {
        const ByteBuffer payload = RandomBytes(300);
        const string s = slatepack::Armor(slatepack::PayloadType::TxNegotiation, payload);
        WALLET_CHECK(s.rfind("BEGINSLATEPACK.", 0) == 0);
        WALLET_CHECK(s.find("ENDSLATEPACK.") != string::npos);

        // Survives being pasted inside other text with reflowed whitespace.
        const string wrapped = "hey, here is the tx:\n\n" + s + "\n\nthanks!";
        slatepack::PayloadType type;
        ByteBuffer out;
        slatepack::Error err = slatepack::Error::None;
        WALLET_CHECK(slatepack::Unarmor(wrapped, type, out, err));
        WALLET_CHECK(err == slatepack::Error::None);
        WALLET_CHECK(type == slatepack::PayloadType::TxNegotiation);
        WALLET_CHECK(out == payload);
    }

    void TestArmorRejects()
    {
        const ByteBuffer payload{ 1, 2, 3, 4, 5 };
        const string s = slatepack::Armor(slatepack::PayloadType::TxNegotiation, payload);

        slatepack::PayloadType type;
        ByteBuffer out;
        slatepack::Error err = slatepack::Error::None;

        // Corruption: flip one body char to a different (still valid) base58 char.
        string bad = s;
        const size_t pos = bad.find("BEGINSLATEPACK.") + strlen("BEGINSLATEPACK.") + 2;
        bad[pos] = (bad[pos] == 'A') ? 'B' : 'A';
        WALLET_CHECK(!slatepack::Unarmor(bad, type, out, err));
        WALLET_CHECK(err == slatepack::Error::BadChecksum || err == slatepack::Error::BadEncoding);

        // Truncation: the end marker is gone, so it no longer reads as a Slatepack at all.
        WALLET_CHECK(!slatepack::Unarmor(s.substr(0, s.size() / 2), type, out, err));
        WALLET_CHECK(err == slatepack::Error::NotSlatepack);

        // Not a Slatepack at all.
        WALLET_CHECK(!slatepack::Unarmor("hello world", type, out, err));
        WALLET_CHECK(err == slatepack::Error::NotSlatepack);

        // Empty input.
        WALLET_CHECK(!slatepack::Unarmor("", type, out, err));
        WALLET_CHECK(err == slatepack::Error::NotSlatepack);

        // A non-base58 character inside an otherwise well-formed envelope.
        string illegal = s;
        illegal[illegal.find("BEGINSLATEPACK.") + strlen("BEGINSLATEPACK.") + 2] = '0'; // '0' is not in the alphabet
        WALLET_CHECK(!slatepack::Unarmor(illegal, type, out, err));
        WALLET_CHECK(err == slatepack::Error::BadEncoding);

        // Every rejection reason renders as something printable.
        for (uint8_t i = 0; i <= static_cast<uint8_t>(slatepack::Error::NoPendingImport); ++i)
            WALLET_CHECK(strlen(slatepack::ErrorToString(static_cast<slatepack::Error>(i))) > 0);
    }

    void TestTxNegotiationRoundTrip()
    {
        slatepack::TxNegotiation n;
        n.m_Peer.m_Channel = 12345U;
        ECC::GenRandom(n.m_Peer.m_Pk);
        n.m_Ciphertext = ByteBuffer{ 9, 8, 7, 6, 5, 4, 3, 2, 1 };

        const ByteBuffer bytes = slatepack::ToBytes(n);
        slatepack::TxNegotiation r;
        WALLET_CHECK(slatepack::FromBytes(r, bytes));
        WALLET_CHECK(r.m_Peer.m_Channel == n.m_Peer.m_Channel);
        WALLET_CHECK(r.m_Peer.m_Pk == n.m_Peer.m_Pk);
        WALLET_CHECK(r.m_Ciphertext == n.m_Ciphertext);

        // And it survives a full armor round-trip as a TxNegotiation payload.
        const string s = slatepack::Armor(slatepack::PayloadType::TxNegotiation, bytes);
        slatepack::PayloadType type;
        ByteBuffer out;
        slatepack::Error err = slatepack::Error::None;
        WALLET_CHECK(slatepack::Unarmor(s, type, out, err));
        slatepack::TxNegotiation r2;
        WALLET_CHECK(slatepack::FromBytes(r2, out));
        WALLET_CHECK(r2.m_Peer.m_Pk == n.m_Peer.m_Pk);
        WALLET_CHECK(r2.m_Ciphertext == n.m_Ciphertext);
    }
}

int main()
{
    TestArmorRoundTrip();
    TestArmorRejects();
    TestTxNegotiationRoundTrip();
    return WALLET_CHECK_RESULT;
}
