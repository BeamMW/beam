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

#include "update_info_provider.h"

#include "version.h"

UpdateInfoProvider::UpdateInfoProvider()
    : m_walletModel(*AppModel::getInstance().getWallet())
    , m_settings(AppModel::getInstance().getSettings())
{
    connect(&m_walletModel, SIGNAL(newAppVersion(const QString&)), SLOT(onNewAppVersion(const QString&)));
}

void UpdateInfoProvider::onNewAppVersion(const QString& msg)
{
    emit showUpdateNotification(msg);
}
