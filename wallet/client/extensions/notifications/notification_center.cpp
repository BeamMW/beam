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
    /**
     *  @storage                used to store notifications
     *  @activeNotifications    shows which of Notification::Type are active
     */
    NotificationCenter::NotificationCenter(
        IWalletDB& storage, const std::map<Notification::Type,bool>& activeNotifications, io::Reactor::Ptr reactor)
        : m_storage(storage)
        , m_activeNotifications(activeNotifications)
        , m_checkoutTimer(io::Timer::create(*reactor))
    {
        loadToCache();
        m_myAddresses = m_storage.getAddresses(true);
        m_checkoutTimer->start(3000/*3sec*/, true/*periodic*/, [this]() { checkAddressesExpirationTime(); });
    }
    
    void NotificationCenter::loadToCache()
    {
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

    bool NotificationCenter::isNotificationTypeActive(Notification::Type type) const
    {
        auto it = m_activeNotifications.find(type);
        assert(it != std::cend(m_activeNotifications));
        return it->second;
    }

    void NotificationCenter::switchOnOffNotifications(Notification::Type type, bool onOff)
    {
        m_activeNotifications[type] = onOff;
        notifySubscribers(ChangeAction::Reset, getNotifications());
    }

    size_t NotificationCenter::getUnreadCount(
        std::function<size_t(std::vector<Notification>::const_iterator, std::vector<Notification>::const_iterator)> counter)
    {
        const auto& notifications = getNotifications();
        return counter(notifications.begin(), notifications.end());
    }

    void NotificationCenter::createNotification(const Notification& notification)
    {
        auto it = m_cache.insert(std::make_pair(notification.m_ID, notification));
        if (it.second == false) // we already have such a notification in the cache
        {
            return; // ignore
        }
        m_storage.saveNotification(notification);

        if (isNotificationTypeActive(notification.m_type))
        {
            notifySubscribers(ChangeAction::Added, {notification});
        }
    }

    void NotificationCenter::updateNotification(const Notification& notification)
    {
        const auto it = m_cache.find(notification.m_ID);
        if (it != std::cend(m_cache))
        {
            m_cache[notification.m_ID] = notification;
            m_storage.saveNotification(notification);

            if (isNotificationTypeActive(notification.m_type))
            {
                notifySubscribers(ChangeAction::Updated, {notification});
            }
        }
    }

    void NotificationCenter::markNotificationAsRead(const ECC::uintBig& notificationID)
    {
        auto search = m_cache.find(notificationID);
        if (search != m_cache.cend() && search->second.m_state == Notification::State::Unread)
        {
            search->second.m_state = Notification::State::Read;
            updateNotification(search->second);
        }
    }
    
    void NotificationCenter::deleteNotification(const ECC::uintBig& notificationID)
    {
        auto search = m_cache.find(notificationID);
        if (search != m_cache.cend())
        {
            auto& notification = search->second;

            notification.m_state = Notification::State::Deleted;
            m_storage.saveNotification(notification);

            if (isNotificationTypeActive(notification.m_type))
            {
                notifySubscribers(ChangeAction::Removed, { notification });
            }
        }
    }

    std::vector<Notification> NotificationCenter::getNotifications()
    {
        checkAddressesExpirationTime();

        std::vector<Notification> notifications;

        for (const auto& pair : m_cache)
        {
            if (!isNotificationTypeActive(pair.second.m_type))
                continue;
            if (pair.second.m_type != Notification::Type::SoftwareUpdateAvailable // we do not filter out deleted software update notifications
                && pair.second.m_type != Notification::Type::WalletImplUpdateAvailable
                && pair.second.m_state == Notification::State::Deleted)
                continue;
            notifications.push_back(pair.second);
        }

        return notifications;
    }

    void NotificationCenter::onNewWalletVersion(const VersionInfo& content, const ECC::uintBig& id)
    {
        auto search = m_cache.find(id);
        if (search == m_cache.cend())
        {
            createNotification(
                Notification {
                    id,
                    Notification::Type::SoftwareUpdateAvailable,
                    Notification::State::Unread,
                    getTimestamp(),
                    toByteBuffer(content)
                });
        }
    }

    void NotificationCenter::onNewWalletVersion(const WalletImplVerInfo& content, const ECC::uintBig& id)
    {
        auto search = m_cache.find(id);
        if (search == m_cache.cend())
        {
            createNotification(
                Notification {
                    id,
                    Notification::Type::WalletImplUpdateAvailable,
                    Notification::State::Unread,
                    getTimestamp(),
                    toByteBuffer(content)
                });
        }
    }

    void NotificationCenter::onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items)
    {
        if (action == ChangeAction::Added || action == ChangeAction::Updated)
        {
            for (const auto& item : items)
            {
                if (item.m_txType == TxType::Contract)
                {
                    // no notifications for contracts at the moment
                    continue;
                }

                Asset::ID assetId = Asset::s_InvalidID;
                if (item.GetParameter(TxParameterID::AssetID, assetId))
                {
                    if (assetId != Asset::s_BeamID)
                    {
                        // GUI wallet doesn't support asset transactions at the moment
                        // So we just block all asset-related notifications.
                        // The only way to get asset-based tx in GUI when somebody
                        // sends asset. Such transactions would automatically fail
                        // and we do not want notifications about them
                        continue;
                    }
                }

                bool failed = (item.m_status == TxStatus::Failed || item.m_status == TxStatus::Canceled);
                if (!failed && item.m_status != TxStatus::Completed)
                {
                    continue;
                }
                const auto& id = item.GetTxID();
                ECC::Hash::Value hv;
                ECC::Hash::Processor() << Blob(id->data(), static_cast<uint32_t>(id->size())) << uint32_t(item.m_status) >> hv;
                
                createNotification(
                    Notification {
                        hv,
                        failed ? Notification::Type::TransactionFailed : Notification::Type::TransactionCompleted,
                        Notification::State::Unread,
                        getTimestamp(),
                        toByteBuffer(TxToken(item))
                    });
            }
        }
    }

    void NotificationCenter::onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items)
    {
        if (action != ChangeAction::Removed)
        {
            for (const auto& item : items)
            {
                if (!item.isOwn()) continue;

                ECC::uintBig id = Cast::Down<ECC::uintBig>(item.m_walletID.m_Pk);
                auto it = m_cache.find(id);

                if (item.isExpired())
                {
                    Notification n {
                        id,
                        Notification::Type::AddressStatusChanged,
                        Notification::State::Unread,
                        getTimestamp(),
                        toByteBuffer(item)
                    };

                    if (it == std::cend(m_cache))
                    {
                        createNotification(n);
                    }
                    else
                    {
                        updateNotification(n);
                    }
                }
                else
                {
                    // check if address was activated and update notification content to show it
                    if (it != std::cend(m_cache))
                    {
                        it->second.m_content = toByteBuffer(item);
                        updateNotification(it->second);
                    }
                }
            }
        }
        updateMyAddresses(action, items);
    }

    void NotificationCenter::updateMyAddresses(ChangeAction action, const std::vector<WalletAddress>& addresses)
    {
        if (action == ChangeAction::Reset)
        {
            m_myAddresses.clear();
        }

        for (const auto& address : addresses)
        {
            switch (action)
            {
                case ChangeAction::Removed:
                {
                    auto it = std::find_if(
                        std::cbegin(m_myAddresses),
                        std::cend(m_myAddresses),
                        [&address](const WalletAddress& a) -> bool
                        {
                            return a.m_walletID == address.m_walletID;
                        });
                    if (it != std::cend(m_myAddresses))
                    {
                        m_myAddresses.erase(it);
                    }
                    break;
                }
                case ChangeAction::Updated:
                {
                    auto it = std::find_if(
                        std::cbegin(m_myAddresses),
                        std::cend(m_myAddresses),
                        [&address](const WalletAddress& a) -> bool
                        {
                            return a.m_walletID == address.m_walletID;
                        });
                    if (it != std::cend(m_myAddresses))
                    {
                        m_myAddresses.erase(it);
                        m_myAddresses.emplace_back(address);
                    }
                    break;
                }
                case ChangeAction::Added:   // same for both actions
                case ChangeAction::Reset:
                    m_myAddresses.emplace_back(address);
                    break;
            }
        }
    }

    void NotificationCenter::checkAddressesExpirationTime()
    {
        for (const auto& address : m_myAddresses)
        {
            if (address.isExpired())
            {
                ECC::uintBig id = Cast::Down<ECC::uintBig>(address.m_walletID.m_Pk);
                auto it = m_cache.find(id);
                
                // No notification for expiration of this address
                if (it == std::cend(m_cache))
                {
                    // creating new notification
                    createNotification(
                        Notification {
                            id,
                            Notification::Type::AddressStatusChanged,
                            Notification::State::Unread,
                            getTimestamp(),
                            toByteBuffer(address)
                        });
                }
                // There is the notification already
                else
                {
                    WalletAddress cachedAddress;
                    if (fromByteBuffer(it->second.m_content, cachedAddress) &&
                        !cachedAddress.isExpired())
                    {
                        // updating existent notification
                        updateNotification(
                            Notification {
                                id,
                                Notification::Type::AddressStatusChanged,
                                Notification::State::Unread,
                                getTimestamp(),
                                toByteBuffer(address)
                            });
                    }
                }
            }
        }
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
            sub->onNotificationsChanged(action, notifications);
        }
    }

} // namespace beam::wallet
