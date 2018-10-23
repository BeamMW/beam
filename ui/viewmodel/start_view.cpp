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
#include <QStringBuilder>
#include <QApplication>
#include <QClipboard>
#include "settings_view.h"
#include "model/app_model.h"
#include "wallet/secstring.h"
#include <thread>

using namespace beam;
using namespace ECC;
using namespace std;

namespace
{
    const char* Testnet[] =
    {
#ifdef BEAM_TESTNET
        "142.93.89.204:8101",
        "188.166.148.169:8101",
        "206.189.141.171:8101",
        "138.68.130.189:8101",
        "178.128.225.252:8101",
        "128.199.142.41:8101",
        "139.59.191.116:8101",
        "206.189.3.9:8101",
        "206.189.15.198:8101",
        "204.48.26.118:8101",
        "174.138.58:8101",
        "142.93.241.66:8101",
        "188.166.122.215:8101",
        "142.93.17.121:8101",
        "104.248.77.220:8101",
        "104.248.27.246:8101",
        "188.166.60.223:8101",
        "128.199.144.164:8101",
        "104.248.182.148:8101",
        "104.248.182.152:8101",
        "159.203.72.8:8101",
        "178.128.233.252:8101",
        "104.248.43.86:8101",
        "104.248.43.99:8101",
        "178.62.19.156:8101",
        "104.248.75.183:8101",
        "206.81.11.82:8101",
        "206.189.138.82:8101",
        "178.128.225.8:8101",
        "142.93.246.182:8101",
        "104.248.31.169:8101",
        "128.199.144.48:8101",
        "178.128.229.48:8101",
        "128.199.144.196:8101",
        "159.65.40.42:8101",
        "178.128.229.50:8101",
        "138.197.193.229:8101",
        "128.199.144.206:8101",
        "178.128.229.65:8101",
        "159.89.234.65:8101",
        "104.248.43.120:8101",
        "104.248.186.25:8101",
        "128.199.145.212:8101",
        "188.166.15.205:8101",
        "138.68.163.99:8101"
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

RecoveryPhraseItem::RecoveryPhraseItem(int index, const QString& phrase)
    : m_index(index)
    , m_phrase(phrase)
{

}

RecoveryPhraseItem::~RecoveryPhraseItem()
{

}

bool RecoveryPhraseItem::isCorrect() const
{
    return m_userInput == m_phrase;
}
const QString& RecoveryPhraseItem::getValue() const
{
    return m_userInput;
}
void RecoveryPhraseItem::setValue(const QString& value)
{
    if (m_userInput != value)
    {
        m_userInput = value;
        emit valueChanged();
        emit isCorrectChanged();
    }
}

int RecoveryPhraseItem::getIndex() const
{
    return m_index;
}

StartViewModel::StartViewModel()
{
    m_recoveryPhrases.push_back("void");
    m_recoveryPhrases.push_back("come");
    m_recoveryPhrases.push_back("effort");
    m_recoveryPhrases.push_back("suffer");
    m_recoveryPhrases.push_back("camp");
    m_recoveryPhrases.push_back("survey");
    m_recoveryPhrases.push_back("warrior");
    m_recoveryPhrases.push_back("heavy");
    m_recoveryPhrases.push_back("shoot");
    m_recoveryPhrases.push_back("primary");
    m_recoveryPhrases.push_back("clutch");
    m_recoveryPhrases.push_back("crush");
    //m_recoveryPhrases.push_back("open");
    //m_recoveryPhrases.push_back("amazing");
    //m_recoveryPhrases.push_back("screen");
    //m_recoveryPhrases.push_back("patrol");
    //m_recoveryPhrases.push_back("group");
    //m_recoveryPhrases.push_back("space");
    //m_recoveryPhrases.push_back("point");
    //m_recoveryPhrases.push_back("ten");
    //m_recoveryPhrases.push_back("exist");
    //m_recoveryPhrases.push_back("slush");
    //m_recoveryPhrases.push_back("involve");
    //m_recoveryPhrases.push_back("unfold");
}

StartViewModel::~StartViewModel()
{

}

bool StartViewModel::walletExists() const
{
    return Keychain::isInitialized(AppModel::getInstance()->getSettings().getWalletStorage());
}

const QStringList& StartViewModel::getRecovertPhrases() const
{
    return m_recoveryPhrases;
}

const QList<QObject*>& StartViewModel::getCheckPhrases()
{
    if (m_checkPhrases.empty())
    {
        srand(time(0));
        for (int i = 0; i < 6; ++i)
        {
            int index = rand() % m_recoveryPhrases.size();
            m_checkPhrases.push_back(new RecoveryPhraseItem(index, m_recoveryPhrases[index]));
        }
    }
    return m_checkPhrases;
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
    AppModel::getInstance()->getSettings().setRunLocalNode(false);
    AppModel::getInstance()->getSettings().setNodeAddress(nodeAddress);
}

void StartViewModel::setupTestnetNode()
{
    AppModel::getInstance()->getSettings().setRunLocalNode(false);
    AppModel::getInstance()->getSettings().setNodeAddress(chooseRandomNode());
}

uint StartViewModel::coreAmount() const
{
    return std::thread::hardware_concurrency();
}

void StartViewModel::copyPhrasesToClipboard()
{
    QString phrases;

    int i = 1;
    for (const auto& p : m_recoveryPhrases)
    {
        phrases = phrases % QString::number(i) % " - " % p % "; ";
        ++i;
    }
    QApplication::clipboard()->setText(phrases);
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
