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

#include "settings.h"
#include "bitcoin/bitcoin.hpp"

namespace beam::bitcoin
{
    std::string BitcoinCoreSettings::generateAuthorization()
    {
        std::string userWithPass(m_userName + ":" + m_pass);
        libbitcoin::data_chunk t(userWithPass.begin(), userWithPass.end());
        return std::string("Basic " + libbitcoin::encode_base64(t));
    }

    BitcoinCoreSettings Settings::GetConnectionOptions() const
    {
        return m_connectionSettings;
    }

    ElectrumSettings Settings::GetElectrumConnectionOptions() const
    {
        return m_electrumConnectionSettings;
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

    bool Settings::IsInitialized() const
    {
        return m_connectionSettings.IsInitialized() || m_electrumConnectionSettings.IsInitialized();
    }

    void Settings::SetConnectionOptions(const BitcoinCoreSettings& connectionSettings)
    {
        m_connectionSettings = connectionSettings;
    }

    void Settings::SetElectrumConnectionOptions(const ElectrumSettings& connectionSettings)
    {
        m_electrumConnectionSettings = connectionSettings;
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
} // namespace beam::bitcoin