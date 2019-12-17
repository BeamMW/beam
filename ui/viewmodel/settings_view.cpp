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
#include "model/swap_coin_client_model.h"
#include <thread>
#include "wallet/secstring.h"
#include "qml_globals.h"
#include <algorithm>
#include "wallet/litecoin/settings.h"
#include "wallet/qtum/settings.h"
#include <boost/algorithm/string/trim.hpp>
#include "utility/string_helpers.h"
#include "mnemonic/mnemonic.h"
#include "wallet/bitcoin/common.h"
#include "wallet/qtum/common.h"
#include "wallet/litecoin/common.h"

using namespace beam;
using namespace ECC;
using namespace std;

namespace
{
    QString AddressToQstring(const io::Address& address) 
    {
        if (!address.empty())
        {
            return str2qstr(address.str());
        }
        return {};
    }

    QString formatAddress(const QString& address, uint16_t port)
    {
        return QString("%1:%2").arg(address).arg(port);
    }

    struct UnpackedAddress 
    {
        QString address;
        uint16_t port = 0;
    };

    UnpackedAddress parseAddress(const QString& address)
    {
        UnpackedAddress res;
        auto separator = address.indexOf(':');
        if (separator > -1)
        {
            res.address = address.left(separator);
            auto portStr = address.mid(separator + 1);
            bool ok = false;
            uint16_t port = portStr.toInt(&ok);
            if (ok)
            {
                res.port = port;
            }
        }
        else
        {
            res.address = address;
        }
        return res;
    }

    const char ELECTRUM_PHRASES_SEPARATOR = ' ';
}


ElectrumPhraseItem::ElectrumPhraseItem(int index, const QString& phrase)
    : m_index(index)
    , m_phrase(phrase)
    , m_userInput(phrase)
{
}

bool ElectrumPhraseItem::isModified() const
{
    return m_userInput != m_phrase;
}

const QString& ElectrumPhraseItem::getValue() const
{
    return m_userInput;
}

void ElectrumPhraseItem::setValue(const QString& value)
{
    if (m_userInput != value)
    {
        m_userInput = value;
        emit valueChanged();
        emit isModifiedChanged();
        emit isAllowedChanged();
    }
}

const QString& ElectrumPhraseItem::getPhrase() const
{
    return m_phrase;
}

int ElectrumPhraseItem::getIndex() const
{
    return m_index;
}

bool ElectrumPhraseItem::isAllowed() const
{
    return bitcoin::isAllowedWord(m_userInput.toStdString());
}

void ElectrumPhraseItem::applyChanges()
{
    m_phrase = m_userInput;
}

void ElectrumPhraseItem::revertChanges()
{
    setValue(m_phrase);
}


SwapCoinSettingsItem::SwapCoinSettingsItem(SwapCoinClientModel& coinClient, wallet::AtomicSwapCoin swapCoin)
    : m_swapCoin(swapCoin)
    , m_coinClient(coinClient)
{
    connect(&m_coinClient, SIGNAL(statusChanged()), this, SLOT(onStatusChanged()));
    connect(&m_coinClient, SIGNAL(connectionErrorChanged()), this, SIGNAL(connectionErrorMsgChanged()));
    LoadSettings();
}

SwapCoinSettingsItem::~SwapCoinSettingsItem()
{
    qDeleteAll(m_seedPhraseItems);
}

QString SwapCoinSettingsItem::getFeeRateLabel() const
{
    switch (m_swapCoin)
    {
        case beam::wallet::AtomicSwapCoin::Bitcoin:
            return QMLGlobals::btcFeeRateLabel();;
        case beam::wallet::AtomicSwapCoin::Litecoin:
            return QMLGlobals::ltcFeeRateLabel();
        case beam::wallet::AtomicSwapCoin::Qtum:
            return QMLGlobals::qtumFeeRateLabel();
        default:
        {
            assert(false && "unexpected swap coin!");
            return QString();
        }
    }
}

QString SwapCoinSettingsItem::getTitle() const
{
    switch (m_settings->GetCurrentConnectionType())
    {
        case beam::bitcoin::ISettings::ConnectionType::None:
            return getGeneralTitle();
        case beam::bitcoin::ISettings::ConnectionType::Core:
            return getConnectedNodeTitle();
        case beam::bitcoin::ISettings::ConnectionType::Electrum:
            return getConnectedElectrumTitle();
        default:
        {
            assert(false && "unexpected connection type");
            return getGeneralTitle();
        }
    }
}

QString SwapCoinSettingsItem::getShowSeedDialogTitle() const
{
    switch (m_swapCoin)
    {
        case beam::wallet::AtomicSwapCoin::Bitcoin:
            //% "Bitcoin seed phrase"
            return qtTrId("bitcoin-show-seed-title");
        case beam::wallet::AtomicSwapCoin::Litecoin:
            //% "Litecoin seed phrase"
            return qtTrId("litecoin-show-seed-title");
        case beam::wallet::AtomicSwapCoin::Qtum:
            //% "Qtum seed phrase"
            return qtTrId("qtum-show-seed-title");
        default:
        {
            assert(false && "unexpected swap coin!");
            return QString();
        }
    }
}

QString SwapCoinSettingsItem::getShowAddressesDialogTitle() const
{
    switch (m_swapCoin)
    {
        case beam::wallet::AtomicSwapCoin::Bitcoin:
            //% "Bitcoin wallet addresses"
            return qtTrId("bitcoin-show-addresses-title");
        case beam::wallet::AtomicSwapCoin::Litecoin:
            //% "Litecoin wallet addresses"
            return qtTrId("litecoin-show-addresses-title");
        case beam::wallet::AtomicSwapCoin::Qtum:
            //% "Qtum wallet addresses"
            return qtTrId("qtum-show-addresses-title");
        default:
        {
            assert(false && "unexpected swap coin!");
            return QString();
        }
    }
}

QString SwapCoinSettingsItem::getGeneralTitle() const
{
    switch (m_swapCoin)
    {
        case wallet::AtomicSwapCoin::Bitcoin:
            //% "Bitcoin"
            return qtTrId("general-bitcoin");
        case wallet::AtomicSwapCoin::Litecoin:
            //% "Litecoin"
            return qtTrId("general-litecoin");
        case wallet::AtomicSwapCoin::Qtum:
            //% "QTUM"
            return qtTrId("general-qtum");
        default:
        {
            assert(false && "unexpected swap coin!");
            return QString();
        }
    }
}

QString SwapCoinSettingsItem::getConnectedNodeTitle() const
{
    // TODO: check, is real need translations?
    switch (m_swapCoin)
    {
        case wallet::AtomicSwapCoin::Bitcoin:
            //% "Bitcoin node"
            return qtTrId("settings-swap-bitcoin-node");
        case wallet::AtomicSwapCoin::Litecoin:
            //% "Litecoin node"
            return qtTrId("settings-swap-litecoin-node");
        case wallet::AtomicSwapCoin::Qtum:
            //% "Qtum node"
            return qtTrId("settings-swap-qtum-node");
        default:
        {
            assert(false && "unexpected swap coin!");
            return QString();
        }
    }
}

QString SwapCoinSettingsItem::getConnectedElectrumTitle() const
{
    // TODO: check, is real need translations?
    switch (m_swapCoin)
    {
        case wallet::AtomicSwapCoin::Bitcoin:
            //% "Bitcoin electrum"
            return qtTrId("settings-swap-bitcoin-electrum");
        case wallet::AtomicSwapCoin::Litecoin:
            //% "Litecoin electrum"
            return qtTrId("settings-swap-litecoin-electrum");
        case wallet::AtomicSwapCoin::Qtum:
            //% "Qtum electrum"
            return qtTrId("settings-swap-qtum-electrum");
        default:
        {
            assert(false && "unexpected swap coin!");
            return QString();
        }
    }
}

int SwapCoinSettingsItem::getFeeRate() const
{
    return m_feeRate;
}

void SwapCoinSettingsItem::setFeeRate(int value)
{
    if (value != m_feeRate)
    {
        m_feeRate = value;
        emit feeRateChanged();
    }
}

QString SwapCoinSettingsItem::getNodeUser() const
{
    return m_nodeUser;
}

void SwapCoinSettingsItem::setNodeUser(const QString& value)
{
    if (value != m_nodeUser)
    {
        m_nodeUser = value;
        emit nodeUserChanged();
    }
}

QString SwapCoinSettingsItem::getNodePass() const
{
    return m_nodePass;
}

void SwapCoinSettingsItem::setNodePass(const QString& value)
{
    if (value != m_nodePass)
    {
        m_nodePass = value;
        emit nodePassChanged();
    }
}

QString SwapCoinSettingsItem::getNodeAddress() const
{
    return m_nodeAddress;
}

void SwapCoinSettingsItem::setNodeAddress(const QString& value)
{
    const auto val = value == "0.0.0.0" ? "" : value;
    if (val != m_nodeAddress)
    {
        m_nodeAddress = val;
        emit nodeAddressChanged();
    }
}

uint16_t SwapCoinSettingsItem::getNodePort() const
{
    return m_nodePort;
}

void SwapCoinSettingsItem::setNodePort(const uint16_t& value)
{
    if (value != m_nodePort)
    {
        m_nodePort = value;
        emit nodePortChanged();
    }
}

QList<QObject*> SwapCoinSettingsItem::getElectrumSeedPhrases()
{
    return m_seedPhraseItems;
}

QChar SwapCoinSettingsItem::getPhrasesSeparatorElectrum() const
{
    return QChar(ELECTRUM_PHRASES_SEPARATOR);
}

bool SwapCoinSettingsItem::getIsCurrentSeedValid() const
{
    return m_isCurrentSeedValid;
}

bool SwapCoinSettingsItem::getIsCurrentSeedSegwit() const
{
    return m_isCurrentSeedSegwit;
}

QString SwapCoinSettingsItem::getNodeAddressElectrum() const
{
    return m_nodeAddressElectrum;
}

void SwapCoinSettingsItem::setNodeAddressElectrum(const QString& value)
{
    if (value != m_nodeAddressElectrum)
    {
        m_nodeAddressElectrum = value;
        emit nodeAddressElectrumChanged();
    }
}

uint16_t SwapCoinSettingsItem::getNodePortElectrum() const
{
    return m_nodePortElectrum;
}

void SwapCoinSettingsItem::setNodePortElectrum(const uint16_t& value)
{
    if (value != m_nodePortElectrum)
    {
        m_nodePortElectrum = value;
        emit nodePortElectrumChanged();
    }
}

bool SwapCoinSettingsItem::getSelectServerAutomatically() const
{
    return m_selectServerAutomatically;
}

void SwapCoinSettingsItem::setSelectServerAutomatically(bool value)
{
    if (value != m_selectServerAutomatically)
    {
        m_selectServerAutomatically = value;
        emit selectServerAutomaticallyChanged();

        if (!m_selectServerAutomatically)
        {
            setNodeAddressElectrum("");
        }
        else
        {
            auto settings = m_coinClient.GetSettings();

            if (auto options = settings.GetElectrumConnectionOptions(); options.IsInitialized())
            {
                applyNodeAddressElectrum(str2qstr(options.m_address));
            }
        }
    }
}

QStringList SwapCoinSettingsItem::getAddressesElectrum() const
{
    auto electrumSettings = m_settings->GetElectrumConnectionOptions();

    if (electrumSettings.IsInitialized())
    {
        auto addresses = bitcoin::generateReceivingAddresses(electrumSettings.m_secretWords, 
            electrumSettings.m_receivingAddressAmount, m_settings->GetAddressVersion());

        QStringList result;
        result.reserve(static_cast<int>(addresses.size()));

        for (const auto& address : addresses)
        {
            result.push_back(QString::fromStdString(address));
        }
        return result;
    }
    return {};
}

void SwapCoinSettingsItem::onStatusChanged()
{
    emit connectionStatusChanged();

    if (m_selectServerAutomatically)
    {
        auto settings = m_coinClient.GetSettings();

        if (auto options = settings.GetElectrumConnectionOptions(); options.IsInitialized())
        {
            applyNodeAddressElectrum(str2qstr(options.m_address));
        }
    }
}

bool SwapCoinSettingsItem::getCanEdit() const
{
    return m_coinClient.canModifySettings();
}

bool SwapCoinSettingsItem::getIsConnected() const
{
    return m_connectionType != beam::bitcoin::ISettings::ConnectionType::None;
}

bool SwapCoinSettingsItem::getIsNodeConnection() const
{
    return m_connectionType == beam::bitcoin::ISettings::ConnectionType::Core;
}

bool SwapCoinSettingsItem::getIsElectrumConnection() const
{
    return m_connectionType == beam::bitcoin::ISettings::ConnectionType::Electrum;
}

QString SwapCoinSettingsItem::getConnectionStatus() const
{
    using beam::bitcoin::Client;

    switch (m_coinClient.getStatus())
    {
        case Client::Status::Uninitialized:
            return "uninitialized";
            
        case Client::Status::Connecting:
            return "disconnected";

        case Client::Status::Connected:
            return "connected";

        case Client::Status::Failed:
        case Client::Status::Unknown:
        default:
            return "error";
    }
}

QString SwapCoinSettingsItem::getConnectionErrorMsg() const
{
    using beam::bitcoin::IBridge;

    switch (m_coinClient.getConnectionError())
    {
        case IBridge::ErrorType::InvalidCredentials:
            //% "Invalid credentials"
            return qtTrId("swap-invalid-credentials-error");

        case IBridge::ErrorType::IOError:
            //% "Cannot connect to node"
            return qtTrId("swap-connection-error");

        case IBridge::ErrorType::InvalidGenesisBlock:
            //% "Invalid genesis block"
            return qtTrId("swap-invalid-genesis-block-error");

        default:
            return QString();
    }
}

void SwapCoinSettingsItem::applyNodeSettings()
{
    bitcoin::BitcoinCoreSettings connectionSettings = m_coinClient.GetSettings().GetConnectionOptions();
    connectionSettings.m_pass = m_nodePass.toStdString();
    connectionSettings.m_userName = m_nodeUser.toStdString();

    if (!m_nodeAddress.isEmpty())
    {
        const std::string address = m_nodeAddress.toStdString();
        connectionSettings.m_address.resolve(address.c_str());
        connectionSettings.m_address.port(m_nodePort);
    }

    m_settings->SetConnectionOptions(connectionSettings);
    m_settings->SetFeeRate(m_feeRate);

    m_coinClient.SetSettings(*m_settings);
}

void SwapCoinSettingsItem::applyElectrumSettings()
{
    bitcoin::ElectrumSettings electrumSettings = m_coinClient.GetSettings().GetElectrumConnectionOptions();
    
    if (!m_selectServerAutomatically && !m_nodeAddressElectrum.isEmpty())
    {
        electrumSettings.m_address = formatAddress(m_nodeAddressElectrum, m_nodePortElectrum).toStdString();
    }

    electrumSettings.m_automaticChooseAddress = m_selectServerAutomatically;
    electrumSettings.m_secretWords = GetSeedPhraseFromSeedItems();
    
    m_settings->SetElectrumConnectionOptions(electrumSettings);
    m_settings->SetFeeRate(m_feeRate);

    m_coinClient.SetSettings(*m_settings);
}

void SwapCoinSettingsItem::resetNodeSettings()
{
    SetDefaultNodeSettings();
    applyNodeSettings();
}

void SwapCoinSettingsItem::resetElectrumSettings()
{
    SetDefaultElectrumSettings();
    applyElectrumSettings();
}

void SwapCoinSettingsItem::newElectrumSeed()
{
    auto secretWords = bitcoin::createElectrumMnemonic(getEntropy());    
    SetSeedElectrum(secretWords);
}

void SwapCoinSettingsItem::restoreSeedElectrum()
{
    SetSeedElectrum(m_settings->GetElectrumConnectionOptions().m_secretWords);
}

void SwapCoinSettingsItem::disconnect()
{
    auto connectionType = bitcoin::ISettings::ConnectionType::None;

    m_settings->ChangeConnectionType(connectionType);
    m_coinClient.SetSettings(*m_settings);
    setConnectionType(connectionType);
}

void SwapCoinSettingsItem::connectToNode()
{
    auto connectionType = bitcoin::ISettings::ConnectionType::Core;

    m_settings->ChangeConnectionType(connectionType);
    m_coinClient.SetSettings(*m_settings);
    setConnectionType(connectionType);
}

void SwapCoinSettingsItem::connectToElectrum()
{
    auto connectionType = bitcoin::ISettings::ConnectionType::Electrum;

    m_settings->ChangeConnectionType(connectionType);
    m_coinClient.SetSettings(*m_settings);
    setConnectionType(connectionType);
}

void SwapCoinSettingsItem::copySeedElectrum()
{
    auto seedElectrum = GetSeedPhraseFromSeedItems();
    auto seedString = vec2str(seedElectrum, ELECTRUM_PHRASES_SEPARATOR);
    QMLGlobals::copyToClipboard(QString::fromStdString(seedString));
}

void SwapCoinSettingsItem::validateCurrentElectrumSeedPhrase()
{
    std::vector<std::string> seedElectrum;
    seedElectrum.reserve(WORD_COUNT);

    // extract seed phrase from user input
    for (const auto phraseItem : m_seedPhraseItems)
    {
        auto word = static_cast<ElectrumPhraseItem*>(phraseItem)->getValue().toStdString();
        seedElectrum.push_back(word);
    }

    setIsCurrentSeedValid(bitcoin::validateElectrumMnemonic(seedElectrum));
    setIsCurrentSeedSegwit(bitcoin::validateElectrumMnemonic(seedElectrum, true));
}

void SwapCoinSettingsItem::LoadSettings()
{
    SetDefaultElectrumSettings();
    SetDefaultNodeSettings();

    m_settings = m_coinClient.GetSettings();

    setFeeRate(m_settings->GetFeeRate());
    setConnectionType(m_settings->GetCurrentConnectionType());

    if (auto options = m_settings->GetConnectionOptions(); options.IsInitialized())
    {
        setNodeUser(str2qstr(options.m_userName));
        setNodePass(str2qstr(options.m_pass));
        applyNodeAddress(AddressToQstring(options.m_address));
    }

    if (auto options = m_settings->GetElectrumConnectionOptions(); options.IsInitialized())
    {
        SetSeedElectrum(options.m_secretWords);
        setSelectServerAutomatically(options.m_automaticChooseAddress);
        applyNodeAddressElectrum(str2qstr(options.m_address));
    }
}

void SwapCoinSettingsItem::SetSeedElectrum(const std::vector<std::string>& seedElectrum)
{
    if (!m_seedPhraseItems.empty())
    {
        qDeleteAll(m_seedPhraseItems);
        m_seedPhraseItems.clear();
    }

    m_seedPhraseItems.reserve(static_cast<int>(WORD_COUNT));

    if (seedElectrum.empty())
    {
        for (int index = 0; index < static_cast<int>(WORD_COUNT); ++index)
        {
            m_seedPhraseItems.push_back(new ElectrumPhraseItem(index, QString()));
        }
    }
    else
    {
        assert(seedElectrum.size() == WORD_COUNT);
        int index = 0;
        for (auto& word : seedElectrum)
        {
            m_seedPhraseItems.push_back(new ElectrumPhraseItem(index++, QString::fromStdString(word)));
        }
    }

    setIsCurrentSeedValid(bitcoin::validateElectrumMnemonic(seedElectrum));
    setIsCurrentSeedSegwit(bitcoin::validateElectrumMnemonic(seedElectrum, true));
    emit electrumSeedPhrasesChanged();
}

void SwapCoinSettingsItem::SetDefaultNodeSettings()
{
    setNodePort(0);
    setNodeAddress("");
    setNodePass("");
    setNodeUser("");
}

void SwapCoinSettingsItem::SetDefaultElectrumSettings()
{
    setNodePortElectrum(0);
    setNodeAddressElectrum("");
    setSelectServerAutomatically(true);
    SetSeedElectrum({});
}

void SwapCoinSettingsItem::setConnectionType(beam::bitcoin::ISettings::ConnectionType type)
{
    if (type != m_connectionType)
    {
        m_connectionType = type;
        emit connectionTypeChanged();
    }
}

void SwapCoinSettingsItem::setIsCurrentSeedValid(bool value)
{
    if (m_isCurrentSeedValid != value)
    {
        m_isCurrentSeedValid = value;
        emit isCurrentSeedValidChanged();
    }
}

void SwapCoinSettingsItem::setIsCurrentSeedSegwit(bool value)
{
    if (m_isCurrentSeedSegwit != value)
    {
        m_isCurrentSeedSegwit = value;
        emit isCurrentSeedSegwitChanged();
    }
}

std::vector<std::string> SwapCoinSettingsItem::GetSeedPhraseFromSeedItems() const
{
    assert(static_cast<size_t>(m_seedPhraseItems.size()) == WORD_COUNT);

    std::vector<std::string> seedElectrum;
    seedElectrum.reserve(WORD_COUNT);

    for (const auto phraseItem : m_seedPhraseItems)
    {
        auto item = static_cast<ElectrumPhraseItem*>(phraseItem);
        auto word = item->getPhrase().toStdString();
        seedElectrum.push_back(word);
    }

    return seedElectrum;
}

void SwapCoinSettingsItem::applyNodeAddress(const QString& address)
{
    auto unpackedAddress = parseAddress(address);
    setNodeAddress(unpackedAddress.address);
    if (unpackedAddress.port > 0)
    {
        setNodePort(unpackedAddress.port);
    }
}

void SwapCoinSettingsItem::applyNodeAddressElectrum(const QString& address)
{
    auto unpackedAddress = parseAddress(address);
    setNodeAddressElectrum(unpackedAddress.address);
    if (unpackedAddress.port > 0)
    {
        setNodePortElectrum(unpackedAddress.port);
    }
}


SettingsViewModel::SettingsViewModel()
    : m_settings{AppModel::getInstance().getSettings()}
    , m_remoteNodePort(0)
    , m_isValidNodeAddress{true}
    , m_isNeedToCheckAddress(false)
    , m_isNeedToApplyChanges(false)
    , m_supportedLanguages(WalletSettings::getSupportedLanguages())
{
    undoChanges();
    connect(&AppModel::getInstance().getNode(), SIGNAL(startedNode()), SLOT(onNodeStarted()));
    connect(&AppModel::getInstance().getNode(), SIGNAL(stoppedNode()), SLOT(onNodeStopped()));
    connect(AppModel::getInstance().getWallet().get(), SIGNAL(addressChecked(const QString&, bool)), SLOT(onAddressChecked(const QString&, bool)));

    m_timerId = startTimer(CHECK_INTERVAL);
}

SettingsViewModel::~SettingsViewModel()
{
    qDeleteAll(m_swapSettings);
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

uint SettingsViewModel::getRemoteNodePort() const
{
    return m_remoteNodePort;
}

void SettingsViewModel::setRemoteNodePort(uint value)
{
    if (value != m_remoteNodePort)
    {
        m_remoteNodePort = value;
        emit remoteNodePortChanged();
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

QString SettingsViewModel::getOwnerKey(const QString& password) const
{
    SecString secretPass = password.toStdString();
    const auto& ownerKey = 
        AppModel::getInstance().getWallet()->exportOwnerKey(secretPass);
    return QString::fromStdString(ownerKey);
}

bool SettingsViewModel::isChanged() const
{
    return formatAddress(m_nodeAddress, m_remoteNodePort) != m_settings.getNodeAddress()
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

    m_settings.setNodeAddress(formatAddress(m_nodeAddress, m_remoteNodePort));
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
    auto remoteNodeAddress = m_settings.getNodeAddress();
    auto unpackedAddress = parseAddress(m_settings.getNodeAddress());
    setNodeAddress(unpackedAddress.address);
    if (unpackedAddress.port > 0)
    {
        setRemoteNodePort(unpackedAddress.port);
    }

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

const QList<QObject*>& SettingsViewModel::getSwapCoinSettings()
{
    if (m_swapSettings.empty())
    {
        m_swapSettings.push_back(new SwapCoinSettingsItem(*AppModel::getInstance().getBitcoinClient(), beam::wallet::AtomicSwapCoin::Bitcoin));
        m_swapSettings.push_back(new SwapCoinSettingsItem(*AppModel::getInstance().getLitecoinClient(), beam::wallet::AtomicSwapCoin::Litecoin));
        m_swapSettings.push_back(new SwapCoinSettingsItem(*AppModel::getInstance().getQtumClient(), beam::wallet::AtomicSwapCoin::Qtum));
    }
    return m_swapSettings;
}
