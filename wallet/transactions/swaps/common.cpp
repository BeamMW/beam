// Copyright 2019 The Beam Team
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

#include "wallet/transactions/swaps/common.h"
#include "wallet/transactions/swaps/bridges/bitcoin/common.h"
#include "wallet/transactions/swaps/bridges/qtum/common.h"
#include "bitcoin/bitcoin.hpp"

namespace beam::wallet
{

bool g_EnforceTestnetSwap = false;

bool UseMainnetSwap()
{
    if (g_EnforceTestnetSwap)
        return false;

    return (Rules::Profile::mainnet == Rules::get().m_Profile);
}

bool IsEthToken(AtomicSwapCoin swapCoin)
{
    return std::count(std::begin(kEthTokens), std::end(kEthTokens), swapCoin);
}

AtomicSwapCoin from_string(const std::string& value)
{
    if (value == "btc")
        return AtomicSwapCoin::Bitcoin;
    else if (value == "ltc")
        return AtomicSwapCoin::Litecoin;
    else if (value == "qtum")
        return AtomicSwapCoin::Qtum;
#if defined(BITCOIN_CASH_SUPPORT)
    else if (value == "bch")
        return AtomicSwapCoin::Bitcoin_Cash;
#endif // BITCOIN_CASH_SUPPORT
    else if (value == "doge")
        return AtomicSwapCoin::Dogecoin;
    else if (value == "dash")
        return AtomicSwapCoin::Dash;
    else if (value == "eth")
        return AtomicSwapCoin::Ethereum;
    else if (value == "dai")
        return AtomicSwapCoin::Dai;
    else if (value == "usdt")
        return AtomicSwapCoin::Usdt;
    else if (value == "wbtc")
        return AtomicSwapCoin::WBTC;

    return AtomicSwapCoin::Unknown;
}

uint64_t UnitsPerCoin(AtomicSwapCoin swapCoin) noexcept
{
    switch (swapCoin)
    {
    case AtomicSwapCoin::Bitcoin:
    case AtomicSwapCoin::Litecoin:
    case AtomicSwapCoin::Qtum:
#if defined(BITCOIN_CASH_SUPPORT)
    case AtomicSwapCoin::Bitcoin_Cash:
#endif // BITCOIN_CASH_SUPPORT
    case AtomicSwapCoin::Dogecoin:
    case AtomicSwapCoin::Dash:
    case AtomicSwapCoin::WBTC:
        return libbitcoin::satoshi_per_bitcoin;
    case AtomicSwapCoin::Ethereum:
    case AtomicSwapCoin::Dai:
        return 1'000'000'000u;
    case AtomicSwapCoin::Usdt:
        return 1'000'000u;
    default:
    {
        assert("Unsupported swapCoin type.");
        return 0;
    }
    }
}

std::string GetCoinName(AtomicSwapCoin swapCoin)
{
    switch (swapCoin)
    {
    case AtomicSwapCoin::Bitcoin:
    {
        return "Bitcoin";
    }
    case AtomicSwapCoin::Litecoin:
    {
        return "Litecoin";
    }
    case AtomicSwapCoin::Qtum:
    {
        return "Qtum";
    }
#if defined(BITCOIN_CASH_SUPPORT)
    case AtomicSwapCoin::Bitcoin_Cash:
    {
        return "Bitcoin Cash";
    }
#endif //BITCOIN_CASH_SUPPORT
    case AtomicSwapCoin::Dogecoin:
    {
        return "Dogecoin";
    }
    case AtomicSwapCoin::Dash:
    {
        return "Dash";
    }
    case AtomicSwapCoin::Ethereum:
    {
        return "Ethereum";
    }
    case AtomicSwapCoin::Dai:
    {
        return "Dai";
    }
    case AtomicSwapCoin::Usdt:
    {
        return "Usdt";
    }
    case AtomicSwapCoin::WBTC:
    {
        return "WBTC";
    }
    default:
    {
        assert(false && "unexpected swap coin!");
        return "Unknown";
    }
    }
}

std::string swapOfferStatusToString(const SwapOfferStatus& status)
{
    switch(status)
    {
    case SwapOfferStatus::Canceled : return "cancelled";
    case SwapOfferStatus::Completed : return "completed";
    case SwapOfferStatus::Expired : return "expired";
    case SwapOfferStatus::Failed : return "failed";
    case SwapOfferStatus::InProgress : return "in progress";
    case SwapOfferStatus::Pending : return "pending";
    default : return "unknown";
    }
}

}  // namespace beam::wallet

namespace std
{
string to_string(beam::wallet::SwapOfferStatus status)
{
    switch (status)
    {
    case beam::wallet::SwapOfferStatus::Pending:
        return "Pending";
    case beam::wallet::SwapOfferStatus::InProgress:
        return "InProgress";
    case beam::wallet::SwapOfferStatus::Completed:
        return "Completed";
    case beam::wallet::SwapOfferStatus::Canceled:
        return "Canceled";
    case beam::wallet::SwapOfferStatus::Expired:
        return "Expired";
    case beam::wallet::SwapOfferStatus::Failed:
        return "Failed";

    default:
        return "";
    }
}

string to_string(beam::wallet::AtomicSwapCoin value)
{
    switch (value)
    {
    case beam::wallet::AtomicSwapCoin::Bitcoin:
        return "BTC";
    case beam::wallet::AtomicSwapCoin::Litecoin:
        return "LTC";
    case beam::wallet::AtomicSwapCoin::Qtum:
        return "QTUM";
#if defined(BITCOIN_CASH_SUPPORT)
    case beam::wallet::AtomicSwapCoin::Bitcoin_Cash:
        return "BCH";
#endif // BITCOIN_CASH_SUPPORT
    case beam::wallet::AtomicSwapCoin::Dogecoin:
        return "DOGE";
    case beam::wallet::AtomicSwapCoin::Dash:
        return "DASH";
    case beam::wallet::AtomicSwapCoin::Ethereum:
        return "ETH";
    case beam::wallet::AtomicSwapCoin::Dai:
        return "DAI";
    case beam::wallet::AtomicSwapCoin::Usdt:
        return "USDT";
    case beam::wallet::AtomicSwapCoin::WBTC:
        return "WBTC";
    default:
        return "";
    }
}
}  // namespace std

namespace beam::electrum
{
std::vector<std::string> generateReceivingAddresses
    (wallet::AtomicSwapCoin swapCoin, const std::vector<std::string>& words, uint32_t amount, uint8_t addressVersion)
{
    std::vector<std::string> addresses;
    libbitcoin::wallet::hd_private masterKey;
    if (swapCoin == wallet::AtomicSwapCoin::Qtum)
    {
        masterKey = qtum::generateElectrumMasterPrivateKeys(words).first;
    }
    else
    {
        masterKey = bitcoin::generateElectrumMasterPrivateKeys(words).first;
    }


    for (uint32_t index = 0; index < amount; index++)
    {
        addresses.emplace_back(bitcoin::getElectrumAddress(masterKey, index, addressVersion));
    }
    return addresses;
}

std::vector<std::string> generateChangeAddresses
    (wallet::AtomicSwapCoin swapCoin, const std::vector<std::string>& words, uint32_t amount, uint8_t addressVersion)
{
    std::vector<std::string> addresses;
    libbitcoin::wallet::hd_private masterKey;
    if (swapCoin == wallet::AtomicSwapCoin::Qtum)
    {
        masterKey = qtum::generateElectrumMasterPrivateKeys(words).second;
    }
    else
    {
        masterKey = bitcoin::generateElectrumMasterPrivateKeys(words).second;
    }

    for (uint32_t index = 0; index < amount; index++)
    {
        addresses.emplace_back(bitcoin::getElectrumAddress(masterKey, index, addressVersion));
    }
    return addresses;
}

bool validateMnemonic(const std::vector<std::string>& words, bool isSegwitType)
{
    auto seedType = isSegwitType ? libbitcoin::wallet::electrum::seed::witness : libbitcoin::wallet::electrum::seed::standard;
    return libbitcoin::wallet::electrum::validate_mnemonic(words, seedType);
}

std::vector<std::string> createMnemonic(const std::vector<uint8_t>& entropy)
{
    return libbitcoin::wallet::electrum::create_mnemonic(entropy);
}

bool isAllowedWord(const std::string& word)
{
    return std::binary_search(libbitcoin::wallet::language::electrum::en.begin(), libbitcoin::wallet::language::electrum::en.end(), word);
}
}  // namespace beam::electrum
