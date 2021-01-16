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
#include <vector>

#include "wallet/transactions/swaps/common.h"
#include "common.h"

namespace beam::ethereum
{
struct Settings
{
    std::string m_projectID = "";
    std::vector<std::string> m_secretWords = {};
    uint32_t m_accountIndex = 0;
    bool m_shouldConnect = false;
    uint16_t m_lockTxMinConfirmations = 12;
    uint16_t m_withdrawTxMinConfirmations = 1;
    uint32_t m_lockTimeInBlocks = 12 * 60 * 4;  // 12h
    double m_blocksPerHour = 250;
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
    Amount m_minFeeRate = 15u;
#else // MASTERNET and TESTNET
    Amount m_minFeeRate = 1u;
#endif
    Amount m_maxFeeRate = 1'000'000'000u;
    uint64_t m_lockTxGasLimit = kLockTxGasLimit;
    uint64_t m_withdrawTxGasLimit = kWithdrawTxGasLimit;

    bool IsInitialized() const;
    bool IsActivated() const;

    uint16_t GetLockTxMinConfirmations() const;
    uint16_t GetWithdrawTxMinConfirmations() const;
    uint32_t GetLockTimeInBlocks() const;
    double GetBlocksPerHour() const;
    Amount GetMinFeeRate() const;
    Amount GetMaxFeeRate() const;
    std::string GetContractAddress(bool isHashLockScheme = false) const;
    std::string GetERC20SwapContractAddress(bool isHashLockScheme = false) const;
    std::string GetTokenContractAddress(beam::wallet::AtomicSwapCoin swapCoin) const;
    std::string GetEthNodeAddress() const;
    std::string GetEthNodeHost() const;
    bool NeedSsl() const;
    std::string GetPathAndQuery() const;

    bool operator == (const Settings& other) const
    {
        return m_projectID == other.m_projectID &&
            m_secretWords == other.m_secretWords &&
            m_accountIndex == other.m_accountIndex &&
            m_shouldConnect == other.m_shouldConnect;
    }

    bool operator != (const Settings& other) const
    {
        return !(*this == other);
    }
};
} // namespace beam::ethereum