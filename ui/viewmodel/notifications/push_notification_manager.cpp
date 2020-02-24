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
    , m_settings(AppModel::getInstance().getSettings())
{
    connect(&m_walletModel,
            SIGNAL(newSoftwareUpdateAvailable(const beam::wallet::VersionInfo&, const ECC::uintBig&)),
            SLOT(onNewSoftwareUpdateAvailable(const beam::wallet::VersionInfo&, const ECC::uintBig&)));
}

void PushNotificationManager::onNewSoftwareUpdateAvailable(const beam::wallet::VersionInfo& info, const ECC::uintBig& notificationID)
{
    if (info.m_application == beam::wallet::VersionInfo::Application::DesktopWallet
     && beamui::getCurrentAppVersion() < info.m_version)
    {
        QString newVersion = QString::fromStdString(info.m_version.to_string());
        QString currentVersion = QString::fromStdString(beamui::getCurrentAppVersion().to_string());
        QVariant id = QVariant::fromValue(notificationID);
        
        emit showUpdateNotification(newVersion, currentVersion, id);
    }
}

void PushNotificationManager::onCancelPopup(const QVariant& variantID)
{
    auto id = variantID.value<ECC::uintBig>();
    m_walletModel.getAsync()->markNotificationAsRead(id);
}
