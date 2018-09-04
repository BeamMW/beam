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

#include "settings_view.h"
#include "version.h"
#include <QtQuick>
#include "model/app_model.h"
#include <thread>
#include "wallet/secstring.h"

using namespace beam;
using namespace ECC;
using namespace std;

SettingsViewModel::SettingsViewModel()
    : m_settings{AppModel::getInstance()->getSettings()}
{
    undoChanges();
}

QString SettingsViewModel::getNodeAddress() const
{
    return m_nodeAddress;
}

void SettingsViewModel::setNodeAddress(const QString& value)
{
    if (value != m_nodeAddress)
    {
        m_nodeAddress = value;
        emit nodeAddressChanged();
        emit propertiesChanged();
    }
}

QString SettingsViewModel::version() const
{
    return QString::fromStdString(PROJECT_VERSION);
}

bool SettingsViewModel::getLocalNodeRun() const
{
    return m_localNodeRun;
}

void SettingsViewModel::setLocalNodeRun(bool value)
{
    if (value != m_localNodeRun)
    {
        m_localNodeRun = value;
        emit localNodeRunChanged();
        emit propertiesChanged();
    }
}

uint SettingsViewModel::getLocalNodePort() const
{
    return m_localNodePort;
}

void SettingsViewModel::setLocalNodePort(uint value)
{
    if (value != m_localNodePort)
    {
        m_localNodePort = value;
        emit localNodePortChanged();
        emit propertiesChanged();
    }
}

uint SettingsViewModel::getLocalNodeMiningThreads() const
{
    return m_localNodeMiningThreads;
}

void SettingsViewModel::setLocalNodeMiningThreads(uint value)
{
    if (value != m_localNodeMiningThreads)
    {
        m_localNodeMiningThreads = value;
        emit localNodeMiningThreadsChanged();
        emit propertiesChanged();
    }
}

uint SettingsViewModel::getLocalNodeVerificationThreads() const
{
    return m_localNodeVerificationThreads;
}

void SettingsViewModel::setLocalNodeVerificationThreads(uint value)
{
    if (value != m_localNodeVerificationThreads)
    {
        m_localNodeVerificationThreads = value;
        emit localNodeVerificationThreadsChanged();
        emit propertiesChanged();
    }
}

int SettingsViewModel::getLockTimeout() const
{
    return m_lockTimeout;
}

void SettingsViewModel::setLockTimeout(int value)
{
    if (value != m_lockTimeout)
    {
        m_lockTimeout = value;
        emit lockTimeoutChanged();
        emit propertiesChanged();
    }
}

uint SettingsViewModel::coreAmount() const
{
    return std::thread::hardware_concurrency();
}

void SettingsViewModel::addLocalNodePeer(const QString& localNodePeer)
{
    m_localNodePeers.push_back(localNodePeer);
    emit localNodePeersChanged();
    emit propertiesChanged();
}

void SettingsViewModel::deleteLocalNodePeer(int index)
{
    m_localNodePeers.removeAt(index);
    emit localNodePeersChanged();
    emit propertiesChanged();
}

void SettingsViewModel::openUrl(const QString& url)
{
    QDesktopServices::openUrl(QUrl(url));
}

bool SettingsViewModel::isChanged() const
{
    return m_nodeAddress != m_settings.getNodeAddress()
        || m_localNodeRun != m_settings.getRunLocalNode()
        || m_localNodePort != m_settings.getLocalNodePort()
        || m_localNodeMiningThreads != m_settings.getLocalNodeMiningThreads()
        || m_localNodeVerificationThreads != m_settings.getLocalNodeVerificationThreads()
        || m_localNodePeers != m_settings.getLocalNodePeers()
        || m_lockTimeout != m_settings.getLockTimeout();
}

void SettingsViewModel::applyChanges()
{
    m_settings.setNodeAddress(m_nodeAddress);
    m_settings.setRunLocalNode(m_localNodeRun);
    m_settings.setLocalNodePort(m_localNodePort);
    m_settings.setLocalNodeMiningThreads(m_localNodeMiningThreads);
    m_settings.setLocalNodeVerificationThreads(m_localNodeVerificationThreads);
    m_settings.setLocalNodePeers(m_localNodePeers);
    m_settings.setLockTimeout(m_lockTimeout);
    m_settings.applyChanges();
    emit propertiesChanged();
}

QStringList SettingsViewModel::getLocalNodePeers() const
{
    return m_localNodePeers;
}

void SettingsViewModel::setLocalNodePeers(const QStringList& localNodePeers)
{
    m_localNodePeers = localNodePeers;
    emit localNodePeersChanged();
    emit propertiesChanged();
}

void SettingsViewModel::undoChanges()
{
    setNodeAddress(m_settings.getNodeAddress());
    setLocalNodeRun(m_settings.getRunLocalNode());
    setLocalNodePort(m_settings.getLocalNodePort());
    setLocalNodeMiningThreads(m_settings.getLocalNodeMiningThreads());
    setLocalNodeVerificationThreads(m_settings.getLocalNodeVerificationThreads());
    setLockTimeout(m_settings.getLockTimeout());
    setLocalNodePeers(m_settings.getLocalNodePeers());
}

void SettingsViewModel::emergencyReset()
{
    m_settings.emergencyReset();
}

void SettingsViewModel::reportProblem()
{
	m_settings.reportProblem();
}

bool SettingsViewModel::checkWalletPassword(const QString& oldPass) const
{
	SecString secretPass = oldPass.toStdString();
	return AppModel::getInstance()->checkWalletPassword(secretPass);
}

void SettingsViewModel::changeWalletPassword(const QString& pass)
{
    AppModel::getInstance()->changeWalletPassword(pass.toStdString());
}
