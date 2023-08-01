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
#include "../../common.h"
#include <bitcoin/bitcoin.hpp>

namespace
{
    constexpr uint8_t kQtumMainnetP2KH = 0x3a;
    constexpr uint8_t kQtumTestnetP2KH = 0x78;

    // TODO roman.strilets it's dupplicate (ethereum/common.cpp)
    libbitcoin::wallet::hd_private ProcessHDPrivate(const libbitcoin::wallet::hd_private& privateKey, uint32_t index, bool hard = true)
    {
        static constexpr auto first = libbitcoin::wallet::hd_first_hardened_key;
        const auto position = hard ? first + index : index;
        return privateKey.derive_private(position);
    }
}

namespace beam::qtum
{
    const char kMainnetGenesisBlockHash[] = "000075aef83cf2853580f8ae8ce6f8c3096cfa21d98334d6e3f95e5582ed986c";
    const char kTestnetGenesisBlockHash[] = "0000e803ee215c0684ca0d2f9220594d3f828617972aad66feb2ba51f5e14222";
    const char kRegtestGenesisBlockHash[] = "665ed5b402ac0b44efc37d8926332994363e8a7278b7ee9a58fb972efadae943";

    uint8_t getAddressVersion()
    {
        return wallet::UseMainnetSwap() ?
            kQtumMainnetP2KH :
            kQtumTestnetP2KH;
    }

    std::vector<std::string> getGenesisBlockHashes()
    {
        if (wallet::UseMainnetSwap())
            return { kMainnetGenesisBlockHash };

        return { kTestnetGenesisBlockHash , kRegtestGenesisBlockHash };
    }

    std::pair<libbitcoin::wallet::hd_private, libbitcoin::wallet::hd_private> generateElectrumMasterPrivateKeys(const std::vector<std::string>& words)
    {
        auto seed = libbitcoin::wallet::decode_mnemonic(words);
        libbitcoin::data_chunk seed_chunk(libbitcoin::to_chunk(seed));

        libbitcoin::wallet::hd_private privateKey(seed_chunk, wallet::UseMainnetSwap() ?libbitcoin::wallet::hd_public::mainnet : libbitcoin::wallet::hd_public::testnet);

        privateKey = ProcessHDPrivate(privateKey, 44);

        if (wallet::UseMainnetSwap())
            privateKey = ProcessHDPrivate(privateKey, 88);
        else
            privateKey = ProcessHDPrivate(privateKey, 1);

        privateKey = ProcessHDPrivate(privateKey, 0);
        
        return std::make_pair(privateKey.derive_private(0), privateKey.derive_private(1));
    }
} // namespace beam::qtum
