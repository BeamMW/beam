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

#include "bitcoin/bitcoin.hpp"

namespace
{
    constexpr uint8_t kDogecoinMainnetP2KH = 30;
}

namespace beam::dogecoin
{
    const char kMainnetGenesisBlockHash[] = "1a91e3dace36e2be3bf030a65679fe821aa1d6ef92e7c9902eb318182c355691";
    const char kTestnetGenesisBlockHash[] = "bb0a78264637406b6360aad926284d544d7049f45189db5664f3c4d07350559e";
    const char kRegtestGenesisBlockHash[] = "3d2160a3b5dc4a9d62e7e66a295f70313ac808440ef7400d6c0772171ce973a5";

    uint8_t getAddressVersion()
    {
        return wallet::UseMainnetSwap() ?
            kDogecoinMainnetP2KH :
            libbitcoin::wallet::ec_private::testnet_p2kh;
    }

    std::vector<std::string> getGenesisBlockHashes()
    {
        if (wallet::UseMainnetSwap())
            return { kMainnetGenesisBlockHash };

        return { kTestnetGenesisBlockHash , kRegtestGenesisBlockHash };
    }
} // namespace beam::dogecoin
