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
    class VerificationProvider
        : public IBroadcastListener
    {
    public:
        VerificationProvider(IBroadcastMsgGateway&, BroadcastMsgValidator&, IWalletDB&);
        virtual ~VerificationProvider() = default;

        bool onMessage(BroadcastMsg&&) override;
        void Subscribe(IVerificationObserver* observer);
        void Unsubscribe(IVerificationObserver* observer);
        std::vector<VerificationInfo> getInfo() const;

    private:
        void loadCache();
        void onUpdateTimer();
        void notifySubscribers(const std::vector<VerificationInfo>&) const;
        void processInfo(const std::vector<VerificationInfo>& info);

		IBroadcastMsgGateway& m_broadcastGateway;
        BroadcastMsgValidator& m_validator;
        IWalletDB& m_storage;
        std::vector<IVerificationObserver*> m_subscribers;
        std::map<Asset::ID, VerificationInfo> m_cache;
        std::set<Asset::ID> m_changed;
        io::Timer::Ptr m_updateTimer;
    };
} // namespace beam::wallet
