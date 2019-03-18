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

using namespace beam;
using namespace ECC;
using namespace std;

AppModel* AppModel::s_instance = nullptr;

AppModel* AppModel::getInstance()
{
    assert(s_instance != nullptr);
    return s_instance;
}

AppModel::AppModel(WalletSettings& settings)
    : m_settings{settings}
    , m_walletReactor(beam::io::Reactor::create())
{
    assert(s_instance == nullptr);
    s_instance = this;

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

    WalletAddress address = wallet::createAddress(*m_db);
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
    assert(m_db);
    m_db.reset();

    assert(m_wallet.use_count() == 1);
    assert(m_wallet);
    m_wallet.reset();

    try
    {
#ifdef WIN32
        boost::filesystem::path appDataPath{ Utf8toUtf16(getSettings().getAppDataPath().c_str()) };
#else
        boost::filesystem::path appDataPath{ getSettings().getAppDataPath() };
#endif
        boost::filesystem::path logsFolderPath = appDataPath;
        logsFolderPath /= WalletSettings::LogsFolder;

        boost::filesystem::path settingsPath = appDataPath;
        settingsPath /= WalletSettings::SettingsFile;

        for (boost::filesystem::directory_iterator endDirIt, it{ appDataPath }; it != endDirIt; ++it)
        {
            // don't delete settings and logs files
            if ((it->path() == logsFolderPath) || (it->path() == settingsPath))
            {
                continue;
            }

            boost::system::error_code error;
            boost::filesystem::remove_all(it->path(), error);
            if (error)
            {
                LOG_ERROR() << error.message();
            }
        }
    }
    catch (std::exception &e)
    {
        LOG_ERROR() << e.what();
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
        m_wallet->start();
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

    getMessages().addMessage(tr("Failed to start node. Please check your node configuration"));
}

void AppModel::start()
{
    m_nodeModel.setKdf(m_db->get_MasterKdf());

    std::string nodeAddrStr;

    if (m_settings.getRunLocalNode())
    {
        connect(&m_nodeModel, SIGNAL(startedNode()), SLOT(startedNode()));
        connect(&m_nodeModel, SIGNAL(failedToStartNode(beam::wallet::ErrorType)), SLOT(onFailedToStartNode(beam::wallet::ErrorType)));

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

