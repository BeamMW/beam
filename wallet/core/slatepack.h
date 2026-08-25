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

    // The armored body is base58-encoded as one big integer, which does not preserve leading
    // zero bytes. The version byte is the first byte of the body, so a non-zero kVersion keeps
    // the encoding round-trip exact without a zero-run prefix scheme.
    static_assert(kVersion != 0, "kVersion must be non-zero: base58 does not preserve a leading zero byte");

    enum class PayloadType : uint8_t
    {
        TxNegotiation = 1, // an in-flight transaction-negotiation message (P1)
        KeyBundle     = 2, // view key / watch bundle (reserved, P2+)
        ProofOfFunds  = 3, // point-in-time balance attestation (reserved, P4)
    };

    // Why a Slatepack was rejected. Carried as a code rather than a string so the desktop
    // wallet can render a translated message; ErrorToString() is the English fallback used by
    // the CLI and the logs.
    enum class Error : uint8_t
    {
        None = 0,
        NotSlatepack,        // no BEGINSLATEPACK./ENDSLATEPACK. block in the text
        BadEncoding,         // base58 body is not decodable or too short
        BadChecksum,         // body decoded but the error-detection code does not match
        UnsupportedVersion,  // version byte is not kVersion
        UnknownType,         // type byte is outside PayloadType
        BadPayload,          // payload does not deserialize as the declared type
        UnsupportedType,     // well-formed, but this build does not consume that type
        NotForThisWallet,    // none of our addresses could decrypt it
        ReadOnlyWallet,      // wallet has no SBBS key: it cannot decrypt anything
        HandlerAddress,      // targets a raw-handler address, which cannot be previewed
        NoPendingImport,     // Commit()/CancelPending() with no matching Preview()
    };

    const char* ErrorToString(Error);

    // Wrap a payload as BEGINSLATEPACK. <base58 body>. ENDSLATEPACK. — the body is
    // version(1) + type(1) + payload + checksum(4), base58-encoded.
    std::string Armor(PayloadType type, const ByteBuffer& payload);

    // Extract+validate the first BEGINSLATEPACK..ENDSLATEPACK block in text (tolerating
    // surrounding words and reflowed whitespace). On success fills 'type'/'payload' and returns
    // true; on failure returns false and sets 'error'.
    bool Unarmor(const std::string& text, PayloadType& type, ByteBuffer& payload, Error& error);

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
