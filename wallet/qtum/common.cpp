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

namespace
{
    constexpr uint8_t kQtumMainnetP2KH = 0x3a;
    constexpr uint8_t kQtumTestnetP2KH = 0x78;
}

namespace beam::qtum
{
    const char kMainnetGenesisBlockHash[] = "000075aef83cf2853580f8ae8ce6f8c3096cfa21d98334d6e3f95e5582ed986c";
    const char kTestnetGenesisBlockHash[] = "0000e803ee215c0684ca0d2f9220594d3f828617972aad66feb2ba51f5e14222";
    const char kRegtestGenesisBlockHash[] = "665ed5b402ac0b44efc37d8926332994363e8a7278b7ee9a58fb972efadae943";

    uint8_t getAddressVersion()
    {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
        return kQtumMainnetP2KH;
#else
        return kQtumTestnetP2KH;
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
} // namespace beam::qtum
