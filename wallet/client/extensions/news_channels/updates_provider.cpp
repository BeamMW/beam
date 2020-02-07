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

#include "updates_provider.h"

#include "utility/logger.h"

namespace beam::wallet
{
    AppUpdateInfoProvider::AppUpdateInfoProvider(
        IBroadcastMsgsGateway& broadcastGateway,
        BroadcastMsgValidator& validator)
        : m_broadcastGateway(broadcastGateway),
          m_validator(validator)
    {
        m_broadcastGateway.registerListener(BroadcastContentType::SoftwareUpdates, this);
    }

    bool AppUpdateInfoProvider::onMessage(uint64_t unused, ByteBuffer&& input)
    {
        try
        {
            BroadcastMsg res;
            if (m_validator.processMessage(input, res))
            {
                VersionInfo updateInfo;
                if (fromByteBuffer(res.m_content, updateInfo))
                {
                    notifySubscribers(updateInfo);
                }
            }
        }
        catch(...)
        {
            LOG_WARNING() << "broadcast message processing exception";
        }
        return false;
    }

    void AppUpdateInfoProvider::Subscribe(INewsObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(),
                            m_subscribers.end(),
                            observer);
        assert(it == m_subscribers.end());
        if (it == m_subscribers.end()) m_subscribers.push_back(observer);
    }

    void AppUpdateInfoProvider::Unsubscribe(INewsObserver* observer)
    {
        auto it = std::find(m_subscribers.begin(),
                            m_subscribers.end(),
                            observer);
        assert(it != m_subscribers.end());
        m_subscribers.erase(it);
    }

    void AppUpdateInfoProvider::notifySubscribers(const VersionInfo& info) const
    {
        for (const auto sub : m_subscribers)
        {
            sub->onNewWalletVersion(info);
        }
    }

} // namespace beam::wallet
