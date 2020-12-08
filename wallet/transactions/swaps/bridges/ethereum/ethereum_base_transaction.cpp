// Copyright 2020 The Beam Team
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

#include "ethereum_base_transaction.h"

#include <ethash/keccak.hpp>


namespace
{
    template <class T>
    uint8_t BytesRequired(T value)
    {
        static_assert(!std::numeric_limits<T>::is_signed, "only unsigned types supported");
        uint8_t i = 0;
        for (; value != 0; ++i, value >>= 8)
        {
        }
        return i;
    }

    uint8_t BytesRequired(ECC::uintBig input)
    {
        uint8_t n = 0;
        for (; input != ECC::Zero; ++n, input.ShiftRight(8, input))
        {
        }
        return n;
    }

    class RLPStream
    {
    public:
        RLPStream() {}
        ~RLPStream() {}

        // Append given datum to the byte stream.
        RLPStream& append(unsigned value) { return append(ECC::uintBig(value)); }
        RLPStream& append(const ECC::uintBig& value);
        RLPStream& append(const beam::ByteBuffer& value);
        RLPStream& append(const libbitcoin::short_hash& value);

        // Shift operators for appending data items.
        template <class T> RLPStream& operator<<(T _data) { return append(_data); }

        // return finallized list(compute and add full size of list before data) 
        beam::ByteBuffer out() const;

        // Clear the output stream.
        void clear() { m_out.clear(); }

    private:
        static const uint8_t kRLPDataLenStart = 0x80;

        beam::ByteBuffer EncodeLength(size_t size, uint8_t offset = kRLPDataLenStart) const;

        beam::ByteBuffer m_out;
    };

    RLPStream& RLPStream::append(const ECC::uintBig& value)
    {
        if (value == ECC::Zero)
        {
            m_out.push_back(0x80);
        }
        else
        {
            auto bytesRequired = BytesRequired(value);

            // For a single byte whose value is in the [0x00, 0x7f] range, that byte is its own RLP encoding
            if (bytesRequired == 1 && value.m_pData[ECC::uintBig::nBytes - 1] < 0x80)
            {
                m_out.push_back(value.m_pData[ECC::uintBig::nBytes - 1]);
            }
            else
            {
                m_out.reserve(m_out.size() + bytesRequired + 1);
                m_out.push_back(static_cast<uint8_t>(kRLPDataLenStart + bytesRequired));
                m_out.insert(m_out.end(), std::end(value.m_pData) - bytesRequired, std::end(value.m_pData));
            }
        }
        return *this;
    }

    RLPStream& RLPStream::append(const beam::ByteBuffer& value)
    {
        auto valueSize = value.size();
        if (value.empty())
        {
            m_out.push_back(0x80);
        }
        else
        {
            auto lengthBuffer = EncodeLength(valueSize);
            m_out.reserve(m_out.size() + lengthBuffer.size() + valueSize);
            m_out.insert(m_out.end(), lengthBuffer.cbegin(), lengthBuffer.cend());
            m_out.insert(m_out.end(), value.begin(), value.end());
        }
        return *this;
    }

    RLPStream& RLPStream::append(const libbitcoin::short_hash& value)
    {
        auto valueSize = value.size();

        m_out.reserve(m_out.size() + valueSize + 1);
        m_out.push_back(static_cast<uint8_t>(kRLPDataLenStart + valueSize));
        m_out.insert(m_out.end(), value.begin(), value.end());
        return *this;
    }

    beam::ByteBuffer RLPStream::out() const
    {
        constexpr uint8_t kRLPListOffset = 0xc0;
        auto outSize = m_out.size();
        auto lengthBuffer = EncodeLength(outSize, kRLPListOffset);

        beam::ByteBuffer out;
        out.reserve(outSize + lengthBuffer.size());
        out.insert(out.end(), lengthBuffer.cbegin(), lengthBuffer.cend());
        out.insert(out.end(), m_out.cbegin(), m_out.cend());

        return out;
    }

    beam::ByteBuffer RLPStream::EncodeLength(size_t size, uint8_t offset) const
    {
        constexpr uint8_t kRLPSmallDataLengthLimit = 55;

        if (size > kRLPSmallDataLengthLimit)
        {
            auto bytesRequired = BytesRequired(size);
            beam::ByteBuffer out(bytesRequired + 1);
            // The range of the first byte is thus [0xb8, 0xbf] or [0xf8, 0xff] for list.
            out.front() = bytesRequired + offset + kRLPSmallDataLengthLimit;

            auto b = out.end();
            for (; size; size >>= 8)
            {
                *(--b) = static_cast<uint8_t>(size & 0xff);
            }

            return out;
        }

        // The range of the first byte is thus [0x80, 0xb7] or [0xc0, 0xf7] for list.
        return beam::ByteBuffer{ static_cast<uint8_t>(size + offset) };
    }
} // namespace

namespace beam::ethereum
{
    beam::ByteBuffer EthBaseTransaction::GetRawSigned(const libbitcoin::ec_secret& secret)
    {
        // Sign
        libbitcoin::recoverable_signature signature;
        if (!Sign(signature, secret))
        {
            // failed to sign raw TX
            return {};
        }

        RLPStream rlpStream;
        rlpStream << m_nonce << m_gasPrice << m_gas << m_receiveAddress << m_value << m_data;

        // TODO: ChainID!
        //rlpStream << 38u;
        rlpStream << (27 + signature.recovery_id);
        // r
        rlpStream << beam::ByteBuffer(std::begin(signature.signature), std::end(signature.signature) - 32);
        // s
        rlpStream << beam::ByteBuffer(std::end(signature.signature) - 32, std::end(signature.signature));

        return rlpStream.out();
    }

    bool EthBaseTransaction::Sign(libbitcoin::recoverable_signature& out, const libbitcoin::ec_secret& secret)
    {
        RLPStream rlpStream;
        // TODO: chainID!
        // rlpStream << m_nonce << m_gasPrice << m_gas << m_receiveAddress << m_value << m_data << 1u << 0u << 0u;
        rlpStream << m_nonce << m_gasPrice << m_gas << m_receiveAddress << m_value << m_data;

        auto txData = rlpStream.out();
        auto hash = ethash::keccak256(&txData[0], txData.size());
        libbitcoin::hash_digest hashDigest;
        std::move(std::begin(hash.bytes), std::end(hash.bytes), hashDigest.begin());

        return libbitcoin::sign_recoverable(out, secret, hashDigest);
    }
} // namespace beam::ethereum

