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

#include "denarius_core_017.h"

#include "bitcoin/bitcoin.hpp"

namespace beam::denarius
{
    DenariusCore017::DenariusCore017(io::Reactor& reactor, ISettingsProvider& settingsProvider)
        : bitcoin::BitcoinCore017(reactor, settingsProvider)
    {
    }

    std::string DenariusCore017::getCoinName() const
    {
        return "denarius";
    }
} // namespace beam::denarius