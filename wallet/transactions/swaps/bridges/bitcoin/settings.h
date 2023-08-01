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

#pragma once

#include "core/serialization_adapters.h"
#include "utility/io/address.h"
#include "utility/common.h"
#include "wallet/core/common.h"
#include "wallet/core/wallet_db.h"
#include "common.h"

namespace beam::bitcoin
{
    struct BitcoinCoreSettings
    {
        std::string m_userName;
        std::string m_pass;
        io::Address m_address;

        std::string generateAuthorization();

        bool IsInitialized() const
        {
            return !m_userName.empty() && !m_pass.empty() && !m_address.empty();
        }

        bool operator == (const BitcoinCoreSettings& other) const
        {
            return m_userName == other.m_userName && m_pass == other.m_pass && m_address == other.m_address;
        }

        bool operator != (const BitcoinCoreSettings& other) const
        {
            return !(*this == other);
        }
    };

    struct ElectrumSettings
    {
        static std::vector<std::string> get_DefaultAddresses();

        std::string m_address;
        std::vector<std::string> m_secretWords;
        bool m_automaticChooseAddress = true;

        std::vector<std::string> m_nodeAddresses = get_DefaultAddresses();

        uint32_t m_receivingAddressAmount = 21;
        uint32_t m_changeAddressAmount = 6;

        bool IsInitialized() const
        {
            return !m_secretWords.empty() &&
                ((!m_address.empty() && !m_automaticChooseAddress) || (m_automaticChooseAddress && m_nodeAddresses.size() > 0));
        }

        bool operator == (const ElectrumSettings& other) const
        {
            return m_address == other.m_address
                && m_secretWords == other.m_secretWords
                && m_automaticChooseAddress == other.m_automaticChooseAddress
                && m_receivingAddressAmount == other.m_receivingAddressAmount
                && m_changeAddressAmount == other.m_changeAddressAmount;
        }

        bool operator != (const ElectrumSettings& other) const
        {
            return !(*this == other);
        }
    };

    class Settings
    {
    public:
        enum class ConnectionType : uint8_t
        {
            None,
            Core,
            Electrum
        };

       // Settings() = default;
       // ~Settings() = default;
        BitcoinCoreSettings GetConnectionOptions() const;
        bool IsCoreActivated() const;
        ElectrumSettings GetElectrumConnectionOptions() const;
        bool IsElectrumActivated() const;
        Amount GetMinFeeRate() const;
        Amount GetMaxFeeRate() const;
        uint16_t GetLockTxMinConfirmations() const;
        uint16_t GetWithdrawTxMinConfirmations() const;
        uint32_t GetLockTimeInBlocks() const;
        bool IsInitialized() const;
        bool IsActivated() const;
        ConnectionType GetCurrentConnectionType() const;
        double GetBlocksPerHour() const;
        uint8_t GetAddressVersion() const;
        std::vector<std::string> GetGenesisBlockHashes() const;
        bool IsSupportedElectrum() const;

        void ChangeConnectionType(ConnectionType type);
        void SetConnectionOptions(const BitcoinCoreSettings& connectionSettings);
        void SetElectrumConnectionOptions(const ElectrumSettings& connectionSettings);

    protected:
        void SetMinFeeRate(Amount feeRate);
        void SetMaxFeeRate(Amount feeRate);
        void SetLockTxMinConfirmations(uint16_t txMinConfirmations);
        void SetWithdrawTxMinConfirmations(uint16_t txMinConfirmations);
        void SetLockTimeInBlocks(uint32_t lockTimeInBlocks);
        void SetBlocksPerHour(double beamBlocksPerBlock);
        void SetAddressVersion(uint8_t addressVersion);
        void SetGenesisBlockHashes(const std::vector<std::string>& genesisBlockHashes);
        void DisableElectrum();

    protected:
        BitcoinCoreSettings m_connectionSettings;
        ElectrumSettings m_electrumConnectionSettings;
        ConnectionType m_connectionType = ConnectionType::None;
        // They are not stored in DB
        Amount m_minFeeRate = 20'000u;
        Amount m_maxFeeRate = 1'000'000u; // COIN / 100
        uint16_t m_lockTxMinConfirmations = 1;
        uint16_t m_withdrawTxMinConfirmations = 1;
        uint32_t m_lockTimeInBlocks = 12 * 6;  // 12h
        double m_blocksPerHour = 6;
        uint8_t m_addressVersion = getAddressVersion();
        std::vector<std::string> m_genesisBlockHashes = getGenesisBlockHashes();
        bool m_isSupportedElectrum = true;
    };

    boost::optional<Settings::ConnectionType> from_string(const std::string&);
    std::string to_string(Settings::ConnectionType);
} // namespace beam::bitcoin
