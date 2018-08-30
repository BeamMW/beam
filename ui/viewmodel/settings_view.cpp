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

uint SettingsViewModel::coreAmount() const
{
    return std::thread::hardware_concurrency();
}

bool SettingsViewModel::isChanged() const
{
    return m_nodeAddress != m_settings.getNodeAddress()
        || m_localNodeRun != m_settings.getRunLocalNode()
        || m_localNodePort != m_settings.getLocalNodePort()
        || m_localNodeMiningThreads != m_settings.getLocalNodeMiningThreads()
        || m_localNodeVerificationThreads != m_settings.getLocalNodeVerificationThreads();
}

void SettingsViewModel::applyChanges()
{
    m_settings.setNodeAddress(m_nodeAddress);
    m_settings.setRunLocalNode(m_localNodeRun);
    m_settings.setLocalNodePort(m_localNodePort);
    m_settings.setLocalNodeMiningThreads(m_localNodeMiningThreads);
    m_settings.setLocalNodeVerificationThreads(m_localNodeVerificationThreads);
}

void SettingsViewModel::undoChanges()
{
    setNodeAddress(m_settings.getNodeAddress());
    setLocalNodeRun(m_settings.getRunLocalNode());
    setLocalNodePort(m_settings.getLocalNodePort());
    setLocalNodeMiningThreads(m_settings.getLocalNodeMiningThreads());
    setLocalNodeVerificationThreads(m_settings.getLocalNodeVerificationThreads());
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
