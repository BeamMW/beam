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

namespace beam::wallet
{
    class VerificationProvider
        : public IBroadcastListener
    {
    public:
        VerificationProvider(IBroadcastMsgGateway&, BroadcastMsgValidator&);
        virtual ~VerificationProvider() = default;

        // IBroadcastListener implementation
        bool onMessage(uint64_t unused, BroadcastMsg&&) override;

        // INewsObserver interface
        void Subscribe(IVerificationObserver* observer);
        void Unsubscribe(IVerificationObserver* observer);

    private:
        void notifySubscribers(const std::vector<VerificationInfo>&) const;

		IBroadcastMsgGateway& m_broadcastGateway;
        BroadcastMsgValidator& m_validator;
        std::vector<IVerificationObserver*> m_subscribers;
    };
} // namespace beam::wallet
