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

#include "wallet/client/extensions/broadcast_gateway/interface.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_validator.h"
#include "wallet/client/extensions/news_channels/interface.h"
#include "wallet/core/wallet_db.h"

namespace beam::wallet
{
    /**
     *  Provides exchange rates from broadcast messages
     */
    class ExchangeRateProvider
        : public IBroadcastListener
    {
    public:
        ExchangeRateProvider(IBroadcastMsgGateway&, BroadcastMsgValidator&, IWalletDB&, bool isEnabled = true);
        virtual ~ExchangeRateProvider() = default;
        std::vector<ExchangeRate> getRates();

        void setOnOff(bool isEnabled);

        // IBroadcastListener implementation
        bool onMessage(uint64_t unused, ByteBuffer&&) override; // TODO: dh remove after 2 fork.
        bool onMessage(uint64_t unused, BroadcastMsg&&) override;
        
        // IExchangeRateObserver interface
        void Subscribe(IExchangeRateObserver* observer);
        void Unsubscribe(IExchangeRateObserver* observer);
        
    private:
        void loadRatesToCache();
        void processRates(const std::vector<ExchangeRate>& rates);
        void notifySubscribers(const std::vector<ExchangeRate>&) const;

        bool m_isEnabled;                           /// Shows if provider is working or turned OFF
		IBroadcastMsgGateway& m_broadcastGateway;
        BroadcastMsgValidator& m_validator;
        IWalletDB& m_storage;
        std::vector<IExchangeRateObserver*> m_subscribers;
        std::map<std::pair<ExchangeRate::Currency,ExchangeRate::Currency>,
                 ExchangeRate> m_cache;
    };
} // namespace beam::wallet
