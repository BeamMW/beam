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

}

void StartViewModel::setupTestnetNode()
{

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
