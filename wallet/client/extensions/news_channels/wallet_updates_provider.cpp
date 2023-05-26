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

#include "wallet_updates_provider.h"

#include "wallet/core/common.h"
#include "utility/logger.h"

namespace beam::wallet
{
    WalletUpdatesProvider::WalletUpdatesProvider(
        IBroadcastMsgGateway& broadcastGateway,
        BroadcastMsgValidator& validator)
        : m_broadcastGateway(broadcastGateway),
          m_validator(validator)
    {
        m_broadcastGateway.registerListener(BroadcastContentType::WalletUpdates, this);
    }

    bool WalletUpdatesProvider::onMessage(BroadcastMsg&& msg)
    {
        if (m_validator.isSignatureValid(msg))
        {
            try
            {
                WalletImplVerInfo walletImplementationInfo;
                if (fromByteBuffer(msg.m_content, walletImplementationInfo))
                {
                    ECC::Hash::Value hash;   // use hash like unique ID
                    ECC::Hash::Processor() << Blob(msg.m_content) >> hash;
                    notifySubscribers(walletImplementationInfo, hash);
                }
            }
            catch(...)
            {
                LOG_WARNING() << "broadcast message processing exception";
                return false;
            }
        }
        return true;
    }

    void WalletUpdatesProvider::Subscribe(INewsObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(),
                            m_subscribers.end(),
                            observer);
        assert(it == m_subscribers.end());
        if (it == m_subscribers.end()) m_subscribers.push_back(observer);
    }

    void WalletUpdatesProvider::Unsubscribe(INewsObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(),
                            m_subscribers.end(),
                            observer);
        assert(it != m_subscribers.end());
        m_subscribers.erase(it);
    }

    void WalletUpdatesProvider::notifySubscribers(const WalletImplVerInfo& info, const ECC::uintBig& signature) const
    {
        for (const auto sub : m_subscribers)
        {
            sub->onNewWalletVersion(info, signature);
        }
    }
    
} // namespace beam::wallet
