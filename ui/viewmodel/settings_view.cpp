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
#include "model/helpers.h"
#include <thread>
#include "wallet/secstring.h"
#include "qml_globals.h"
#include <algorithm>


using namespace beam;
using namespace ECC;
using namespace std;


SettingsViewModel::SettingsViewModel()
    : m_settings{AppModel::getInstance().getSettings()}
    , m_isValidNodeAddress{true}
    , m_isNeedToCheckAddress(false)
    , m_isNeedToApplyChanges(false)
    , m_supportedLanguages(WalletSettings::getSupportedLanguages())
{
    undoChanges();
    connect(&AppModel::getInstance().getNode(), SIGNAL(startedNode()), SLOT(onNodeStarted()));
    connect(&AppModel::getInstance().getNode(), SIGNAL(stoppedNode()), SLOT(onNodeStopped()));
    connect(AppModel::getInstance().getWallet().get(), SIGNAL(addressChecked(const QString&, bool)), SLOT(onAddressChecked(const QString&, bool)));

    LoadBitcoinSettings();

    // TODO:SWAP-SETTINGS load LTC settings
    // TODO:SWAP-SETTINGS load QTUM settings

    m_timerId = startTimer(CHECK_INTERVAL);
}

void SettingsViewModel::onNodeStarted()
{
    emit localNodeRunningChanged();
}

void SettingsViewModel::onNodeStopped()
{
    emit localNodeRunningChanged();
}

void SettingsViewModel::onAddressChecked(const QString& addr, bool isValid)
{
    if (m_nodeAddress == addr && m_isValidNodeAddress != isValid)
    {
        m_isValidNodeAddress = isValid;
        emit validNodeAddressChanged();

        if (m_isNeedToApplyChanges)
        {
            if (m_isValidNodeAddress)
                applyChanges();

            m_isNeedToApplyChanges = false;
        }
    }
}

QString SettingsViewModel::getBTCUser() const
{
    return m_bitcoinUser;
}

void SettingsViewModel::setBTCUser(const QString& value)
{
    if (value != m_bitcoinUser)
    {
        m_bitcoinUser = value;
        emit btcUserChanged();
        emit propertiesChanged();
    }
}

QString SettingsViewModel::getBTCPass() const
{
    return m_bitcoinPass;
}

void SettingsViewModel::setBTCPass(const QString& value)
{
    if (value != m_bitcoinPass)
    {
        m_bitcoinPass = value;
        emit btcPassChanged();
        emit propertiesChanged();
    }
}

QString SettingsViewModel::getBTCNodeAddress() const
{
    return m_bitcoinNodeAddress;
}

void SettingsViewModel::setBTCNodeAddress(const QString& value)
{
    const auto val = value == "0.0.0.0" ? "" : value;
    if (val != m_bitcoinNodeAddress)
    {
        m_bitcoinNodeAddress = val;
        emit btcNodeAddressChanged();
        emit propertiesChanged();
    }
}

int SettingsViewModel::getBTCFeeRate() const
{
    return m_bitcoinFeeRate;
}

void SettingsViewModel::setBTCFeeRate(int value)
{
    LOG_INFO() << "setBTCFeeRate " << value;
    if (value != m_bitcoinFeeRate)
    {
        m_bitcoinFeeRate = value;
        emit btcFeeRateChanged();
        emit propertiesChanged();
    }
}

/* work in progress, do not touch now
void SettingsViewModel::disableBtc()
{
    setBTCFeeRate(QMLGlobals::defFeeRateBTC());
    setBTCNodeAddress("0.0.0.0");
    setBTCPass("");
    setBTCUser("");
    //applyBtcSettings();
}
 */

void SettingsViewModel::applyBtcSettings()
{
    BitcoindSettings connectionSettings;
    connectionSettings.m_pass = m_bitcoinPass.toStdString();
    connectionSettings.m_userName = m_bitcoinUser.toStdString();

    if (!m_bitcoinNodeAddress.isEmpty())
    {
        const std::string address = m_bitcoinNodeAddress.toStdString();
        connectionSettings.m_address.resolve(address.c_str());
    }

    m_bitcoinSettings->SetConnectionOptions(connectionSettings);
    m_bitcoinSettings->SetFeeRate(m_bitcoinFeeRate);

    AppModel::getInstance().getBitcoinClient()->SetSettings(*m_bitcoinSettings);

    // TODO:SWAP-SETTINGS probably need to remove
    AppModel::getInstance().getBitcoinClient()->GetAsync()->GetBalance();
}

QString SettingsViewModel::getLTCUser() const
{
    return m_litecoinUser;
}

void SettingsViewModel::setLTCUser(const QString& value)
{
    if (value != m_litecoinUser)
    {
        m_litecoinUser = value;
        emit ltcUserChanged();
        emit propertiesChanged();
    }
}

QString SettingsViewModel::getLTCPass() const
{
    return m_litecoinPass;
}

void SettingsViewModel::setLTCPass(const QString& value)
{
    if (value != m_litecoinPass)
    {
        m_litecoinPass = value;
        emit ltcPassChanged();
        emit propertiesChanged();
    }
}

QString SettingsViewModel::getLTCNodeAddress() const
{
    return m_litecoinNodeAddress;
}

void SettingsViewModel::setLTCNodeAddress(const QString& value)
{
    if (value != m_litecoinNodeAddress)
    {
        m_litecoinNodeAddress = value;
        emit ltcNodeAddressChanged();
        emit propertiesChanged();
    }
}

int SettingsViewModel::getLTCFeeRate() const
{
    return m_litecoinFeeRate;
}

void SettingsViewModel::setLTCFeeRate(int value)
{
    if (value != m_litecoinFeeRate)
    {
        m_litecoinFeeRate = value;
        emit ltcFeeRateChanged();
        emit propertiesChanged();
    }
}

void SettingsViewModel::applyLtcSettings()
{
    // TODO:SWAP-SETTINGS save LTC settings. These can be empty, take a look at btc apply
}

QString SettingsViewModel::getQTUMUser() const
{
    return m_qtumUser;
}

void SettingsViewModel::setQTUMUser(const QString& value)
{
    if (value != m_qtumUser)
    {
        m_qtumUser = value;
        emit qtumUserChanged();
        emit propertiesChanged();
    }
}

QString SettingsViewModel::getQTUMPass() const
{
    return m_qtumPass;
}

void SettingsViewModel::setQTUMPass(const QString& value)
{
    if (value != m_qtumPass)
    {
        m_qtumPass = value;
        emit qtumPassChanged();
        emit propertiesChanged();
    }
}

QString SettingsViewModel::getQTUMNodeAddress() const
{
    return m_qtumNodeAddress;
}

void SettingsViewModel::setQTUMNodeAddress(const QString& value)
{
    if (value != m_qtumNodeAddress)
    {
        m_qtumNodeAddress = value;
        emit qtumNodeAddressChanged();
        emit propertiesChanged();
    }
}

int SettingsViewModel::getQTUMFeeRate() const
{
    return m_qtumFeeRate;
}

void SettingsViewModel::setQTUMFeeRate(int value)
{
    if (value != m_qtumFeeRate)
    {
        m_qtumFeeRate = value;
        emit qtumFeeRateChanged();
        emit propertiesChanged();
    }
}

void SettingsViewModel::applyQtumSettings()
{
    // TODO:SWAP-SETTINGS save QTUM settings. These can be empty, take a look at btc apply
}

bool SettingsViewModel::isLocalNodeRunning() const
{
    return AppModel::getInstance().getNode().isNodeRunning();
}

bool SettingsViewModel::isValidNodeAddress() const
{
    return m_isValidNodeAddress;
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

        if (!m_isNeedToCheckAddress)
        {
            m_isNeedToCheckAddress = true;
            m_timerId = startTimer(CHECK_INTERVAL);
        }

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

        if (!m_localNodeRun && !m_isNeedToCheckAddress)
        {
            m_isNeedToCheckAddress = true;
            m_timerId = startTimer(CHECK_INTERVAL);
        }

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

int SettingsViewModel::getLockTimeout() const
{
    return m_lockTimeout;
}

void SettingsViewModel::setLockTimeout(int value)
{
    if (value != m_lockTimeout)
    {
        m_lockTimeout = value;
        m_settings.setLockTimeout(m_lockTimeout);
        emit lockTimeoutChanged();
    }
}

bool SettingsViewModel::isPasswordReqiredToSpendMoney() const
{
    return m_isPasswordReqiredToSpendMoney;
}

void SettingsViewModel::setPasswordReqiredToSpendMoney(bool value)
{
    if (value != m_isPasswordReqiredToSpendMoney)
    {
        m_isPasswordReqiredToSpendMoney = value;
        m_settings.setPasswordReqiredToSpendMoney(m_isPasswordReqiredToSpendMoney);
        emit passwordReqiredToSpendMoneyChanged();
    }
}

bool SettingsViewModel::isAllowedBeamMWLinks() const
{
    return m_isAllowedBeamMWLinks;
}

void SettingsViewModel::allowBeamMWLinks(bool value)
{
    if (value != m_isAllowedBeamMWLinks)
    {
        m_isAllowedBeamMWLinks = value;
        m_settings.setAllowedBeamMWLinks(m_isAllowedBeamMWLinks);
        emit beamMWLinksAllowed();
    }
}

QStringList SettingsViewModel::getSupportedLanguages() const
{
    return m_supportedLanguages;
}

int SettingsViewModel::getCurrentLanguageIndex() const
{
    return m_currentLanguageIndex;
}

void SettingsViewModel::setCurrentLanguageIndex(int value)
{
    m_currentLanguageIndex = value;
    m_settings.setLocaleByLanguageName(
            m_supportedLanguages[m_currentLanguageIndex]);
    emit currentLanguageIndexChanged();
}

QString SettingsViewModel::getCurrentLanguage() const
{
    return m_supportedLanguages[m_currentLanguageIndex];
}

void SettingsViewModel::setCurrentLanguage(QString value)
{
    auto index = m_supportedLanguages.indexOf(value);
    if (index != -1 )
    {
        setCurrentLanguageIndex(index);
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

void SettingsViewModel::refreshWallet()
{
    AppModel::getInstance().getWallet()->getAsync()->refresh();
}

void SettingsViewModel::openFolder(const QString& path)
{
    WalletSettings::openFolder(path);
}

bool SettingsViewModel::checkWalletPassword(const QString& oldPass) const
{
    SecString secretPass = oldPass.toStdString();
    return AppModel::getInstance().checkWalletPassword(secretPass);
}

bool SettingsViewModel::isChanged() const
{
    return m_nodeAddress != m_settings.getNodeAddress()
        || m_localNodeRun != m_settings.getRunLocalNode()
        || m_localNodePort != m_settings.getLocalNodePort()
        || m_localNodePeers != m_settings.getLocalNodePeers();
}

void SettingsViewModel::applyChanges()
{
    if (!m_localNodeRun && m_isNeedToCheckAddress)
    {
        m_isNeedToApplyChanges = true;
        return;
    }

    m_settings.setNodeAddress(m_nodeAddress);
    m_settings.setRunLocalNode(m_localNodeRun);
    m_settings.setLocalNodePort(m_localNodePort);
    m_settings.setLocalNodePeers(m_localNodePeers);
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

void SettingsViewModel::undoChanges()
{
    setNodeAddress(m_settings.getNodeAddress());
    setLocalNodeRun(m_settings.getRunLocalNode());
    setLocalNodePort(m_settings.getLocalNodePort());
    setLockTimeout(m_settings.getLockTimeout());
    setLocalNodePeers(m_settings.getLocalNodePeers());
    setPasswordReqiredToSpendMoney(m_settings.isPasswordReqiredToSpendMoney());
    allowBeamMWLinks(m_settings.isAllowedBeamMWLinks());
    setCurrentLanguageIndex(m_supportedLanguages.indexOf(m_settings.getLanguageName()));
}

void SettingsViewModel::reportProblem()
{
    m_settings.reportProblem();
}

void SettingsViewModel::changeWalletPassword(const QString& pass)
{
    AppModel::getInstance().changeWalletPassword(pass.toStdString());
}

void SettingsViewModel::timerEvent(QTimerEvent *event)
{
    if (m_isNeedToCheckAddress && !m_localNodeRun)
    {
        m_isNeedToCheckAddress = false;

        AppModel::getInstance().getWallet()->getAsync()->checkAddress(m_nodeAddress.toStdString());

        killTimer(m_timerId);
    }
}

void SettingsViewModel::LoadBitcoinSettings()
{
    m_bitcoinSettings = AppModel::getInstance().getBitcoinClient()->GetSettings();
    setBTCUser(str2qstr(m_bitcoinSettings->GetConnectionOptions().m_userName));
    setBTCPass(str2qstr(m_bitcoinSettings->GetConnectionOptions().m_pass));
    setBTCNodeAddress(str2qstr(m_bitcoinSettings->GetConnectionOptions().m_address.str()));
    setBTCFeeRate(m_bitcoinSettings->GetFeeRate());
}
