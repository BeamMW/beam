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
#include "wallet/common.h"
#include "wallet/wallet_db.h"
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
        std::string m_address;
        std::vector<std::string> m_secretWords;
        bool m_automaticChooseAddress = true;

        std::vector<std::string> m_nodeAddresses
        {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
            "104.248.139.211:50002",
            "148.251.22.104:50002",
            "157.245.172.236:50002",
            "167.172.42.31:50002",
            "178.62.80.20:50002",
            "2AZZARITA.hopto.org:50006",
            "52.1.56.181:50002",
            "Bitkoins.nl:50512",
            "bitcoin.grey.pw:50004",
            "bitcoin.lukechilds.co:50002",
            "bitcoin.lukechilds.co:50002",
            "blkhub.net:50002",
            "btc.electroncash.dk:60002",
            "btc.litepay.ch:50002",
            "btc.usebsv.com:50006",
            "e5a8f0d103c23.not.fyi:50002",
            "electrum.hodl-shop.io:50002",
            "electrum.jochen-hoenicke.de:50005",
            "electrum.networkingfanatic.com:50002",
            "electrum.vom-stausee.de:50002",
            "electrum1.privateservers.network:50002",
            "electrum2.privateservers.network:50002",
            "electrumx.electricnewyear.net:50002",
            "electrumx.schulzemic.net:50002",
            "exs.ignorelist.com:50002",
            "fortress.qtornado.com:443",
            "hamsterlove.ddns.net:50002",
            "ns3079943.ip-217-182-196.eu:50002",
            "ns3079944.ip-217-182-196.eu:50002",
            "thanos.xskyx.net:50002",
            "vps4.hsmiths.com:50002",
            "xtrum.com:50002",
            "satoshi.fan:50002"
#else // MASTERNET and TESTNET
            "testnet.qtornado.com:51002",
            "bitcoin.cluelessperson.com:51002",
            "testnet.hsmiths.com:53012",
            "testnet1.bauerj.eu:50002",
            "tn.not.fyi:55002"
#endif
        };

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
                && m_automaticChooseAddress == other.m_automaticChooseAddress;
        }

        bool operator != (const ElectrumSettings& other) const
        {
            return !(*this == other);
        }
    };

    class ISettings
    {
    public:
        using Ptr = std::shared_ptr<ISettings>;

        enum class ConnectionType : uint8_t
        {
            None,
            Core,
            Electrum
        };
        
        virtual ~ISettings() {};

        virtual BitcoinCoreSettings GetConnectionOptions() const = 0;
        virtual bool IsCoreActivated() const = 0;
        virtual ElectrumSettings GetElectrumConnectionOptions() const = 0;
        virtual bool IsElectrumActivated() const = 0;
        virtual Amount GetFeeRate() const = 0;
        virtual uint16_t GetTxMinConfirmations() const = 0;
        virtual uint32_t GetLockTimeInBlocks() const = 0;
        virtual bool IsInitialized() const = 0;
        virtual bool IsActivated() const = 0;
        virtual ConnectionType GetCurrentConnectionType() const = 0;
        virtual double GetBlocksPerHour() const = 0;
        virtual uint8_t GetAddressVersion() const = 0;
        virtual std::vector<std::string> GetGenesisBlockHashes() const = 0;
    };

    boost::optional<ISettings::ConnectionType> from_string(const std::string&);
    std::string to_string(ISettings::ConnectionType);

    class Settings : public ISettings
    {
    public:
       // Settings() = default;
       // ~Settings() = default;
        BitcoinCoreSettings GetConnectionOptions() const override;
        bool IsCoreActivated() const override;
        ElectrumSettings GetElectrumConnectionOptions() const override;
        bool IsElectrumActivated() const override;
        Amount GetFeeRate() const override;
        uint16_t GetTxMinConfirmations() const override;
        uint32_t GetLockTimeInBlocks() const override;
        bool IsInitialized() const override;
        bool IsActivated() const override;
        ConnectionType GetCurrentConnectionType() const override;
        double GetBlocksPerHour() const override;
        uint8_t GetAddressVersion() const override;
        std::vector<std::string> GetGenesisBlockHashes() const override;

        void SetConnectionOptions(const BitcoinCoreSettings& connectionSettings);
        void SetElectrumConnectionOptions(const ElectrumSettings& connectionSettings);
        void SetFeeRate(Amount feeRate);
        void SetTxMinConfirmations(uint16_t txMinConfirmations);
        void SetLockTimeInBlocks(uint32_t lockTimeInBlocks);
        void ChangeConnectionType(ConnectionType type);
        void SetBlocksPerHour(double beamBlocksPerBlock);
        void SetAddressVersion(uint8_t addressVersion);
        void SetGenesisBlockHashes(const std::vector<std::string>& genesisBlockHashes);

    protected:
        BitcoinCoreSettings m_connectionSettings;
        ElectrumSettings m_electrumConnectionSettings;
        ConnectionType m_connectionType = ConnectionType::None;
        Amount m_feeRate = 90000;
        // They are not stored in DB
        uint16_t m_txMinConfirmations = 6;
        uint32_t m_lockTimeInBlocks = 12 * 6;  // 12h
        double m_blocksPerHour = 6;
        uint8_t m_addressVersion = getAddressVersion();
        std::vector<std::string> m_genesisBlockHashes = getGenesisBlockHashes();
    };
} // namespace beam::bitcoin
