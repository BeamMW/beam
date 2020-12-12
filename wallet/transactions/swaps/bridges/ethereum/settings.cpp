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

namespace beam::ethereum
{
bool Settings::IsInitialized() const
{
    return m_secretWords.size() == 12 && !m_address.empty() && (!m_swapContractAddress.empty() || !m_swapHashlockContractAddress.empty());
}

bool Settings::IsTokenInitialized(beam::wallet::AtomicSwapCoin swapCoin) const
{
    if (IsInitialized() && !m_erc20SwapContractAddress.empty())
    {
        switch (swapCoin)
        {
        case beam::wallet::AtomicSwapCoin::Dai:
            return !m_daiContractAddress.empty();
        case beam::wallet::AtomicSwapCoin::Tether:
            return !m_usdtContractAddress.empty();
        case beam::wallet::AtomicSwapCoin::WBTC:
            return !m_wbtcContractAddress.empty();
        default:
            assert(false && "Unsupported token!");
            break;
        }
    }
    return false;
}

bool Settings::IsTokenActivated(beam::wallet::AtomicSwapCoin swapCoin) const
{
    if (IsTokenInitialized(swapCoin))
    {
        switch (swapCoin)
        {
        case beam::wallet::AtomicSwapCoin::Dai:
            return m_shouldConnectToDai;
        case beam::wallet::AtomicSwapCoin::Tether:
            return m_shouldConnectToUsdt;
        case beam::wallet::AtomicSwapCoin::WBTC:
            return m_shouldConnectToWBTC;
        default:
            assert(false && "Unsupported token!");
            break;
        }
    }
    return false;
}

bool Settings::IsActivated() const
{
    return m_shouldConnect && IsInitialized();
}

uint16_t Settings::GetTxMinConfirmations() const
{
    return m_txMinConfirmations;
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
    return isHashLockScheme ? m_swapHashlockContractAddress : m_swapContractAddress;
}

std::string Settings::GetERC20SwapContractAddress(bool isHashLockScheme) const
{
    return isHashLockScheme ? m_erc20SwapHashlockContractAddress : m_erc20SwapContractAddress;
}

std::string Settings::GetTokenContractAddress(beam::wallet::AtomicSwapCoin swapCoin) const
{
    switch (swapCoin)
    {
    case beam::wallet::AtomicSwapCoin::Dai:
        return m_daiContractAddress;
    case beam::wallet::AtomicSwapCoin::Tether:
        return m_usdtContractAddress;
    case beam::wallet::AtomicSwapCoin::WBTC:
        return m_wbtcContractAddress;
    default:
        assert(false && "Unsupported token!");
        return {};
    }
}
} // namespace beam::ethereum