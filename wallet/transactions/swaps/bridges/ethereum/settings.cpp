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

#include "settings.h"

namespace
{
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
    const std::string kSwapContractAddress = "0x2FA243fC8f9EAF014f8d6E909157B6A48cEE0bdC";
    const std::string kSwapHashlockContractAddress = "";
    const std::string kErc20SwapContractAddress = "0xDd62a95626453F54E686cF0531bCbf6766150794";
    const std::string kErc20SwapHashlockContractAddress = "";
    const std::string kDaiContractAddress = "0x6b175474e89094c44da98b954eedeac495271d0f";
    const std::string kUsdtContractAddress = "0xdac17f958d2ee523a2206206994597c13d831ec7";
    const std::string kWBTCContractAddress = "0x2260fac5e5542a773aa44fbcfedf7c193bc2c599";

    const std::string kEthNodeAddress = "mainnet.infura.io:443";
    const std::string kEthNodeHost = "mainnet.infura.io";
    const bool kNeedSsl = true;
    const std::string kPahtAndQuery = "/v3/";
#else // MASTERNET and TESTNET
    // TODO roman.strilets need to fill in they
    const std::string kSwapContractAddress = "0x6d9b8787758e3a965f496c0e25fd29fc82d2b87f";
    const std::string kSwapHashlockContractAddress = "";
    const std::string kErc20SwapContractAddress = "0x1f567e003c3b2d45f3e5e384180f518f150184a4";
    const std::string kErc20SwapHashlockContractAddress = "";
    const std::string kDaiContractAddress = "0xd4a05ed1b15666c459bba178490ea53b9e5061ec";
    const std::string kUsdtContractAddress = "0x0b3dec250a07d3a7f411db9bd986feab29c52fbc";
    const std::string kWBTCContractAddress = "0xe75dc3a612f855890d53a2196a087a9cbe55d058";

    const std::string kEthNodeAddress = "ropsten.infura.io:443";
    const std::string kEthNodeHost = "ropsten.infura.io";
    const bool kNeedSsl = true;
    const std::string kPahtAndQuery = "/v3/";
#endif
}

namespace beam::ethereum
{
bool Settings::IsInitialized() const
{
    return m_secretWords.size() == 12 && !m_projectID.empty();
}

bool Settings::IsActivated() const
{
    return m_shouldConnect && IsInitialized();
}

uint16_t Settings::GetLockTxMinConfirmations() const
{
    return m_lockTxMinConfirmations;
}

uint16_t Settings::GetWithdrawTxMinConfirmations() const
{
    return m_withdrawTxMinConfirmations;
}

uint32_t Settings::GetLockTimeInBlocks() const
{
    return m_lockTimeInBlocks;
}

double Settings::GetBlocksPerHour() const
{
    return m_blocksPerHour;
}

Amount Settings::GetMinFeeRate() const
{
    return m_minFeeRate;
}

Amount Settings::GetMaxFeeRate() const
{
    return m_maxFeeRate;
}

std::string Settings::GetContractAddress(bool isHashLockScheme) const
{
    return isHashLockScheme ? kSwapHashlockContractAddress : kSwapContractAddress;
}

std::string Settings::GetERC20SwapContractAddress(bool isHashLockScheme) const
{
    return isHashLockScheme ? kErc20SwapHashlockContractAddress : kErc20SwapContractAddress;
}

std::string Settings::GetTokenContractAddress(beam::wallet::AtomicSwapCoin swapCoin) const
{
    switch (swapCoin)
    {
    case beam::wallet::AtomicSwapCoin::Dai:
        return kDaiContractAddress;
    case beam::wallet::AtomicSwapCoin::Usdt:
        return kUsdtContractAddress;
    case beam::wallet::AtomicSwapCoin::WBTC:
        return kWBTCContractAddress;
    default:
        assert(false && "Unsupported token!");
        return {};
    }
}

std::string Settings::GetEthNodeAddress() const
{
    return kEthNodeAddress;
}

std::string Settings::GetEthNodeHost() const
{
    return kEthNodeHost;
}

bool Settings::NeedSsl() const
{
    return kNeedSsl;
}

std::string Settings::GetPathAndQuery() const
{
    return kPahtAndQuery + m_projectID; // TODO roman.strilets add Project ID
}
} // namespace beam::ethereum