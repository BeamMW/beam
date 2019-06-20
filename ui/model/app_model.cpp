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

#include "app_model.h"
#include "utility/common.h"
#include "utility/logger.h"

#include <boost/filesystem.hpp>
#include <QApplication>
#include <QTranslator>

#if defined(BEAM_HW_WALLET)
#include "wallet/hw_wallet.h"
#endif

using namespace beam;
using namespace beam::wallet;
using namespace ECC;
using namespace std;

namespace
{
const char* kDefaultTranslationsPath = ":/translations";

void RemoveFile(boost::filesystem::path path)
{
    boost::system::error_code error;
    boost::filesystem::remove(path, error);
    if (error)
    {
        LOG_ERROR() << error.message();
    }
}
}

AppModel* AppModel::s_instance = nullptr;

AppModel* AppModel::getInstance()
{
    assert(s_instance != nullptr);
    return s_instance;
}

AppModel::AppModel(WalletSettings& settings, QQmlApplicationEngine& qmlEngine)
    : m_settings{settings}
    , m_qmlEngine{qmlEngine}
    , m_walletReactor(beam::io::Reactor::create())
    , m_translator(make_unique<QTranslator>())
{
    assert(s_instance == nullptr);
    s_instance = this;

    loadTranslation();    
    connect(&m_settings, SIGNAL(localeChanged()), SLOT(onLocaleChanged()));

    m_nodeModel.start();
}

AppModel::~AppModel()
{
    s_instance = nullptr;
}

bool AppModel::createWallet(const SecString& seed, const SecString& pass)
{
    auto dbFilePath = m_settings.getWalletStorage();
    if (WalletDB::isInitialized(dbFilePath))
    {
        // it seems that we are trying to restore or login to another wallet.
        // Rename existing db 
        boost::filesystem::path p = dbFilePath;
        boost::filesystem::path newName = dbFilePath + "_" + to_string(getTimestamp());
        boost::filesystem::rename(p, newName);
    }
    m_db = WalletDB::init(dbFilePath, pass, seed.hash(), m_walletReactor);
    if (!m_db)
        return false;

    // generate default address

    WalletAddress address = storage::createAddress(*m_db);
    address.m_label = "default";
    m_db->saveAddress(address);

    OnWalledOpened(pass);
    return true;
}

bool AppModel::openWallet(const beam::SecString& pass)
{
    m_db = WalletDB::open(m_settings.getWalletStorage(), pass, m_walletReactor);
    if (!m_db)
        return false;

    OnWalledOpened(pass);
    return true;
}

void AppModel::OnWalledOpened(const beam::SecString& pass)
{
    m_passwordHash = pass.hash();
    start();
}

void AppModel::resetWalletImpl()
{
    if(m_settings.getRunLocalNode()) {
        disconnect(&m_nodeModel, SIGNAL(startedNode()), this, SLOT(startedNode()));
        disconnect(&m_nodeModel, SIGNAL(failedToStartNode(beam::wallet::ErrorType)), this, SLOT(onFailedToStartNode(beam::wallet::ErrorType)));
        disconnect(&m_nodeModel, SIGNAL(failedToSyncNode(beam::wallet::ErrorType)), this, SLOT(onFailedToStartNode(beam::wallet::ErrorType)));
    }

    assert(m_db);
    m_db.reset();

    assert(m_wallet.use_count() == 1);
    assert(m_wallet);
    m_wallet.reset();

    try
    {
#ifdef WIN32
        boost::filesystem::path walletDBPath{ Utf8toUtf16(getSettings().getWalletStorage().c_str()) };
        boost::filesystem::path nodeDBPath{ Utf8toUtf16(getSettings().getLocalNodeStorage().c_str()) };
#else
        boost::filesystem::path walletDBPath{ getSettings().getWalletStorage() };
        boost::filesystem::path nodeDBPath{ getSettings().getLocalNodeStorage() };
#endif
        RemoveFile(walletDBPath);
        RemoveFile(nodeDBPath);
    }
    catch (std::exception &e)
    {
        LOG_ERROR() << e.what();
    }
}

void AppModel::loadTranslation()
{
    auto locale = m_settings.getLocale();
    if (m_translator->load(locale, kDefaultTranslationsPath))
    {
        qApp->installTranslator(m_translator.get());
    }
    else
    {
        LOG_WARNING() << "Can't load translation";
    }
}

void AppModel::applySettingsChanges()
{
    if (m_nodeModel.isNodeRunning())
    {
        m_nodeModel.stopNode();
    }

    if (m_settings.getRunLocalNode())
    {
        m_nodeModel.startNode();

        io::Address nodeAddr = io::Address::LOCALHOST;
        nodeAddr.port(m_settings.getLocalNodePort());

        m_wallet->getAsync()->setNodeAddress(nodeAddr.str());
    }
    else
    {
        auto nodeAddr = m_settings.getNodeAddress().toStdString();
        m_wallet->getAsync()->setNodeAddress(nodeAddr);
    }
}

void AppModel::startedNode()
{
    if (m_wallet && !m_wallet->isRunning())
    {
        disconnect(
            &m_nodeModel, SIGNAL(failedToSyncNode(beam::wallet::ErrorType)),
            this, SLOT(onFailedToStartNode(beam::wallet::ErrorType)));
        m_wallet->start();
    }
}

void AppModel::stoppedNode()
{
    resetWalletImpl();
    disconnect(&m_nodeModel, SIGNAL(stoppedNode()), this, SLOT(stoppedNode()));
}

void AppModel::onFailedToStartNode(beam::wallet::ErrorType errorCode)
{
    if (errorCode == beam::wallet::ErrorType::ConnectionAddrInUse && m_wallet)
    {
        emit m_wallet->walletError(errorCode);
        return;
    }

    if (errorCode == beam::wallet::ErrorType::TimeOutOfSync && m_wallet)
    {
        //% "Failed to start the integrated node: the timezone settings of your machine are out of sync. Please fix them and restart the wallet."
        getMessages().addMessage(qtTrId("appmodel-failed-time-not-synced"));
        return;
    }

    //% "Failed to start node. Please check your node configuration"
    getMessages().addMessage(qtTrId("appmodel-failed-start-node"));
}

void AppModel::onLocaleChanged()
{
    qApp->removeTranslator(m_translator.get());
    m_translator = make_unique<QTranslator>();
    loadTranslation();
    m_qmlEngine.retranslate();
}

void AppModel::start()
{
    // TODO(alex.starun): should be uncommented when HW detection will be done

//#if defined(BEAM_HW_WALLET)
//    {
//        HWWallet hw;
//        auto key = hw.getOwnerKeySync();
//        
//        LOG_INFO() << "Owner key" << key;
//
//        // TODO: password encryption will be removed
//        std::string pass = "1";
//        KeyString ks;
//        ks.SetPassword(Blob(pass.data(), static_cast<uint32_t>(pass.size())));
//
//        ks.m_sRes = key;
//
//        std::shared_ptr<ECC::HKdfPub> pKdf = std::make_shared<ECC::HKdfPub>();
//
//        if (ks.Import(*pKdf))
//        {
//            m_nodeModel.setOwnerKey(pKdf);
//        }
//        else
//        {
//            LOG_ERROR() << "veiw key import failed";            
//        }
//    }
//#else
    m_nodeModel.setKdf(m_db->get_MasterKdf());
//#endif

    std::string nodeAddrStr;

    if (m_settings.getRunLocalNode())
    {
        connect(&m_nodeModel, SIGNAL(startedNode()), SLOT(startedNode()));
        connect(&m_nodeModel, SIGNAL(failedToStartNode(beam::wallet::ErrorType)), SLOT(onFailedToStartNode(beam::wallet::ErrorType)));
        connect(&m_nodeModel, SIGNAL(failedToSyncNode(beam::wallet::ErrorType)), SLOT(onFailedToStartNode(beam::wallet::ErrorType)));

        m_nodeModel.startNode();

        io::Address nodeAddr = io::Address::LOCALHOST;
        nodeAddr.port(m_settings.getLocalNodePort());
        nodeAddrStr = nodeAddr.str();
    }
    else
        nodeAddrStr = m_settings.getNodeAddress().toStdString();

    m_wallet = std::make_shared<WalletModel>(m_db, nodeAddrStr, m_walletReactor);

    if (!m_settings.getRunLocalNode())
        m_wallet->start();
}

WalletModel::Ptr AppModel::getWallet() const
{
    return m_wallet;
}

WalletSettings& AppModel::getSettings()
{
    return m_settings;
}

MessageManager& AppModel::getMessages()
{
    return m_messages;
}

NodeModel& AppModel::getNode()
{
    return m_nodeModel;
}

void AppModel::resetWallet()
{
    if (m_nodeModel.isNodeRunning())
    {
        connect(&m_nodeModel, SIGNAL(stoppedNode()), SLOT(stoppedNode()));
        m_nodeModel.stopNode();
        return;
    }

    resetWalletImpl();
}

bool AppModel::checkWalletPassword(const beam::SecString& pass) const
{
    auto passwordHash = pass.hash();

    return passwordHash.V == m_passwordHash.V;
}

void AppModel::changeWalletPassword(const std::string& pass)
{
    beam::SecString t = pass;
    m_passwordHash.V = t.hash().V;

    m_wallet->getAsync()->changeWalletPassword(pass);
}
