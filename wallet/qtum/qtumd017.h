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

#include "../bitcoin/bitcoind017.h"
#include "options.h"

namespace beam
{
    class Qtumd017 : public Bitcoind017
    {
    public:
        Qtumd017() = delete;
        Qtumd017(io::Reactor& reactor, const QtumOptions& options);

        uint8_t getAddressVersion() override;
        std::string getCoinName() const override;
    };
}