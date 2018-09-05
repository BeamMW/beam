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
#include <thread>

using namespace beam;
using namespace ECC;
using namespace std;

#define BEAM_TESTNET

namespace
{
    const char* Testnet[] =
    {
#ifdef BEAM_TESTNET
        "104.248.112.39:8101",
        "104.248.144.187:8101",
        "104.248.144.248:8101",
        "104.248.55.141:8101",
        "104.248.55.33:8101",
        "104.248.59.230:8101",
        "104.248.62.29:8101",
        "139.59.138.214:8101",
        "139.59.85.59:8101",
        "139.59.88.178:8101",
        "142.93.34.30:8101",
        "142.93.85.129:8101",
        "142.93.93.121:8101",
        "159.203.16.71:8101",
        "159.203.62.139:8101",
        "159.65.159.100:8101",
        "159.65.199.166:8101",
        "159.89.159.90:8101",
        "159.89.179.243:8101",
        "165.227.103.139:8101",
        "165.227.105.73:8101",
        "167.99.152.50:8101",
        "167.99.184.124:8101",
        "167.99.184.99:8101",
        "167.99.191.32:8101",
        "167.99.209.93:8101",
        "167.99.223.132:8101",
        "178.128.147.10:8101",
        "206.189.129.73:8101",
        "206.189.173.169:8101",
        "206.189.201.131:8101",
        "207.154.228.133:8101",
        "207.154.228.141:8101",
        "207.154.236.20:8101",
        "207.154.236.21:8101",
        "209.97.181.63:8101",
        "209.97.186.216:8101",
        "209.97.188.105:8101",
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

    QString chooseRandomNode()
    {
        srand(time(0));
        return QString(Testnet[rand() % (sizeof(Testnet) / sizeof(Testnet[0]))]);
    }

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
    QStringList peers;
    peers.push_back(chooseRandomNode());
    settings.setLocalNodePeers(peers);
}

void StartViewModel::setupRemoteNode(const QString& nodeAddress)
{
    AppModel::getInstance()->getSettings().setNodeAddress(nodeAddress);
}

void StartViewModel::setupTestnetNode()
{
    AppModel::getInstance()->getSettings().setNodeAddress(chooseRandomNode());
}

uint StartViewModel::coreAmount() const
{
    return std::thread::hardware_concurrency();
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
