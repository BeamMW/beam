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
    constexpr uint8_t kDenariusMainnetP2KH = 30; //Decimal Base58 Prefix
}

namespace beam::denarius
{
    const char kMainnetGenesisBlockHash[] = "00000d5dbbda01621cfc16bbc1f9bf3264d641a5dbf0de89fd0182c2c4828fcd";
    const char kTestnetGenesisBlockHash[] = "000086bfe8264d241f7f8e5393f747784b8ca2aa98bdd066278d590462a4fdb4";
    const char kRegtestGenesisBlockHash[] = "530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9";

    uint8_t getAddressVersion()
    {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
        return kDenariusMainnetP2KH;
#else

        //return libbitcoin::wallet::ec_private::testnet_p2kh;
        return kDenariusMainnetP2KH;
#endif
    }

    std::vector<std::string> getGenesisBlockHashes()
    {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
        return { kMainnetGenesisBlockHash };
#else
        //return { kTestnetGenesisBlockHash , kRegtestGenesisBlockHash };
        return { kMainnetGenesisBlockHash };
#endif
    }
} // namespace beam::denarius