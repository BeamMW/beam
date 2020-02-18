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
        LOG_DEBUG() << "NotificationCenter::loadToCache()";

        std::vector<Notification> notifications;
        try
        {
            notifications = m_storage.getNotifications();
        }
        catch(const std::exception& e)
        {
            LOG_ERROR() << "Notifications loading failed: " << e.what();
        }
        catch(...)
        {
            LOG_ERROR() << "Notifications loading failed.";
        }
        
        for (const auto& notification : notifications)
        {
            m_cache[notification.m_ID] = notification;
        }
    }

    void NotificationCenter::store(const Notification& notification)
    {
        m_cache[notification.m_ID] = notification;
        m_storage.saveNotification(notification);

        // TODO: ChangeAction depending on state
        notifySubscribers(ChangeAction::Updated, {notification});
    }

    std::vector<Notification> NotificationCenter::getNotifications() const
    {
        LOG_DEBUG() << "NotificationCenter::getNotifications()";

        std::vector<Notification> notifications;

        for (const auto& pair : m_cache)
        {
            // if (status == ...)
            notifications.push_back(pair.second);
        }

        return notifications;
    }

    void NotificationCenter::onNewWalletVersion(const VersionInfo& content, const ECC::uintBig& signature)
    {
        LOG_DEBUG() << "NotificationCenter::onNewWalletVersion()";

        Notification n;
        n.m_ID = signature;
        n.m_type = Notification::Type::SoftwareUpdateAvailable;
        n.m_createTime = getTimestamp();
        n.m_read = false;
        n.m_content = toByteBuffer(content);

        store(n);
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
