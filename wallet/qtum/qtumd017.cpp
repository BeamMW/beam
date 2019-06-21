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

#include "qtumd017.h"

#include "bitcoin/bitcoin.hpp"

namespace {
    constexpr uint8_t QtumMainnetP2KH = 0x3a;
    constexpr uint8_t QtumTestnetP2KH = 0x78;
}

namespace beam
{
    Qtumd017::Qtumd017(io::Reactor& reactor, const QtumOptions& options)
        : Bitcoind017(reactor, options)
    {
    }

    uint8_t Qtumd017::getAddressVersion()
    {
        if (isMainnet())
        {
            return QtumMainnetP2KH;
        }

        return QtumTestnetP2KH;
    }

    std::string Qtumd017::getCoinName() const
    {
        return "qtum";
    }
}