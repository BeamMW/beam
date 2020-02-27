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

#include <QObject>

#include "model/app_model.h"
#include "viewmodel/notifications/notifications_list.h"

class NotificationsViewModel : public QObject
{
	Q_OBJECT
    Q_PROPERTY(QAbstractItemModel*  readNotifications        READ getReadNotifications        NOTIFY allNotificationsChanged)
    Q_PROPERTY(QAbstractItemModel*  unreadNotifications      READ getUnreadNotifications      NOTIFY allNotificationsChanged)

public:
    NotificationsViewModel();

    QAbstractItemModel* getReadNotifications();
    QAbstractItemModel* getUnreadNotifications();

public slots:
    void onNotificationsDataModelChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::Notification>&);
    
signals:
    void allNotificationsChanged();

private:

    WalletModel& m_walletModel;

    NotificationsList m_readNotificationsList;
    NotificationsList m_unreadNotificationsList;
};
