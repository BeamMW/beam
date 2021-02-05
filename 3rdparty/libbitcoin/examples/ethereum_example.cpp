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

#include <bitcoin/bitcoin.hpp>
#include <ethash/keccak.hpp>
#include <iostream>
#include <vector>

#include "utility/hex.h"
#include "core/ecc.h"


libbitcoin::wallet::hd_private ProcessHDPrivate(const libbitcoin::wallet::hd_private& privateKey, uint32_t index, bool hard = true)
{
    static constexpr auto first = libbitcoin::wallet::hd_first_hardened_key;
    const auto position = hard ? first + index : index;
    return privateKey.derive_private(position);
}

template <class T>
unsigned BytesRequired(T value)
{
    static_assert(!std::numeric_limits<T>::is_signed, "only unsigned types supported");
    unsigned i = 0;
    for (; value != 0; ++i, value >>= 8)
    {
    }
    return i;
}

unsigned BytesRequired(ECC::uintBig input)
{
    unsigned n = 0;
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

    /// Append given datum to the byte stream.
    RLPStream& append(unsigned value) { return append(ECC::uintBig(value)); }
    RLPStream& append(const ECC::uintBig& value);
    RLPStream& append(const beam::ByteBuffer& value);
    RLPStream& append(const libbitcoin::short_hash& value);

    /// Shift operators for appending data items.
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

struct EthTransactionSkeleton
{
    libbitcoin::short_hash m_from;
    libbitcoin::short_hash m_receiveAddress;
    ECC::uintBig m_value = ECC::Zero;
    beam::ByteBuffer m_data;
    ECC::uintBig m_nonce = ECC::Zero;
    ECC::uintBig m_gas = ECC::Zero;
    ECC::uintBig m_gasPrice = ECC::Zero;
    
    // sign_recoverable
    // RLPEncode: m_nonce << m_gasPrice << m_gas << m_receiveAddress << m_value << m_data << *m_chainId << 0 << 0;
    
    beam::ByteBuffer GetRawSigned(const libbitcoin::ec_secret& secret);

    libbitcoin::recoverable_signature Sign(const libbitcoin::ec_secret& secret);
};

beam::ByteBuffer EthTransactionSkeleton::GetRawSigned(const libbitcoin::ec_secret& secret)
{
    // Sign
    auto signature = Sign(secret);

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

libbitcoin::recoverable_signature EthTransactionSkeleton::Sign(const libbitcoin::ec_secret& secret)
{
    RLPStream rlpStream;
    // TODO: chainID!
    // rlpStream << m_nonce << m_gasPrice << m_gas << m_receiveAddress << m_value << m_data << 1u << 0u << 0u;
    rlpStream << m_nonce << m_gasPrice << m_gas << m_receiveAddress << m_value << m_data;

    auto txData = rlpStream.out();
    auto hash = ethash::keccak256(&txData[0], txData.size());
    libbitcoin::hash_digest hashDigest;
    std::move(std::begin(hash.bytes), std::end(hash.bytes), hashDigest.begin());

    libbitcoin::recoverable_signature signature;
    // TODO: check result
    libbitcoin::sign_recoverable(signature, secret, hashDigest);
    return signature;
}

void GenerateAddress(uint32_t index)
{
    std::vector<std::string> words = { "weather", "hen", "detail", "region", "misery", "click", "wealth", "butter", "immense", "hire", "pencil", "social"};
    //std::vector<std::string> words = { "grass", "happy", "napkin", "skill", "hazard", "isolate", "slot", "barely", "stamp", "dismiss", "there", "found"};

    //std::vector<std::string> words = { "radar", "blur", "cabbage", "chef", "fix", "engine", "embark", "joy", "scheme", "fiction", "master", "release" };
    auto seed = libbitcoin::wallet::decode_mnemonic(words);
    libbitcoin::data_chunk seed_chunk(libbitcoin::to_chunk(seed));

    const auto prefixes = libbitcoin::wallet::hd_private::to_prefixes(0, 0);
    libbitcoin::wallet::hd_private private_key(seed_chunk, prefixes);

    private_key = ProcessHDPrivate(private_key, 44);
    private_key = ProcessHDPrivate(private_key, 60);
    private_key = ProcessHDPrivate(private_key, 0);
    private_key = ProcessHDPrivate(private_key, 0, false);
    private_key = ProcessHDPrivate(private_key, index, false);

    std::cout << private_key.encoded() << std::endl;
    std::cout << libbitcoin::encode_base16(private_key.secret()) << std::endl;


    libbitcoin::ec_compressed point;

    libbitcoin::secret_to_public(point, private_key.secret());

    auto pk = libbitcoin::wallet::ec_public(point, false);
    auto rawPk = pk.encoded();

    std::cout << rawPk << std::endl;    

    auto tmp = beam::from_hex(std::string(rawPk.begin() + 2, rawPk.end()));

    auto hash = ethash::keccak256(&tmp[0], tmp.size());
    libbitcoin::data_chunk data;
    for (int i = 12; i < 32; i++)
    {
        data.push_back(hash.bytes[i]);
    }

    std::cout << libbitcoin::encode_base16(data);
}

void TestTxToRLP()
{
    libbitcoin::data_chunk addrDataChunk;
    libbitcoin::decode_base16(addrDataChunk, "40E96E6278469e9Bc41D4C849E9635Eea08A369B");
    libbitcoin::short_hash address;
    std::copy(addrDataChunk.begin(), addrDataChunk.end(), address.begin());

    EthTransactionSkeleton tx;
    tx.m_nonce = 5u;
    tx.m_gas = 100500u;
    tx.m_gasPrice = 100u;
    tx.m_value = 1234567890u;
    tx.m_from = address;
    tx.m_receiveAddress = address;

    RLPStream rlpStream;

    rlpStream << tx.m_nonce << tx.m_gasPrice << tx.m_gas << tx.m_receiveAddress << tx.m_value << tx.m_data;

    std::cout << "tx data: " << libbitcoin::encode_base16(rlpStream.out()) << "\n";
}

void TestTxToRLPFull()
{
    libbitcoin::data_chunk addrDataChunk;
    libbitcoin::decode_base16(addrDataChunk, "13129DAcC4f3642fe2d3b85c8ba6BC4766950964");
    libbitcoin::short_hash address;
    std::copy(addrDataChunk.begin(), addrDataChunk.end(), address.begin());

    libbitcoin::data_chunk secretData;
    libbitcoin::decode_base16(secretData, "feea6be022b9ec09a1ee55820bba1d7acc98c889f590cf7939c4a6a8b0967e5b");
    libbitcoin::ec_secret secret;
    std::move(secretData.begin(), secretData.end(), std::begin(secret));

    EthTransactionSkeleton tx;
    tx.m_nonce = 1u;
    tx.m_gas = 10u;
    tx.m_gasPrice = 100u;
    tx.m_value = 123456789u;
    tx.m_from = address;
    tx.m_receiveAddress = address;

    std::cout << "tx data: " << libbitcoin::encode_base16(tx.GetRawSigned(secret)) << "\n";
}

int main()
{
    TestTxToRLP();
    TestTxToRLPFull();

    GenerateAddress(1);

    return 0;
}