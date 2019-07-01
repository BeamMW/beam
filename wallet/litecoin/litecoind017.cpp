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

#include "litecoind017.h"

#include "bitcoin/bitcoin.hpp"

namespace {
    constexpr uint8_t kLitecoinMainnetP2KH = 48;
}

namespace beam
{
    Litecoind017::Litecoind017(io::Reactor& reactor, const LitecoinOptions& options)
        : Bitcoind017(reactor, options)
    {
    }

    uint8_t Litecoind017::getAddressVersion()
    {
        if (isMainnet())
        {
            return kLitecoinMainnetP2KH;
        }

        return libbitcoin::wallet::ec_private::testnet_p2kh;
    }

    std::string Litecoind017::getCoinName() const
    {
        return "litecoin";
    }
}