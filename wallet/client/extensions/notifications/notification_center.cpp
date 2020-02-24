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

        loadToCache();
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

    void NotificationCenter::createNotification(const Notification& notification)
    {
        LOG_DEBUG() << "createNotification()";

        m_cache[notification.m_ID] = notification;
        m_storage.saveNotification(notification);

        notifySubscribers(ChangeAction::Added, {notification});
    }

    void NotificationCenter::updateNotification(const Notification& notification)
    {
        LOG_DEBUG() << "updateNotification()";

        const auto it = m_cache.find(notification.m_ID);
        if (it != std::cend(m_cache))
        {
            m_cache[notification.m_ID] = notification;
            m_storage.saveNotification(notification);

            notifySubscribers(ChangeAction::Updated, {notification});
        }
    }

    void NotificationCenter::markNotificationAsRead(const ECC::uintBig& notificationID)
    {
        LOG_DEBUG() << "markNotificationAsRead()";

        auto search = m_cache.find(notificationID);
        if (search != m_cache.cend() && search->second.m_state == Notification::State::Unread)
        {
            search->second.m_state = Notification::State::Read;
            updateNotification(search->second);
        }
    }
    
    void NotificationCenter::deleteNotification(const ECC::uintBig& notificationID)
    {
        LOG_DEBUG() << "deleteNotification()";

        auto search = m_cache.find(notificationID);
        if (search != m_cache.cend())
        {
            search->second.m_state = Notification::State::Deleted;
            m_storage.saveNotification(search->second);

            notifySubscribers(ChangeAction::Removed, { search->second });

            m_cache.erase(search);
        }
    }

    std::vector<Notification> NotificationCenter::getNotifications() const
    {
        LOG_DEBUG() << "NotificationCenter::getNotifications()";

        std::vector<Notification> notifications;

        for (const auto& pair : m_cache)
        {
            if (pair.second.m_state == Notification::State::Deleted) continue;
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
        n.m_state = Notification::State::Unread;
        n.m_content = toByteBuffer(content);
        
        createNotification(n);
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
