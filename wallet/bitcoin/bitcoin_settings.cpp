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

#include "bitcoin_settings.h"

namespace beam::bitcoin
{
    const BitcoindSettings& Settings::GetConnectionOptions() const
    {
        return m_connectionSettings;
    }

    Amount Settings::GetFeeRate() const
    {
        return m_feeRate;
    }

    Amount Settings::GetMinFeeRate() const
    {
        return m_minFeeRate;
    }

    uint16_t Settings::GetTxMinConfirmations() const
    {
        return m_txMinConfirmations;
    }

    uint32_t Settings::GetLockTimeInBlocks() const
    {
        return m_lockTimeInBlocks;
    }

    wallet::SwapSecondSideChainType Settings::GetChainType() const
    {
        return m_chainType;
    }

    bool Settings::IsInitialized() const
    {
        return m_connectionSettings.IsInitialized();
    }

    void Settings::SetConnectionOptions(const BitcoindSettings& connectionSettings)
    {
        m_connectionSettings = connectionSettings;
    }

    void Settings::SetFeeRate(Amount feeRate)
    {
        m_feeRate = feeRate;
    }

    void Settings::SetMinFeeRate(beam::Amount feeRate)
    {
        m_minFeeRate = feeRate;
    }

    void Settings::SetTxMinConfirmations(uint16_t txMinConfirmations)
    {
        m_txMinConfirmations = txMinConfirmations;
    }

    void Settings::SetLockTimeInBlocks(uint32_t lockTimeInBlocks)
    {
        m_lockTimeInBlocks = lockTimeInBlocks;
    }

    void Settings::SetChainType(wallet::SwapSecondSideChainType chainType)
    {
        m_chainType = chainType;
    }
} // namespace beam::bitcoin