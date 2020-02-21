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

#include "notifications_view.h"

#include "utility/logger.h"
#include "wallet/client/extensions/news_channels/interface.h"

NotificationsViewModel::NotificationsViewModel()
    : m_walletModel{*AppModel::getInstance().getWallet()}
{
    connect(&m_walletModel,
            SIGNAL(notificationsChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::Notification>&)),
            SLOT(onNotificationsDataModelChanged(beam::wallet::ChangeAction, const std::vector<beam::wallet::Notification>&)));
    
    m_walletModel.getAsync()->getNotifications();
}

void NotificationsViewModel::onNotificationsDataModelChanged(beam::wallet::ChangeAction action, const std::vector<beam::wallet::Notification>& notifications)
{
    // test
    m_notifications.clear();
    for (const auto& n : notifications)
    {
        m_notifications.push_back(n);

        if (n.m_type == beam::wallet::Notification::Type::SoftwareUpdateAvailable)
        {
            beam::wallet::VersionInfo versionInfo;
            bool res = beam::wallet::fromByteBuffer(n.m_content, versionInfo);
            if (res)
            {
                LOG_INFO() << "News version available. Type: " << beam::wallet::VersionInfo::to_string(versionInfo.m_application)
                        << ". Version: " << versionInfo.m_version.to_string();
            }
        }
    }
}
