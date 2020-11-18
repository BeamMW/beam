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

#pragma once

#include <string>
#include <bitcoin/bitcoin.hpp>
#include "core/ecc.h"

#include "wallet/transactions/swaps/common.h"

namespace beam::ethereum
{
inline constexpr uint8_t kEthContractABIWordSize = 32;
inline constexpr uint8_t kEthContractMethodHashSize = 4;

std::string ConvertEthAddressToStr(const libbitcoin::short_hash& addr);
libbitcoin::short_hash ConvertStrToEthAddress(const std::string& addressStr);
libbitcoin::short_hash GetEthAddressFromPubkeyStr(const std::string& pubkeyStr);
ECC::uintBig ConvertStrToUintBig(const std::string& number, bool hex = true);
std::string AddHexPrefix(const std::string& value);
std::string RemoveHexPrefix(const std::string& value);

void AddContractABIWordToBuffer(const libbitcoin::data_slice& src, libbitcoin::data_chunk& dst);
uint32_t GetCoinUnitsMultiplier(beam::wallet::AtomicSwapCoin swapCoin);
bool IsEthereumBased(wallet::AtomicSwapCoin swapCoin);

namespace ERC20Hashes
{
    // "allowance(address,address)"
    inline const char* kAllowanceHash = "dd62ed3e";
    // "approve(address,uint256)"
    inline const char* kApproveHash = "095ea7b3";
    // "balanceOf(address)"
    inline const char* kBalanceOfHash = "70a08231";
    // "totalSupply()"
    inline const char* kTotalSupplyHash = "18160ddd";
    // "transfer(address,uint256)"
    inline const char* kTransferHash = "a9059cbb";
    // "transferFrom(address,address,uint256)"
    inline const char* kTransferFromHash = "23b872dd";
    // "name()"
    inline const char* kNameHash = "06fdde03";
    // "decimals()"
    inline const char* kDecimalsHash = "313ce567";
} // namespace ERC20Hashes
} // namespace beam::ethereum