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

#include "common.h"

#include "bitcoin/bitcoin.hpp"

namespace beam::bitcoin
{
    const char kMainnetGenesisBlockHash[] = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
    const char kTestnetGenesisBlockHash[] = "000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943";
    const char kRegtestGenesisBlockHash[] = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";

    uint64_t btc_to_satoshi(double btc)
    {
        return static_cast<uint64_t>(std::round(btc * libbitcoin::satoshi_per_bitcoin));
    }

    uint8_t getAddressVersion()
    {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
        return libbitcoin::wallet::ec_private::mainnet_p2kh;
#else
        return libbitcoin::wallet::ec_private::testnet_p2kh;
#endif
    }

    std::vector<std::string> getGenesisBlockHashes()
    {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
        return { kMainnetGenesisBlockHash };
#else
        return { kTestnetGenesisBlockHash , kRegtestGenesisBlockHash };
#endif
    }

    bool validateElectrumMnemonic(const std::vector<std::string>& words, bool isSegwitType)
    {
        auto seedType = isSegwitType ? libbitcoin::wallet::electrum::seed::witness : libbitcoin::wallet::electrum::seed::standard;
        return libbitcoin::wallet::electrum::validate_mnemonic(words, seedType);
    }

    std::vector<std::string> createElectrumMnemonic(const std::vector<uint8_t>& entropy)
    {
        return libbitcoin::wallet::electrum::create_mnemonic(entropy);
    }

    std::pair<libbitcoin::wallet::hd_private, libbitcoin::wallet::hd_private> generateElectrumMasterPrivateKeys(const std::vector<std::string>& words)
    {
        auto hd_seed = libbitcoin::wallet::electrum::decode_mnemonic(words);
        libbitcoin::data_chunk seed_chunk(libbitcoin::to_chunk(hd_seed));
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
        libbitcoin::wallet::hd_private masterPrivateKey(seed_chunk, libbitcoin::wallet::hd_public::mainnet);
#else
        libbitcoin::wallet::hd_private masterPrivateKey(seed_chunk, libbitcoin::wallet::hd_public::testnet);
#endif

        return std::make_pair(masterPrivateKey.derive_private(0), masterPrivateKey.derive_private(1));
    }

    std::string getElectrumAddress(const libbitcoin::wallet::hd_private& privateKey, uint32_t index, uint8_t addressVersion)
    {
        libbitcoin::wallet::ec_public publicKey(privateKey.to_public().derive_public(index).point());
        libbitcoin::wallet::payment_address address = publicKey.to_payment_address(addressVersion);

        return address.encoded();
    }

    std::vector<std::string> generateReceivingAddresses(const std::vector<std::string>& words, uint32_t amount, uint8_t addressVersion)
    {
        std::vector<std::string> addresses;
        auto masterKey = generateElectrumMasterPrivateKeys(words).first;

        for (uint32_t index = 0; index < amount; index++)
        {
            addresses.push_back(getElectrumAddress(masterKey, index, addressVersion));
        }
        return addresses;
    }

    std::vector<std::string> generateChangeAddresses(const std::vector<std::string>& words, uint32_t amount, uint8_t addressVersion)
    {
        std::vector<std::string> addresses;
        auto masterKey = generateElectrumMasterPrivateKeys(words).second;

        for (uint32_t index = 0; index < amount; index++)
        {
            addresses.push_back(getElectrumAddress(masterKey, index, addressVersion));
        }
        return addresses;
    }

    bool isAllowedWord(const std::string& word)
    {
        return std::binary_search(libbitcoin::wallet::language::electrum::en.begin(), libbitcoin::wallet::language::electrum::en.end(), word);
    }
} // namespace beam::bitcoin
