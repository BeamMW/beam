// Copyright 2018 The Beam Team
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

#include "settings.h"
#include <QtQuick>

#include "app_model.h"

using namespace std;

namespace
{
    const char* NodeAddressName = "node/address";

    const char* LocalNodeRun = "localnode/run";
    const char* LocalNodePort = "localnode/port";
    const char* LocalNodeMiningThreads = "localnode/mining_threads";
    const char* LocalNodeVerificationThreads = "localnode/verification_threads";
}

WalletSettings::WalletSettings(const QDir& appDataDir)
    : m_data{ appDataDir.filePath("setting.ini"), QSettings::IniFormat }
    , m_appDataDir{appDataDir}
{

}

string WalletSettings::getWalletStorage() const
{
    return m_appDataDir.filePath("wallet.db").toStdString();
}

string WalletSettings::getBbsStorage() const
{
    return m_appDataDir.filePath("keys.bbs").toStdString();
}

QString WalletSettings::getNodeAddress() const
{
    return m_data.value(NodeAddressName).toString();
}

void WalletSettings::setNodeAddress(const QString& addr)
{
    if (addr != getNodeAddress())
    {
        auto walletModel = AppModel::getInstance()->getWallet();
        if (walletModel)
        {
            walletModel->async->setNodeAddress(addr.toStdString());
        }
        m_data.setValue(NodeAddressName, addr);
        emit nodeAddressChanged();
    }
    
}

void WalletSettings::emergencyReset()
{
    auto walletModel = AppModel::getInstance()->getWallet();
    if (walletModel)
    {
        walletModel->async->emergencyReset();
    }
}

bool WalletSettings::getRunLocalNode() const
{
    return m_data.value(LocalNodeRun, false).toBool();
}

void WalletSettings::setRunLocalNode(bool value)
{
    m_data.setValue(LocalNodeRun, value);
    emit localNodeRunChanged();
}

short WalletSettings::getLocalNodePort() const
{
    return m_data.value(LocalNodePort, 10000).toInt();
}

void WalletSettings::setLocalNodePort(short port)
{
    m_data.setValue(LocalNodePort, port);
    emit localNodePortChanged();
}

int WalletSettings::getLocalNodeMiningThreads() const
{
    return m_data.value(LocalNodeMiningThreads, 1).toInt();
}

void WalletSettings::setLocalNodeMiningThreads(int n)
{
    m_data.setValue(LocalNodeMiningThreads, n);
    emit localNodeMiningThreadsChanged();
}

int WalletSettings::getLocalNodeVerificationThreads() const
{
    return m_data.value(LocalNodeVerificationThreads, 1).toInt();
}

void WalletSettings::setLocalNodeVerificationThreads(int n)
{
    m_data.setValue(LocalNodeVerificationThreads, n);
    emit localNodeVerificationThreadsChanged();
}

string WalletSettings::getLocalNodeStorage() const
{
    return m_appDataDir.filePath("node.db").toStdString();
}

string WalletSettings::getTempDir() const
{
    return m_appDataDir.filePath("./temp").toStdString();
}

