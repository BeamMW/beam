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
#include <QApplication>
#include <QClipboard>
#include "model/app_model.h"
#include <thread>
#include "wallet/secstring.h"

#include <algorithm>

#ifdef BEAM_USE_GPU
#include "utility/gpu/gpu_tools.h"
#endif

using namespace beam;
using namespace ECC;
using namespace std;


DeviceItem::DeviceItem(const QString& name, int32_t index, bool enabled)
    : m_name(name)
    , m_index(index)
    , m_enabled(enabled)
{

}

DeviceItem::~DeviceItem()
{

}

QString DeviceItem::getName() const
{
    return m_name;
}

bool DeviceItem::getEnabled() const
{
    return m_enabled;
}

void DeviceItem::setEnabled(bool value)
{
    if (m_enabled != value)
    {
        m_enabled = value;
        emit enableChanged();
    }
}

int32_t DeviceItem::getIndex() const
{
    return m_index;
}


SettingsViewModel::SettingsViewModel()
    : m_settings{AppModel::getInstance()->getSettings()}
{
    undoChanges();
    connect(&AppModel::getInstance()->getNode(), SIGNAL(startedNode()), SLOT(onNodeStarted()));
    connect(&AppModel::getInstance()->getNode(), SIGNAL(stoppedNode()), SLOT(onNodeStopped()));
}

void SettingsViewModel::onNodeStarted()
{
    emit localNodeRunningChanged();
}

void SettingsViewModel::onNodeStopped()
{
    emit localNodeRunningChanged();
}

bool SettingsViewModel::isLocalNodeRunning() const
{
    return AppModel::getInstance()->getNode().isNodeRunning();
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

QString SettingsViewModel::getVersion() const
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

void SettingsViewModel::copyToClipboard(const QString& text)
{
    QApplication::clipboard()->setText(text);
}

bool SettingsViewModel::showUseGpu() const
{
#ifdef BEAM_USE_GPU
    return true;
#else
    return false;
#endif
}

QQmlListProperty<DeviceItem> SettingsViewModel::getSupportedDevices() 
{
    return QQmlListProperty<DeviceItem>(this, m_supportedDevices);
}

bool SettingsViewModel::hasSupportedGpu()
{
#ifdef BEAM_USE_GPU
    if (!m_hasSupportedGpu.is_initialized())
    {
        m_hasSupportedGpu = HasSupportedCard();
    }
    if (*m_hasSupportedGpu == false)
    {
        setUseGpu(false);
        return false;
    }

    if (m_supportedDevices.empty())
    {
        auto selectedDevices = m_settings.getMiningDevices();
        auto cards = GetSupportedCards();
        for (const auto& card : cards)
        {
            bool enabled = find(selectedDevices.begin(), selectedDevices.end(), card.index) != selectedDevices.end();
            m_supportedDevices.push_back(new DeviceItem(QString::fromStdString(card.name), (int32_t)card.index, enabled));
        }
    }

    return true;
#else
    return false;
#endif
}

void SettingsViewModel::refreshWallet()
{
    AppModel::getInstance()->getWallet()->getAsync()->refresh();
}

#ifdef BEAM_USE_GPU

vector<int32_t> SettingsViewModel::getSelectedDevice() const
{
    vector<int32_t> v;
    for (const auto& d : m_supportedDevices)
    {
        DeviceItem* device = (DeviceItem*)d;
        if (device->getEnabled())
        {
            v.push_back(device->getIndex());
        }
    }
    return v;
}

#endif


bool SettingsViewModel::isChanged() const
{
    return m_nodeAddress != m_settings.getNodeAddress()
        || m_localNodeRun != m_settings.getRunLocalNode()
        || m_localNodePort != m_settings.getLocalNodePort()
        || m_localNodeMiningThreads != m_settings.getLocalNodeMiningThreads()
        || m_localNodePeers != m_settings.getLocalNodePeers()
#ifdef BEAM_USE_GPU
        || m_lockTimeout != m_settings.getLockTimeout()
        || m_useGpu != m_settings.getUseGpu()
        || (!m_supportedDevices.empty() && m_settings.getMiningDevices() != getSelectedDevice());
#else
        || m_lockTimeout != m_settings.getLockTimeout();
#endif
}


void SettingsViewModel::applyChanges()
{
    m_settings.setNodeAddress(m_nodeAddress);
    m_settings.setRunLocalNode(m_localNodeRun);
    m_settings.setLocalNodePort(m_localNodePort);
    m_settings.setLocalNodeMiningThreads(m_localNodeMiningThreads);
    m_settings.setLocalNodePeers(m_localNodePeers);
    m_settings.setLockTimeout(m_lockTimeout);
#ifdef BEAM_USE_GPU
    m_settings.setUseGpu(m_useGpu);
     
    m_settings.setMiningDevices(getSelectedDevice());
#endif
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

QString SettingsViewModel::getWalletLocation() const
{
    return QString::fromStdString(m_settings.getAppDataPath());
}

void SettingsViewModel::setUseGpu(bool value)
{
#ifdef BEAM_USE_GPU
    m_useGpu = value;
    emit localNodeUseGpuChanged();
    emit propertiesChanged();
#endif
}

bool SettingsViewModel::getUseGpu() const
{
#ifdef BEAM_USE_GPU
    return m_useGpu;
#else
    return false;
#endif
}

void SettingsViewModel::undoChanges()
{
    setNodeAddress(m_settings.getNodeAddress());
    setLocalNodeRun(m_settings.getRunLocalNode());
    setLocalNodePort(m_settings.getLocalNodePort());
    setLocalNodeMiningThreads(m_settings.getLocalNodeMiningThreads());
    setLockTimeout(m_settings.getLockTimeout());
    setLocalNodePeers(m_settings.getLocalNodePeers());
#ifdef BEAM_USE_GPU
    setUseGpu(m_settings.getUseGpu());
#endif
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
