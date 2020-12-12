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
        std::string m_address;
        std::vector<std::string> m_secretWords;
        bool m_automaticChooseAddress = true;

        std::vector<std::string> m_nodeAddresses
        {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
            "213.109.162.82:50002",
            "109.248.206.13:50002",
            "ragtor.duckdns.org:50002",
            "electrumx.papabyte.com:50002",
            "ttbit.mine.bz:50002",
            "68.183.188.105:50002",
            "ecdsa.net:110",
            "165.73.108.186:50005",
            "1electrumx.hopto.me:50002",
            "104.248.139.211:50002",
            "142.93.6.38:50002",
            "157.245.172.236:50002",
            "178.62.80.20:50002",
            "2AZZARITA.hopto.org:50006",
            "51.68.207.75:50002",
            "65.27.134.105:50002",
            "alviss.coinjoined.com:50002",
            "btc.electrum.bitbitnet.net:50002",
            "btc.litepay.ch:50002",
            "e5a8f0d103c23.not.fyi:50002",
            "electrum.coinext.com.br:50002",
            "electrum.emzy.de:50002",
            "electrum-btc.leblancnet.us:50002",
            "electrum.snekash.io:50002",
            "electrum.syngularity.es:50002",
            "electrum2.privateservers.network:50002",
            "electrumx.erbium.eu:50002",
            "electrumx.schulzemic.net:50002",
            "electrumx.ultracloud.tk:50002",
            "gall.pro:50002",
            "hodlers.beer:50002",
            "node1.btccuracao.com:50002",
            "skbxmit.coinjoined.com:50002",
            "stavver.dyshek.org:50002",
            "vmd63185.contaboserver.net:50002",
            "xtrum.com:50002"
#else // MASTERNET and TESTNET
            "testnet.hsmiths.com:53012",
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
        uint16_t GetTxMinConfirmations() const;
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
        void SetTxMinConfirmations(uint16_t txMinConfirmations);
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
        Amount m_minFeeRate = 1000u;
        // TODO roman.strilet need to investigate
        Amount m_maxFeeRate = 1'000'000u;
        uint16_t m_txMinConfirmations = 6;
        uint32_t m_lockTimeInBlocks = 12 * 6;  // 12h
        double m_blocksPerHour = 6;
        uint8_t m_addressVersion = getAddressVersion();
        std::vector<std::string> m_genesisBlockHashes = getGenesisBlockHashes();
        bool m_isSupportedElectrum = true;
    };

    boost::optional<Settings::ConnectionType> from_string(const std::string&);
    std::string to_string(Settings::ConnectionType);
} // namespace beam::bitcoin
