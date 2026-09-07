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
#include <bitcoin/bitcoin/math/hash.hpp>
#include <bitcoin/bitcoin/math/elliptic_curve.hpp>
#include <bitcoin/bitcoin/utility/data.hpp>
#include "core/ecc.h"

#include "wallet/transactions/swaps/common.h"

namespace beam::ethereum
{
inline constexpr uint8_t kEthContractABIWordSize = 32;
inline constexpr uint8_t kEthContractMethodHashSize = 4;

const uint64_t kLockTxGasLimit = 200'000u;
const uint64_t kApproveTxGasLimit = 60'000u;
const uint64_t kWithdrawTxGasLimit = 200'000u;

std::string ConvertEthAddressToStr(const libbitcoin::short_hash& addr);
libbitcoin::short_hash ConvertStrToEthAddress(const std::string& addressStr);
libbitcoin::short_hash GetEthAddressFromPubkeyStr(const std::string& pubkeyStr);
libbitcoin::ec_secret GeneratePrivateKey(const std::vector<std::string> words, uint32_t accountIndex);
libbitcoin::short_hash GenerateEthereumAddress(const std::vector<std::string> words, uint32_t accountIndex);
ECC::uintBig ConvertStrToUintBig(const std::string& number, bool hex = true);
std::string AddHexPrefix(const std::string& value);
std::string RemoveHexPrefix(const std::string& value);

void AddContractABIWordToBuffer(const libbitcoin::data_slice& src, libbitcoin::data_chunk& dst);
uint32_t GetCoinUnitsMultiplier(beam::wallet::AtomicSwapCoin swapCoin);
bool IsEthereumBased(wallet::AtomicSwapCoin swapCoin);

// Parses an ABI uint256 word (64 hex chars, no 0x) as token decimals.
// Fails when any byte above the lowest is set or the value exceeds
// kMaxTokenDecimals.
bool ParseTokenDecimalsWord(const std::string& hexWord, uint8_t& decimals);

// Per-offer ERC-20 token (AtomicSwapCoin::Erc20Token) equivalents of
// UnitsPerCoin/GetCoinUnitsMultiplier, parameterized by the token's on-chain
// decimals (TxParameterID::AtomicSwapTokenDecimals) instead of a fixed table.
// walletDecimals = min(decimals, 9); on-wire value = Amount * TokenUnitsMultiplier(decimals).
// These reproduce today's constants for classic coins: ETH/DAI (18 -> 10^9/10^9),
// USDT (6 -> 10^6/1), WBTC (8 -> 10^8/1).
// decimals is attacker-controlled (comes from the counterparty's contract via
// getTokenInfo, or from a peer's offer-board TxParameterID::AtomicSwapTokenDecimals),
// so it must be bounded by kMaxTokenDecimals before it reaches these helpers.
// isExtendedOfferDataValid() and getTokenInfo() are the primary guards; the
// assert+clamp below is only a backstop against a programmer error letting an
// out-of-range decimals slip through, never the primary defense (TokenUnitsMultiplier
// computes 10^(decimals-9) in a uint32_t, which wraps/zeroes for decimals >= 19).
uint64_t WalletUnitsPerToken(uint8_t decimals);   // = 10^min(decimals, 9)
uint32_t TokenUnitsMultiplier(uint8_t decimals);  // = 10^max(0, decimals - 9)

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
    // "symbol()"
    inline const char* kSymbolHash = "95d89b41";
} // namespace ERC20Hashes

namespace swap_contract
{
    std::string GetRefundMethodHash(bool isHashLockScheme);
    std::string GetLockMethodHash(bool isErc20, bool isHashLockScheme);
    std::string GetRedeemMethodHash(bool isHashLockScheme);
    std::string GetDetailsMethodHash(bool isHashLockScheme);
} // swap_contract
} // namespace beam::ethereum
