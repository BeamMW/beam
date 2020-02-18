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

#include "notification_center.h"

#include "utility/logger.h"

namespace beam::wallet
{
    NotificationCenter::NotificationCenter(IWalletDB& storage)
        : m_storage(storage)
    {
        LOG_DEBUG() << "NotificationCenter()";
    }
    
    void NotificationCenter::loadToCache()
    {
        LOG_DEBUG() << "loadToCache()";
    }

    void NotificationCenter::store(const Notification& notification)
    {
        m_storage.saveNotification(notification);
    }

    std::vector<Notification> NotificationCenter::getNotifications()
    {
        LOG_DEBUG() << "getNotifications()";
        return {};
    }

    void NotificationCenter::onNewWalletVersion(const VersionInfo&)
    {
        LOG_DEBUG() << "onNewWalletVersion()";
    }

    void NotificationCenter::Subscribe(INotificationsObserver* observer)
    {
        assert(
            std::find(std::begin(m_subscribers),
                      std::end(m_subscribers),
                      observer) == std::end(m_subscribers));

        m_subscribers.push_back(observer);
    }

    void NotificationCenter::Unsubscribe(INotificationsObserver* observer)
    {
        auto it = std::find(std::begin(m_subscribers),
                            std::end(m_subscribers),
                            observer);

        assert(it != std::end(m_subscribers));

        m_subscribers.erase(it);
    }

    void NotificationCenter::notifySubscribers(ChangeAction action, const std::vector<Notification>& notifications) const
    {
        for (const auto sub : m_subscribers)
        {
                sub->onNotificationsChanged(action, std::vector<Notification>{notifications});
        }
    }

} // namespace beam::wallet
