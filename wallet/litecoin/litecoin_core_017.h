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

#include "wallet/bitcoin/bitcoin_core_017.h"
#include "settings_provider.h"

namespace beam::litecoin
{
    class LitecoinCore017 : public bitcoin::BitcoinCore017
    {
    public:
        LitecoinCore017() = delete;
        LitecoinCore017(io::Reactor& reactor, ILitecoinCoreSettingsProvider& settingsProvider);

    protected:
        std::string getCoinName() const override;
    };
} // namespace beam::litecoin