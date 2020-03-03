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
    {
    public:
        NotificationCenter(IWalletDB& storage, const std::map<Notification::Type,bool>& activeNotifications);

        std::vector<Notification> getNotifications() const;
        void markNotificationAsRead(const ECC::uintBig& notificationID);
        void deleteNotification(const ECC::uintBig& notificationID);

        void switchOnOffNotifications(Notification::Type, bool);
        size_t getUnreadCount() const;

        void Subscribe(INotificationsObserver* observer);
        void Unsubscribe(INotificationsObserver* observer);

        // INewsObserver implementation
        virtual void onNewWalletVersion(const VersionInfo&, const ECC::uintBig&) override;

        // TODO interface for wallet transactions and addresses changes listening

    private:
        void notifySubscribers(ChangeAction, const std::vector<Notification>&) const;
        bool isNotificationTypeActive(Notification::Type) const;
        void loadToCache();
        void createNotification(const Notification&);
        void updateNotification(const Notification&);

        IWalletDB& m_storage;
        std::map<Notification::Type, bool> m_activeNotifications;
        std::unordered_map<ECC::uintBig, Notification> m_cache;
        std::vector<INotificationsObserver*> m_subscribers;
    };
} // namespace beam::wallet
