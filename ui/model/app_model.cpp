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
    NoLeak<uintBig> walletSeed;
    walletSeed.V = Zero;
    {
        Hash::Value hv;
        Hash::Processor() << seed.data() >> hv;
        walletSeed.V = hv;
    }

    auto db = Keychain::init(m_settings.getWalletStorage(), pass, walletSeed);

    if (db)
    {
        try
        {
            IKeyStore::Options options;
            options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
            options.fileName = m_settings.getBbsStorage();

            IKeyStore::Ptr keystore = IKeyStore::create(options, pass.data(), pass.size());

            // generate default address
            WalletAddress defaultAddress = {};
            defaultAddress.m_own = true;
            defaultAddress.m_label = "default";
            defaultAddress.m_createTime = getTimestamp();
            defaultAddress.m_duration = numeric_limits<uint64_t>::max();
            keystore->gen_keypair(defaultAddress.m_walletID);
            keystore->save_keypair(defaultAddress.m_walletID, true);

            db->saveAddress(defaultAddress);

            start(db, keystore);
        }
        catch (const std::runtime_error&)
        {
            m_messages.newMessage("Failed to generate default address");
        }

        return true;
    }

    return false;
}

bool AppModel::openWallet(const beam::SecString& pass)
{
    auto db = Keychain::open(m_settings.getWalletStorage(), pass);

    if (db)
    {
        IKeyStore::Ptr keystore;
        IKeyStore::Options options;
        options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
        options.fileName = m_settings.getBbsStorage();

        try
        {
            keystore = IKeyStore::create(options, pass.data(), pass.size());
        }
        catch (const beam::KeyStoreException& ex)
        {
            return false;
        }

        start(db, keystore);
        return true;
    }
    return false;
}

void AppModel::start(IKeyChain::Ptr db, IKeyStore::Ptr keystore)
{
    auto nodeAddr = m_settings.getNodeAddress().toStdString();
    m_wallet = std::make_shared<WalletModel>(db, keystore, nodeAddr);

    m_wallet->start();
   
    if (m_settings.getRunLocalNode())
    {
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = Zero;
        if (m_settings.getLocalNodeMiningThreads() > 0)
        {
            db->getVar("WalletSeed", seed);
        }
        
        m_node = make_unique<NodeModel>(seed);
        m_node->start();
    }
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