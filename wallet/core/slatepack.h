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

#pragma once

#include "wallet/core/common.h" // WalletID, ByteBuffer
#include <string>

// Slatepack: an armored, copy-pastable envelope for node-independent transport of wallet
// data — base58 framing + a checksum and version/type discriminators over a binary payload.
namespace beam::wallet::slatepack
{
    static const uint8_t kVersion = 1;

    enum class PayloadType : uint8_t
    {
        TxNegotiation = 1, // an in-flight transaction-negotiation message (P1)
        KeyBundle     = 2, // view key / watch bundle (reserved, P2+)
        ProofOfFunds  = 3, // point-in-time balance attestation (reserved, P4)
    };

    // Wrap a payload as BEGINSLATEPACK. <base58 body>. ENDSLATEPACK. — the body is
    // version(1) + type(1) + payload + checksum(4), base58-encoded.
    std::string Armor(PayloadType type, const ByteBuffer& payload);

    // Extract+validate the first BEGINSLATEPACK..ENDSLATEPACK block in text (tolerating
    // surrounding words and reflowed whitespace). On success fills 'type'/'payload' and returns
    // true; on failure returns false with a short user-facing reason in 'error'.
    bool Unarmor(const std::string& text, PayloadType& type, ByteBuffer& payload, std::string& error);

    // A queued negotiation message routed manually instead of over SBBS: peer WalletID plus the
    // already SBBS-encrypted body (armor adds no crypto — a public Slatepack reveals only type/size).
    struct TxNegotiation
    {
        WalletID m_Peer;
        ByteBuffer m_Ciphertext;

        template <typename Archive>
        void serialize(Archive& ar)
        {
            ar & m_Peer & m_Ciphertext;
        }
    };

    ByteBuffer ToBytes(const TxNegotiation&);
    bool FromBytes(TxNegotiation&, const ByteBuffer&);
}
