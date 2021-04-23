// Copyright 2020 The Beam Team
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

#include "wallet/core/exchange_rate.h"
#include "version_info.h"

namespace beam::wallet
{
    /**
     *  Interface for news channels observers. 
     */
    struct INewsObserver
    {
        /**
         *  @content    content of notification (new release information)
         *  @id         unique ID of notification (possibly HASH of content)
         */
        virtual void onNewWalletVersion(const VersionInfo& content, const ECC::uintBig& id) = 0;
        virtual void onNewWalletVersion(const WalletImplVerInfo& content, const ECC::uintBig& id) = 0;
        // virtual void onBeamNews() = 0;
    };

    /**
     *  Interface for exchange rates observers.
     */
    struct IExchangeRatesObserver
    {
        virtual void onExchangeRates(const ExchangeRates&) = 0;
    };

} // namespace beam::wallet
