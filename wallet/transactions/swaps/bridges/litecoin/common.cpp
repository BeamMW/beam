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

namespace
{
    constexpr uint8_t kLitecoinMainnetP2KH = 48;
}

namespace beam::litecoin
{
    const char kMainnetGenesisBlockHash[] = "12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195a7e7e04bfe2";
    const char kTestnetGenesisBlockHash[] = "4966625a4b2851d9fdee139e56211a0d88575f59ed816ff5e6a63deb4e3e29a0";
    const char kRegtestGenesisBlockHash[] = "530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9";

    uint8_t getAddressVersion()
    {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
        return kLitecoinMainnetP2KH;
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
} // namespace beam::litecoin
