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
#include <QMessageBox>
#include <QStringBuilder>
#include <QApplication>
#include <QClipboard>
#include <QVariant>
#if defined(QT_PRINTSUPPORT_LIB)
#include <QtPrintSupport/qtprintsupportglobal.h>
#include <QPrinter>
#include <QPrintDialog>
#include <QPainter>
#include <QPaintEngine>
#include <QPrinterInfo>
#endif
#include "settings_view.h"
#include "model/app_model.h"
#include "wallet/secstring.h"
#include <thread>

#ifdef BEAM_USE_GPU
#include "utility/gpu/gpu_tools.h"
#endif

using namespace beam;
using namespace ECC;
using namespace std;

namespace
{
    const char* Testnet[] =
    {
#ifdef BEAM_TESTNET
        "13.250.45.106:8100",
        "54.169.161.81:8100",
        "18.136.210.119:8100",
        "54.169.159.14:8100",
        "54.169.184.144:8100",
        "54.93.49.101:8100",
        "18.185.184.24:8100",
        "18.185.69.110:8100",
        "18.194.255.84:8100",
        "18.197.107.66:8100",
        "13.57.227.248:8100",
        "13.52.103.105:8100",
        "54.183.139.124:8100",
        "54.153.64.192:8100",
        "54.215.250.187:8100"
 #else
        "172.104.249.212:8101",
        "23.239.23.209:8201",
        "172.105.211.232:8301",
        "96.126.111.61:8401",
        "176.58.98.195:8501"
#endif
    };

    const QChar PHRASES_SEPARATOR = ';';
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

const QString& RecoveryPhraseItem::getPhrase() const
{
    return m_phrase;
}

int RecoveryPhraseItem::getIndex() const
{
    return m_index;
}

StartViewModel::StartViewModel()
    : m_isRecoveryMode{false}
{

}

StartViewModel::~StartViewModel()
{

}

bool StartViewModel::walletExists() const
{
    return WalletDB::isInitialized(AppModel::getInstance()->getSettings().getWalletStorage());
}

bool StartViewModel::getIsRecoveryMode() const
{
    return m_isRecoveryMode;
}

void StartViewModel::setIsRecoveryMode(bool value)
{
    if (value != m_isRecoveryMode)
    {
        m_isRecoveryMode = value;
        m_recoveryPhrases.clear();
        AppModel::getInstance()->setRestoreWallet(value);
        emit isRecoveryModeChanged();
    }
}

const QList<QObject*>& StartViewModel::getRecoveryPhrases()
{
    if (m_recoveryPhrases.empty())
    {
        if (!m_isRecoveryMode)
        {
            m_generatedPhrases = beam::createMnemonic(beam::getEntropy(), beam::language::en);
        }
        else
        {
            m_generatedPhrases.resize(12);
        }
        assert(m_generatedPhrases.size() == 12);
        m_recoveryPhrases.reserve(static_cast<int>(m_generatedPhrases.size()));
        int i = 0;
        for (const auto& p : m_generatedPhrases)
        {
            m_recoveryPhrases.push_back(new RecoveryPhraseItem(i++, QString::fromStdString(p)));
        }
    }
    return m_recoveryPhrases;
}

const QList<QObject*>& StartViewModel::getCheckPhrases()
{
    if (m_checkPhrases.empty())
    {
        srand(time(0));
        set<int> indecies;
        while (indecies.size() < 6)
        {
            int index = rand() % m_recoveryPhrases.size();
            auto it = indecies.insert(index);
            if (it.second)
            {
                m_checkPhrases.push_back(new RecoveryPhraseItem(index, QString::fromStdString(m_generatedPhrases[index])));
            }
        }
    }

    return m_checkPhrases;
}

QChar StartViewModel::getPhrasesSeparator()
{
    return PHRASES_SEPARATOR;
}

void StartViewModel::setUseGpu(bool value)
{
#ifdef BEAM_USE_GPU
    if (value != AppModel::getInstance()->getSettings().getUseGpu())
    {
        AppModel::getInstance()->getSettings().setUseGpu(value);
        emit useGpuChanged();
    }
#endif
}

bool StartViewModel::getUseGpu() const
{
#ifdef BEAM_USE_GPU
    return AppModel::getInstance()->getSettings().getUseGpu();
#else
    return false;
#endif
}

bool StartViewModel::getIsRunLocalNode() const
{
    return AppModel::getInstance()->getSettings().getRunLocalNode();
}

QString StartViewModel::chooseRandomNode() const
{
    srand(time(0));
    return QString(Testnet[rand() % (sizeof(Testnet) / sizeof(Testnet[0]))]);
}

int StartViewModel::getLocalPort() const
{
    return AppModel::getInstance()->getSettings().getLocalNodePort();
}

int StartViewModel::getLocalMiningThreads() const
{
    return AppModel::getInstance()->getSettings().getLocalNodeMiningThreads();
}

QString StartViewModel::getRemoteNodeAddress() const
{
    return AppModel::getInstance()->getSettings().getNodeAddress();
}

QString StartViewModel::getLocalNodePeer() const
{
    auto peers = AppModel::getInstance()->getSettings().getLocalNodePeers();
    return !peers.empty() ? peers.first() : "";
}

void StartViewModel::setupLocalNode(int port, int miningThreads, const QString& localNodePeer)
{
    auto& settings = AppModel::getInstance()->getSettings();
#ifdef BEAM_USE_GPU
    if (settings.getUseGpu())
    {
        settings.setLocalNodeMiningThreads(1);
    }
    else
    {
        settings.setLocalNodeMiningThreads(miningThreads);
    }
#else
    settings.setLocalNodeMiningThreads(miningThreads);
#endif
    auto localAddress = QString::asprintf("127.0.0.1:%d", port);
    settings.setNodeAddress(localAddress);
    settings.setLocalNodePort(port);
    settings.setRunLocalNode(true);
    QStringList peers;
    peers.push_back(localNodePeer);
    settings.setLocalNodePeers(peers);
}

void StartViewModel::setupRemoteNode(const QString& nodeAddress)
{
    AppModel::getInstance()->getSettings().setRunLocalNode(false);
    AppModel::getInstance()->getSettings().setNodeAddress(nodeAddress);
}

void StartViewModel::setupRandomNode()
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
    for (const auto& p : m_generatedPhrases)
    {
        phrases = phrases % p.c_str() % PHRASES_SEPARATOR;
    }
    QApplication::clipboard()->setText(phrases);
}

void StartViewModel::printRecoveryPhrases(QVariant viewData )
{
    try
    {
        if (QPrinterInfo::availablePrinters().isEmpty())
        {
            AppModel::getInstance()->getMessages().addMessage(tr("Printer is not found. Please, check your printer preferences."));
            return;
        }
        QImage image = qvariant_cast<QImage>(viewData);
        QPrinter printer;
        printer.setOutputFormat(QPrinter::NativeFormat);
        printer.setColorMode(QPrinter::GrayScale);
        QPrintDialog dialog(&printer);
        if (dialog.exec() == QDialog::Accepted) {

            QPainter painter(&printer);
            
            QRect rect = painter.viewport();
            QFont f;
            f.setPixelSize(16);
            painter.setFont(f);
            int x = 60, y = 30;

            const int n = 4;
            int s = rect.width() / n;

            for (int i = 0; i < m_recoveryPhrases.size(); ++i)
            {
                if (i % n == 0)
                {
                    x = 60;
                    y += 30;
                }
                else
                {
                    x += s;
                }
                QString t = QString::number(i + 1) % " - " % m_generatedPhrases[i].c_str();
                painter.drawText(x, y, t);
                
            }
           
            //QRect rect = painter.viewport();
            //QSize size = image.size();
            //size.scale(rect.size(), Qt::KeepAspectRatio);
            //painter.setViewport(rect.x(), rect.y() + 60, size.width(), size.height());
            //painter.setWindow(image.rect());
            //painter.drawImage(0, 0, image);
            painter.end();
        }
    }
    catch (...)
    {
        AppModel::getInstance()->getMessages().addMessage(tr("Failed to print seed phrases. Please, check your printer."));
    }
}

void StartViewModel::resetPhrases()
{
    m_recoveryPhrases.clear();
    m_generatedPhrases.clear();
    m_checkPhrases.clear();
    emit recoveryPhrasesChanged();
}

bool StartViewModel::showUseGpu() const
{
#ifdef BEAM_USE_GPU
    return true;
#else
    return false;
#endif
}

bool StartViewModel::hasSupportedGpu()
{
#ifdef BEAM_USE_GPU
    if (!HasSupportedCard())
    {
        setUseGpu(false);
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool StartViewModel::createWallet()
{
    if (m_isRecoveryMode)
    {
        assert(m_generatedPhrases.size() == static_cast<size_t>(m_recoveryPhrases.size()));
        for (int i = 0; i < m_recoveryPhrases.size(); ++i)
        {
            QString s = static_cast<RecoveryPhraseItem*>(m_recoveryPhrases[i])->getValue();
            m_generatedPhrases[i] = s.toStdString();
        }
    }
    auto buf = beam::decodeMnemonic(m_generatedPhrases);

    SecString secretSeed;
    secretSeed.assign(buf.data(), buf.size());
    SecString sectretPass = m_password;
    return AppModel::getInstance()->createWallet(secretSeed, sectretPass);
}

bool StartViewModel::openWallet(const QString& pass)
{
    // TODO make this secure
    SecString secretPass = pass.toStdString();
    return AppModel::getInstance()->openWallet(secretPass);
}

void StartViewModel::setPassword(const QString& pass)
{
    m_password = pass.toStdString();
}