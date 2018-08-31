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

#include "start_view.h"
#include "wallet/keystore.h"
#include <QMessageBox>
#include "settings_view.h"
#include "model/app_model.h"
#include "wallet/secstring.h"

using namespace beam;
using namespace ECC;
using namespace std;

define BEAM_TESTNET

namespace
{
    const char* Testnet[] =
    {
#ifdef BEAM_TESTNET
        "45.79.216.245:8101",
        "69.164.197.237:8101",
        "69.164.203.140:8101",
        "172.104.146.73:8101",
        "173.230.154.68:8101",
        "85.159.211.40:8101",
        "213.168.248.160:8101",
        "172.104.30.189:8101",
        "172.104.191.23:8101",
        "172.105.231.90:8101",
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

void StartViewModel::setupLocalNode(int port, int miningThreads, bool generateGenesys)
{
    auto& settings = AppModel::getInstance()->getSettings();
    settings.setLocalNodeMiningThreads(miningThreads);
    auto localAddress = QString::asprintf("127.0.0.1:%d", port);
    settings.setNodeAddress(localAddress);
    settings.setLocalNodePort(port);
    settings.setRunLocalNode(true);
    settings.setGenerateGenesys(generateGenesys);
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
    // TODO make this secure
    SecString secretSeed = seed.toStdString();
    SecString sectretPass = pass.toStdString();
    return AppModel::getInstance()->createWallet(secretSeed, sectretPass);
}

bool StartViewModel::openWallet(const QString& pass)
{
    // TODO make this secure
    SecString secretPass = pass.toStdString();
    return AppModel::getInstance()->openWallet(secretPass);
}
