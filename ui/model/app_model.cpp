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
    m_db = Keychain::init(m_settings.getWalletStorage(), pass, seed.hash());

    if (m_db)
    {
        try
        {
            m_passwordHash = pass.hash();

            IKeyStore::Options options;
            options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
            options.fileName = m_settings.getBbsStorage();

            IKeyStore::Ptr keystore = IKeyStore::create(options, pass.data(), pass.size());

            // generate default address
            WalletAddress defaultAddress = {};
            defaultAddress.m_own = true;
            defaultAddress.m_label = "default";
            defaultAddress.m_createTime = getTimestamp();
            keystore->gen_keypair(defaultAddress.m_walletID);
            keystore->save_keypair(defaultAddress.m_walletID, true);

            m_db->saveAddress(defaultAddress);

            start(keystore);
        }
        catch (const beam::KeyStoreException&)
        {
            m_messages.addMessage("Failed to generate default address");
            return false;
        }

        return true;
    }

    return false;
}

bool AppModel::openWallet(const beam::SecString& pass)
{
    m_db = Keychain::open(m_settings.getWalletStorage(), pass);

    if (m_db)
    {
		m_passwordHash = pass.hash();

        IKeyStore::Ptr keystore;
        IKeyStore::Options options;
        options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
        options.fileName = m_settings.getBbsStorage();

        try
        {
            keystore = IKeyStore::create(options, pass.data(), pass.size());
        }
        catch (const beam::KeyStoreException& )
        {
            return false;
        }

        start(keystore);
        return true;
    }
    return false;
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

        m_wallet->async->setNodeAddress(nodeAddr.str());
    }
    else
    {
        auto nodeAddr = m_settings.getNodeAddress().toStdString();
        m_wallet->async->setNodeAddress(nodeAddr);
    }
}

void AppModel::start(IKeyStore::Ptr keystore)
{
    if (m_settings.getRunLocalNode())
    {
        startNode();

        io::Address nodeAddr = io::Address::LOCALHOST;
        nodeAddr.port(m_settings.getLocalNodePort());
        m_wallet = std::make_shared<WalletModel>(m_db, keystore, nodeAddr.str());

        m_wallet->start();
    }
    else
    {
        auto nodeAddr = m_settings.getNodeAddress().toStdString();
        m_wallet = std::make_shared<WalletModel>(m_db, keystore, nodeAddr);

        m_wallet->start();
    }
}

void AppModel::startNode()
{
    m_node = make_unique<NodeModel>();
	m_node->m_pKdf = m_db->get_Kdf();
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

    m_wallet->async->changeWalletPassword(pass);
}
