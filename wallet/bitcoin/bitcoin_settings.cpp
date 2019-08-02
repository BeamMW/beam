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

namespace beam
{
    const BitcoindSettings& BitcoinSettings::GetConnectionOptions() const
    {
        return m_connectionSettings;
    }

    Amount BitcoinSettings::GetFeeRate() const
    {
        return m_feeRate;
    }

    Amount BitcoinSettings::GetMinFeeRate() const
    {
        return m_minFeeRate;
    }

    uint16_t BitcoinSettings::GetTxMinConfirmations() const
    {
        return m_txMinConfirmations;
    }

    uint32_t BitcoinSettings::GetLockTimeInBlocks() const
    {
        return m_lockTimeInBlocks;
    }

    wallet::SwapSecondSideChainType BitcoinSettings::GetChainType() const
    {
        return m_chainType;
    }

    void BitcoinSettings::SetConnectionOptions(const BitcoindSettings& connectionSettings)
    {
        m_connectionSettings = connectionSettings;
    }

    void BitcoinSettings::SetFeeRate(Amount feeRate)
    {
        m_feeRate = feeRate;
    }

    void BitcoinSettings::SetMinFeeRate(beam::Amount feeRate)
    {
        m_minFeeRate = feeRate;
    }

    void BitcoinSettings::SetTxMinConfirmations(uint16_t txMinConfirmations)
    {
        m_txMinConfirmations = txMinConfirmations;
    }

    void BitcoinSettings::SetLockTimeInBlocks(uint32_t lockTimeInBlocks)
    {
        m_lockTimeInBlocks = lockTimeInBlocks;
    }

    void BitcoinSettings::SetChainType(wallet::SwapSecondSideChainType chainType)
    {
        m_chainType = chainType;
    }
}