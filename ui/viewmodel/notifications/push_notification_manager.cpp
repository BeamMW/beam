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

#include "push_notification_manager.h"

#include "viewmodel/ui_helpers.h"

PushNotificationManager::PushNotificationManager()
    : m_walletModel(*AppModel::getInstance().getWallet())
{
    connect(&m_walletModel,
            SIGNAL(notificationsChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::Notification>&)),
            SLOT(onNotificationsChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::Notification>&)));

    m_walletModel.getAsync()->getNotifications();
}

void PushNotificationManager::onNewSoftwareUpdateAvailable(const beam::wallet::VersionInfo& info, const ECC::uintBig& notificationID, bool showPopup)
{
    if (info.m_application == beam::wallet::VersionInfo::Application::DesktopWallet
     && beamui::getCurrentAppVersion() < info.m_version)
    {
        m_hasNewerVersion = true;
        if (showPopup)
        {
            QString newVersion = QString::fromStdString(info.m_version.to_string());
            QString currentVersion = QString::fromStdString(beamui::getCurrentAppVersion().to_string());
            QVariant id = QVariant::fromValue(notificationID);
        
            emit showUpdateNotification(newVersion, currentVersion, id);
        }
    }
}

void PushNotificationManager::onNotificationsChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::Notification>& notifications)
{
    if ((m_firstNotification && action == beam::wallet::ChangeAction::Reset)
        || action == beam::wallet::ChangeAction::Added)
    {
        for (const auto& n : notifications)
        {
            if (n.m_type == beam::wallet::Notification::Type::SoftwareUpdateAvailable)
            {
                beam::wallet::VersionInfo info;
                if (beam::wallet::fromByteBuffer(n.m_content, info))
                {
                    onNewSoftwareUpdateAvailable(info, n.m_ID, n.m_state == beam::wallet::Notification::State::Unread);
                }
            }
        }
        m_firstNotification = false;
    }
}

void PushNotificationManager::onCancelPopup(const QVariant& variantID)
{
    auto id = variantID.value<ECC::uintBig>();
    m_walletModel.getAsync()->markNotificationAsRead(id);
}

bool PushNotificationManager::hasNewerVersion() const
{
    return m_hasNewerVersion;
}