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

#include "start.h"
#include "wallet/keystore.h"
#include <QMessageBox>
#include "settings.h"
#include "model/app_model.h"

using namespace beam;
using namespace ECC;
using namespace std;

namespace
{
    const char* Testnet[] =
    {
#ifdef BEAM_TESTNET
        "45.79.216.245:7101",
        "45.79.216.245:7102",
        "69.164.197.237:7201",
        "69.164.197.237:7202",
        "69.164.203.140:7301",
        "69.164.203.140:7302",
        "172.104.146.73:7401",
        "172.104.146.73:7402",
        "173.230.154.68:7501",
        "173.230.154.68:7502",
        "85.159.211.40:7601",
        "85.159.211.40:7602",
        "213.168.248.160:7701",
        "213.168.248.160:7702",
        "172.104.30.189:7801",
        "172.104.30.189:7802",
        "172.104.191.23:7901",
        "172.104.191.23:7902",
        "172.105.231.90:7001",
        "172.105.231.90:7002",
#else
        "172.104.249.212:8101",
        "172.104.249.212:8102",
        "23.239.23.209:8201",
        "23.239.23.209:8202",
        "172.105.211.232:8301",
        "172.105.211.232:8302",
        "96.126.111.61:8401",
        "96.126.111.61:8402",
        "176.58.98.195:8501",
        "176.58.98.195:8502"
#endif
    };

}

StartViewModel::StartViewModel()
{

}

StartViewModel::~StartViewModel()
{

}

bool StartViewModel::walletExists() const
{
    return Keychain::isInitialized(AppModel::getInstance()->getSettings().getWalletStorage());
}

void StartViewModel::setupLocalNode(int port, int miningThreads)
{

}

void StartViewModel::setupRemoteNode(const QString& nodeAddress)
{
    AppModel::getInstance()->getSettings().setNodeAddress(nodeAddress);
}

void StartViewModel::setupTestnetNode()
{
    srand(time(0));
    auto nodeAddr = Testnet[rand() % (sizeof(Testnet) / sizeof(Testnet[0]))];
    AppModel::getInstance()->getSettings().setNodeAddress(nodeAddr);
}

bool StartViewModel::createWallet(const QString& seed, const QString& pass)
{
    NoLeak<uintBig> walletSeed;
    walletSeed.V = Zero;
    {
        Hash::Value hv;
        Hash::Processor() << seed.toStdString().c_str() >> hv;
        walletSeed.V = hv;
    }

    string walletPass = pass.toStdString();
    auto db = Keychain::init(AppModel::getInstance()->getSettings().getWalletStorage(), walletPass, walletSeed);

    if (db)
    {
        try
        {
            IKeyStore::Options options;
            options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
            options.fileName = AppModel::getInstance()->getSettings().getBbsStorage();

            IKeyStore::Ptr keystore = IKeyStore::create(options, walletPass.c_str(), walletPass.size());

            // generate default address
            WalletAddress defaultAddress = {};
            defaultAddress.m_own = true;
            defaultAddress.m_label = "default";
            defaultAddress.m_createTime = getTimestamp();
            defaultAddress.m_duration = numeric_limits<uint64_t>::max();
            keystore->gen_keypair(defaultAddress.m_walletID);
            keystore->save_keypair(defaultAddress.m_walletID, true);

            db->saveAddress(defaultAddress);

            auto nodeAddr = AppModel::getInstance()->getSettings().getNodeAddress().toStdString();
            WalletModel::Ptr walletModel = std::make_shared<WalletModel>(db, keystore, nodeAddr);
            AppModel::getInstance()->setWallet(walletModel);

            walletModel->start();
        }
        catch (const std::runtime_error&)
        {
            AppModel::getInstance()->getMessages().newMessage("Failed to generate default address");
        }

        return true;
    }

    return false;
}

bool StartViewModel::openWallet(const QString& pass)
{
    string walletPassword = pass.toStdString();
    auto db = Keychain::open(AppModel::getInstance()->getSettings().getWalletStorage(), walletPassword);

    if (db)
    {
        IKeyStore::Ptr keystore;
        IKeyStore::Options options;
        options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
        options.fileName = AppModel::getInstance()->getSettings().getBbsStorage();

        try
        {
            keystore = IKeyStore::create(options, walletPassword.c_str(), walletPassword.size());
        }
        catch (const beam::KeyStoreException& ex)
        {
        
            return false;
        }

        auto nodeAddr = AppModel::getInstance()->getSettings().getNodeAddress().toStdString();
        WalletModel::Ptr walletModel = std::make_shared<WalletModel>(db, keystore, nodeAddr);
        AppModel::getInstance()->setWallet(walletModel);

        walletModel->start();

        return true;
    }

    return false;
}
