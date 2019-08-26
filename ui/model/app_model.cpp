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
#include "utility/fsutils.h"
#include <boost/filesystem.hpp>
#include <QApplication>
#include <QTranslator>
#include "wallet/local_private_key_keeper.h"

using namespace beam;
using namespace beam::wallet;
using namespace ECC;
using namespace std;

AppModel* AppModel::s_instance = nullptr;

AppModel& AppModel::getInstance()
{
    assert(s_instance != nullptr);
    return *s_instance;
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
    const auto dbFilePath = m_settings.getWalletStorage();
    const auto wasInitialized = WalletDB::isInitialized(dbFilePath);
    m_db.reset();

    if(wasInitialized)
    {
        // it seems that we are trying to restore or login to another wallet.
        // Rename/backup existing db
        boost::filesystem::path p = dbFilePath;
        boost::filesystem::path newName = dbFilePath + "_" + to_string(getTimestamp());
        boost::filesystem::rename(p, newName);
    }

    m_db = WalletDB::init(dbFilePath, pass, seed.hash(), m_walletReactor);
    if (!m_db) return false;

    m_keyKeeper = std::make_shared<LocalPrivateKeyKeeper>(m_db);

    // generate default address
    WalletAddress address = storage::createAddress(*m_db, m_keyKeeper);
    address.m_label = "default";
    m_db->saveAddress(address);

    onWalledOpened(pass);
    return true;
}

bool AppModel::openWallet(const beam::SecString& pass)
{
    assert(m_db == nullptr);
    m_db = WalletDB::open(m_settings.getWalletStorage(), pass, m_walletReactor);
    if (!m_db) return false;

    m_keyKeeper = std::make_shared<LocalPrivateKeyKeeper>(m_db);

    onWalledOpened(pass);
    return true;
}

void AppModel::onWalledOpened(const beam::SecString& pass)
{
    m_passwordHash = pass.hash();
    start();
}

void AppModel::resetWallet()
{
    if (m_nodeModel.isNodeRunning())
    {
        m_nsc.disconnect();

        auto dconn = MakeConnectionPtr();
        *dconn = connect(&m_nodeModel, &NodeModel::destroyedNode, [this, dconn]() {
            QObject::disconnect(*dconn);
            resetWalletImpl();
        });

        m_nodeModel.stopNode();
        return;
    }

    resetWalletImpl();
}

void AppModel::resetWalletImpl()
{
    assert(m_wallet);
    assert(m_wallet.use_count() == 1);
    assert(m_db);

    m_wallet.reset();
    m_db.reset();

    fsutils::remove(getSettings().getWalletStorage());
    fsutils::remove(getSettings().getLocalNodeStorage());

    emit walletReseted();
}

void AppModel::applySettingsChanges()
{
    if (m_nodeModel.isNodeRunning())
    {
        m_nsc.disconnect();
        m_nodeModel.stopNode();
    }

    if (m_settings.getRunLocalNode())
    {
        startNode();

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

void AppModel::nodeSettingsChanged()
{
    applySettingsChanges();
    if (!m_settings.getRunLocalNode())
    {
        if (!m_wallet->isRunning())
        {
            m_wallet->start();
        }
    }
}

void AppModel::onStartedNode()
{
    m_nsc.disconnect();
    assert(m_wallet);

    if (!m_wallet->isRunning())
    {
        m_wallet->start();
    }
}

void AppModel::onFailedToStartNode(beam::wallet::ErrorType errorCode)
{
    m_nsc.disconnect();

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

void AppModel::start()
{
    m_nodeModel.setOwnerKey(m_db->get_OwnerKdf());

    std::string nodeAddrStr = m_settings.getNodeAddress().toStdString();
    if (m_settings.getRunLocalNode())
    {
        io::Address nodeAddr = io::Address::LOCALHOST;
        nodeAddr.port(m_settings.getLocalNodePort());
        nodeAddrStr = nodeAddr.str();
    }

    m_wallet = std::make_shared<WalletModel>(m_db, m_keyKeeper, nodeAddrStr, m_walletReactor);

    if (m_settings.getRunLocalNode())
    {
        startNode();
    }
    else
    {
        m_wallet->start();
    }
}

void AppModel::startNode()
{
    m_nsc
        << connect(&m_nodeModel, &NodeModel::startedNode, this, &AppModel::onStartedNode)
        << connect(&m_nodeModel, &NodeModel::failedToStartNode, this, &AppModel::onFailedToStartNode)
        << connect(&m_nodeModel, &NodeModel::failedToSyncNode, this, &AppModel::onFailedToStartNode);

    m_nodeModel.startNode();
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

WalletModel::Ptr AppModel::getWallet() const
{
    return m_wallet;
}

WalletSettings& AppModel::getSettings() const
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
