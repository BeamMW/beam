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
    , m_restoreWallet{false}
{
    assert(s_instance == nullptr);
    s_instance = this;
}

AppModel::~AppModel()
{
    s_instance = nullptr;
}

bool AppModel::createWallet(const SecString& seed, const SecString& pass)
{
    m_db = WalletDB::init(m_settings.getWalletStorage(), pass, seed.hash());
    if (!m_db)
		return false;

    m_passwordHash = pass.hash();

    // generate default address
    WalletAddress defaultAddress = {};
    defaultAddress.m_label = "default";
    defaultAddress.m_createTime = getTimestamp();
    m_db->createAndSaveAddress(defaultAddress);

    start();

    return true;
}

bool AppModel::openWallet(const beam::SecString& pass)
{
    m_db = WalletDB::open(m_settings.getWalletStorage(), pass);
	if (!m_db)
		return false;

	m_passwordHash = pass.hash();

    start();
    return true;
}

void AppModel::applySettingsChanges()
{
    if (m_node)
    {
        m_node.reset();
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

void AppModel::start()
{
    if (m_settings.getRunLocalNode())
    {
        startNode();

        io::Address nodeAddr = io::Address::LOCALHOST;
        nodeAddr.port(m_settings.getLocalNodePort());
        m_wallet = std::make_shared<WalletModel>(m_db, nodeAddr.str());

        m_wallet->start();
    }
    else
    {
        auto nodeAddr = m_settings.getNodeAddress().toStdString();
        m_wallet = std::make_shared<WalletModel>(m_db, nodeAddr);

        m_wallet->start();
    }
}

void AppModel::startNode()
{
    m_node = make_unique<NodeModel>();
	m_node->m_pKdf = m_db->get_MasterKdf();
    m_node->start();
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
    assert(m_node);
    return *m_node;
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

void AppModel::setRestoreWallet(bool value)
{
    m_restoreWallet = value;
}

bool AppModel::shouldRestoreWallet() const
{
    return m_restoreWallet;
}
