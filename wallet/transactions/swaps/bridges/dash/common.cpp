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
    constexpr uint8_t kDashMainnetP2KH = 76;
    constexpr uint8_t kDashRegtestP2KH = 140;
}

namespace beam::dash
{
    const char kMainnetGenesisBlockHash[] = "00000ffd590b1485b3caadc19b22e6379c733355108f107a430458cdf3407ab6";
    const char kTestnetGenesisBlockHash[] = "00000bafbc94add76cb75e2ec92894837288a481e5c005f6563d91623bf8bc2c";
    const char kRegtestGenesisBlockHash[] = "000008ca1832a4baf228eb1553c03d3a2c8e02399550dd6ea8d65cec3ef23d2e";

    uint8_t getAddressVersion()
    {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
        return kDashMainnetP2KH;
#else
        return kDashRegtestP2KH;
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
} // namespace beam::dash
