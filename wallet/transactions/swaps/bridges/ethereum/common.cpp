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

#include "common.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <ethash/keccak.hpp>
#include "utility/hex.h"

namespace
{
    const std::string kHexPrefix = "0x";

    bool HasHexPrefix(const std::string& value)
    {
        return value.find(kHexPrefix) == 0;
    }
}

namespace beam::ethereum
{
std::string ConvertEthAddressToStr(const libbitcoin::short_hash& addr)
{
    return kHexPrefix + libbitcoin::encode_base16(addr);
}

libbitcoin::short_hash ConvertStrToEthAddress(const std::string& addressStr)
{
    libbitcoin::short_hash address;
    libbitcoin::decode_base16(address, RemoveHexPrefix(addressStr));
    return address;
}

libbitcoin::short_hash GetEthAddressFromPubkeyStr(const std::string& pubkeyStr)
{
    auto tmp = beam::from_hex(std::string(pubkeyStr.begin() + 2, pubkeyStr.end()));
    auto hash = ethash::keccak256(&tmp[0], tmp.size());
    libbitcoin::short_hash address;

    std::copy_n(&hash.bytes[12], 20, address.begin());
    return address;
}

ECC::uintBig ConvertStrToUintBig(const std::string& number, bool hex)
{
    libbitcoin::data_chunk dc;

    if (hex)
    {
        dc = beam::from_hex(RemoveHexPrefix(number));
    }
    else
    {
        boost::multiprecision::uint256_t value(number);
        std::stringstream stream;

        stream << std::hex << value;
        dc = beam::from_hex(stream.str());
    }

    ECC::uintBig result = ECC::Zero;
    std::copy(dc.crbegin(), dc.crend(), std::rbegin(result.m_pData));
    return result;
}

std::string AddHexPrefix(const std::string& value)
{
    if (!HasHexPrefix(value))
    {
        return kHexPrefix + value;
    }

    return value;
}

std::string RemoveHexPrefix(const std::string& value)
{
    if (HasHexPrefix(value))
    {
        return std::string(value.begin() + 2, value.end());
    }

    return value;
}

void AddContractABIWordToBuffer(const libbitcoin::data_slice& src, libbitcoin::data_chunk& dst)
{
    assert(src.size() <= kEthContractABIWordSize);
    if (src.size() < kEthContractABIWordSize)
    {
        dst.insert(dst.end(), kEthContractABIWordSize - src.size(), 0x00);
    }
    dst.insert(dst.end(), src.begin(), src.end());
}

uint32_t GetCoinUnitsMultiplier(beam::wallet::AtomicSwapCoin swapCoin)
{
    switch (swapCoin)
    {
    case beam::wallet::AtomicSwapCoin::Ethereum:
    case beam::wallet::AtomicSwapCoin::Dai:
        return 1'000'000'000u;
    case beam::wallet::AtomicSwapCoin::Tether:
    case beam::wallet::AtomicSwapCoin::WBTC:
        return 1u;
    default:
        assert(false && "Unexpected swapCoin!");
        return 1u;
    }
}

bool IsEthereumBased(wallet::AtomicSwapCoin swapCoin)
{
    switch (swapCoin)
    {
    case beam::wallet::AtomicSwapCoin::Ethereum:
    case beam::wallet::AtomicSwapCoin::Dai:
    case beam::wallet::AtomicSwapCoin::Tether:
    case beam::wallet::AtomicSwapCoin::WBTC:
        return true;
    default:
        return false;
    }
}
} // namespace beam::ethereum