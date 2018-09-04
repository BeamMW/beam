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
