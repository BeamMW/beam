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

#include "wallet/client/extensions/notifications/notification_observer.h"
#include "wallet/client/extensions/news_channels/interface.h"
#include "wallet/core/wallet_db.h"

#include "utility/std_extension.h"

namespace beam::wallet
{
    class NotificationCenter
        : public INewsObserver
        , public IWalletDbObserver
    {

    // TODO dh unittests of address notifications

    public:
        using Cache = std::unordered_map<ECC::uintBig, Notification>;
        NotificationCenter(
            IWalletDB& storage, const std::map<Notification::Type,bool>& activeNotifications, io::Reactor::Ptr reactor);

        std::vector<Notification> getNotifications();
        void markNotificationAsRead(const ECC::uintBig& notificationID);
        void deleteNotification(const ECC::uintBig& notificationID);

        void switchOnOffNotifications(Notification::Type, bool);
        size_t getUnreadCount(
            std::function<size_t(std::vector<Notification>::const_iterator, std::vector<Notification>::const_iterator)> counter);

        void Subscribe(INotificationsObserver* observer);
        void Unsubscribe(INotificationsObserver* observer);

        // INewsObserver implementation
        void onNewWalletVersion(const VersionInfo&, const ECC::uintBig&) override;
        void onNewWalletVersion(const WalletImplVerInfo&, const ECC::uintBig&) override;

        // IWalletDbObserver implementation
        void onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items) override;
        void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;
        
    private:
        void notifySubscribers(ChangeAction, const std::vector<Notification>&) const;
        bool isNotificationTypeActive(Notification::Type) const;
        void loadToCache();
        void createNotification(const Notification&);
        void updateNotification(const Notification&);

        void updateMyAddresses(ChangeAction action, const std::vector<WalletAddress>& addresses);
        void checkAddressesExpirationTime();

        IWalletDB& m_storage;
        std::map<Notification::Type, bool> m_activeNotifications;
        Cache m_cache;
        std::vector<INotificationsObserver*> m_subscribers;

        std::vector<WalletAddress> m_myAddresses;
        io::Timer::Ptr m_checkoutTimer;
    };
} // namespace beam::wallet
