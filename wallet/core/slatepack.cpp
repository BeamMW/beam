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
#include "wallet/core/base58.h"
#include "core/ecc_native.h" // ECC::Hash, Blob
#include "utility/serialize.h"
#include <cstring>

namespace beam::wallet::slatepack
{
    namespace
    {
        const char kBegin[] = "BEGINSLATEPACK.";
        const char kEnd[]   = "ENDSLATEPACK.";
        const size_t kWordLen = 5;
        const size_t kWordsPerLine = 12;
        const size_t kHeaderSize = 2; // version + type
        const size_t kChecksumSize = 4;

        // 4-byte error-detection code: leading bytes of double-SHA256 over 'data'.
        void ComputeChecksum(const ByteBuffer& data, uint8_t out[kChecksumSize])
        {
            ECC::Hash::Value hv;
            ECC::Hash::Processor() << Blob(data.data(), static_cast<uint32_t>(data.size())) >> hv;
            ECC::Hash::Processor() << hv >> hv;
            std::memcpy(out, hv.m_pData, kChecksumSize);
        }
    }

    std::string Armor(PayloadType type, const ByteBuffer& payload)
    {
        ByteBuffer body;
        body.reserve(kHeaderSize + payload.size() + kChecksumSize);
        body.push_back(kVersion);
        body.push_back(static_cast<uint8_t>(type));
        body.insert(body.end(), payload.begin(), payload.end());

        uint8_t cs[kChecksumSize];
        ComputeChecksum(body, cs);
        body.insert(body.end(), cs, cs + kChecksumSize);

        const std::string b58 = EncodeToBase58(body);

        std::string out(kBegin);
        out += ' ';
        size_t words = 0;
        for (size_t i = 0; i < b58.size(); i += kWordLen)
        {
            out += b58.substr(i, kWordLen);
            out += (++words % kWordsPerLine == 0) ? '\n' : ' ';
        }
        if (!out.empty() && (out.back() == ' ' || out.back() == '\n'))
            out.pop_back();
        out += ". ";
        out += kEnd;
        return out;
    }

    bool Unarmor(const std::string& text, PayloadType& type, ByteBuffer& payload, std::string& error)
    {
        const size_t begin = text.find(kBegin);
        if (begin == std::string::npos) { error = "not a Slatepack"; return false; }
        const size_t bodyStart = begin + std::strlen(kBegin);
        const size_t end = text.find(kEnd, bodyStart);
        if (end == std::string::npos) { error = "not a Slatepack"; return false; }

        // Reassemble the base58 body, dropping the whitespace and '.' separators the armor
        // inserted for readability.
        std::string b58;
        b58.reserve(end - bodyStart);
        for (size_t i = bodyStart; i < end; ++i)
        {
            const char c = text[i];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '.') continue;
            b58 += c;
        }

        const ByteBuffer body = DecodeBase58(b58);
        if (body.size() < kHeaderSize + kChecksumSize) { error = "damaged Slatepack (encoding)"; return false; }

        const ByteBuffer data(body.begin(), body.end() - kChecksumSize);
        uint8_t cs[kChecksumSize];
        ComputeChecksum(data, cs);
        if (std::memcmp(cs, body.data() + body.size() - kChecksumSize, kChecksumSize) != 0)
        {
            error = "damaged Slatepack (checksum)";
            return false;
        }

        if (data[0] != kVersion) { error = "Slatepack is from a newer wallet version"; return false; }
        if (data[1] < static_cast<uint8_t>(PayloadType::TxNegotiation) ||
            data[1] > static_cast<uint8_t>(PayloadType::ProofOfFunds))
        {
            error = "unknown Slatepack type";
            return false;
        }

        type = static_cast<PayloadType>(data[1]);
        payload.assign(data.begin() + kHeaderSize, data.end());
        return true;
    }

    ByteBuffer ToBytes(const TxNegotiation& n)
    {
        Serializer ser;
        ser & n;
        const SerializeBuffer sb = ser.buffer();
        const uint8_t* p = reinterpret_cast<const uint8_t*>(sb.first);
        return ByteBuffer(p, p + sb.second);
    }

    bool FromBytes(TxNegotiation& n, const ByteBuffer& b)
    {
        try
        {
            Deserializer der;
            der.reset(b.data(), b.size());
            der & n;
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
}
