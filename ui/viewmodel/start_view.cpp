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
#include <QDateTime>
#include <QMessageBox>
#include <QStringBuilder>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QVariant>
#include <QStandardPaths>
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
#include "model/keyboard.h"
#include "version.h"
#include "wallet/secstring.h"
#include "wallet/default_peers.h"

#include <boost/filesystem.hpp>
#include <thread>

using namespace beam;
using namespace ECC;
using namespace std;

namespace
{
    const QChar PHRASES_SEPARATOR = ';';

    boost::filesystem::path pathFromStdString(const std::string& path)
    {
#ifdef WIN32
        boost::filesystem::path boostPath{ Utf8toUtf16(path.c_str()) };
#else
        boost::filesystem::path boostPath{ path };
#endif
        return boostPath;
    }

    std::vector<boost::filesystem::path> findAllWalletDB(const std::string& appPath)
    {
        std::vector<boost::filesystem::path> walletDBs;
        try
        {
            auto appDataPath = pathFromStdString(appPath);

            if (!boost::filesystem::exists(appDataPath))
            {
                return {};
            }

            for (boost::filesystem::recursive_directory_iterator endDirIt, it{ appDataPath }; it != endDirIt; ++it)
            {
                if (it.level() > 1)
                {
                    it.pop();
                    if (it == endDirIt)
                    {
                        break;
                    }
                }

                if (it->path().filename() == WalletSettings::WalletDBFile)
                {
                    walletDBs.push_back(it->path());
                }
            }
        }
        catch (std::exception &e)
        {
            LOG_ERROR() << e.what();
        }

        return walletDBs;
    }

    void removeNodeDataIfNeeded()
    {
        try
        {
            auto appDataPath = pathFromStdString(AppModel::getInstance()->getSettings().getAppDataPath());

            if (!boost::filesystem::exists(appDataPath))
            {
                return;
            }
            string nodePath = AppModel::getInstance()->getSettings().getLocalNodeStorage();
            try
            {
                beam::NodeDB nodeDB;
                nodeDB.Open(nodePath.c_str());
                return;
            }
            catch (const beam::NodeDBUpgradeException&)
            {
            }
            
            boost::filesystem::remove(pathFromStdString(nodePath));

            std::vector<boost::filesystem::path> macroBlockFiles;
            for (boost::filesystem::directory_iterator endDirIt, it{ appDataPath }; it != endDirIt; ++it)
            {
                if (it->path().filename().wstring().find(L"tempmb") == 0)
                {
                    macroBlockFiles.push_back(it->path());
                }
            }

            for (auto& path : macroBlockFiles)
            {
                boost::filesystem::remove(path);
            }
        }
        catch (std::exception &e)
        {
            LOG_ERROR() << e.what();
        }
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

bool RecoveryPhraseItem::isAllowed() const
{
    return isAllowedWord(m_userInput.toStdString(), language::en);
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
        emit isAllowedChanged();
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

WalletDBPathItem::WalletDBPathItem(const std::string& walletDBPath, uintmax_t fileSize, time_t lastWriteTime)
    : m_fullPath{walletDBPath}
    , m_fileSize(fileSize)
    , m_lastWriteTime(lastWriteTime)
{
}

WalletDBPathItem::~WalletDBPathItem()
{
}

int WalletDBPathItem::getFileSize() const
{
    return m_fileSize;
}

QString WalletDBPathItem::getFullPath() const
{
    return QString::fromStdString(m_fullPath);
}

QString WalletDBPathItem::getShortPath() const
{
    return QString();
}

QString WalletDBPathItem::getLastWriteDateString() const
{
    QDateTime datetime = QDateTime::fromTime_t(m_lastWriteTime);
    return datetime.toString(Qt::SystemLocaleShortDate);
}

StartViewModel::StartViewModel()
    : m_isRecoveryMode{false}
{
    if (!walletExists())
    {
        // find all wallet.db in appData and defaultAppData
        findExistingWalletDB();
        removeNodeDataIfNeeded();
    }
}

StartViewModel::~StartViewModel()
{
    qDeleteAll(m_walletDBpaths);
}

bool StartViewModel::walletExists() const
{
    return wallet::WalletDB::isInitialized(AppModel::getInstance()->getSettings().getWalletStorage());
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

bool StartViewModel::getIsRunLocalNode() const
{
    return AppModel::getInstance()->getSettings().getRunLocalNode();
}

QString StartViewModel::chooseRandomNode() const
{
    auto peers = getDefaultPeers();
    srand(time(0));
    return QString(peers[rand() % peers.size()].c_str());
}

QString StartViewModel::walletVersion() const
{
    return QString::fromStdString(PROJECT_VERSION);
}

int StartViewModel::getLocalPort() const
{
    return AppModel::getInstance()->getSettings().getLocalNodePort();
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

QQmlListProperty<WalletDBPathItem> StartViewModel::getWalletDBpaths()
{
    return QQmlListProperty<WalletDBPathItem>(this, m_walletDBpaths);
}

bool StartViewModel::isCapsLockOn() const
{
    return keyboard::isCapsLockOn();
}

void StartViewModel::setupLocalNode(int port, const QString& localNodePeer)
{
    auto& settings = AppModel::getInstance()->getSettings();
    auto localAddress = QString::asprintf("127.0.0.1:%d", port);
    settings.setNodeAddress(localAddress);
    settings.setLocalNodePort(port);
    settings.setRunLocalNode(true);
    QStringList peers;
    
    for (const auto& peer : getDefaultPeers())
    {
        if (localNodePeer != peer.c_str())
        {
            peers.push_back(peer.c_str());
        }
    }

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
            //% "Printer is not found. Please, check your printer preferences."
            AppModel::getInstance()->getMessages().addMessage(qtTrId("start-view-printer-not-found-error"));
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
        //% "Failed to print seed phrase. Please, check your printer."
        AppModel::getInstance()->getMessages().addMessage(qtTrId("start-view-printer-error"));
    }
}

void StartViewModel::resetPhrases()
{
    m_recoveryPhrases.clear();
    m_generatedPhrases.clear();
    m_checkPhrases.clear();
    emit recoveryPhrasesChanged();
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

bool StartViewModel::checkWalletPassword(const QString& password) const
{
    SecString secretPassword = password.toStdString();
    return AppModel::getInstance()->checkWalletPassword(secretPassword);
}

void StartViewModel::setPassword(const QString& pass)
{
    m_password = pass.toStdString();
}

void StartViewModel::findExistingWalletDB()
{
    auto appDataPath = AppModel::getInstance()->getSettings().getAppDataPath();
    auto defaultAppDataPath = QDir(QStandardPaths::writableLocation(QStandardPaths::DataLocation)).path().toStdString();

    auto walletDBs = findAllWalletDB(appDataPath);

    if (appDataPath != defaultAppDataPath)
    {
        auto additionnalWalletDBs = findAllWalletDB(defaultAppDataPath);
        walletDBs.reserve(walletDBs.size() + additionnalWalletDBs.size());
        walletDBs.insert(std::end(walletDBs), std::begin(additionnalWalletDBs), std::end(additionnalWalletDBs));
    }

    for (auto& walletDBPath : walletDBs)
    {
        auto fileSize = boost::filesystem::file_size(walletDBPath);
        auto lastWriteTime = boost::filesystem::last_write_time(walletDBPath);
        m_walletDBpaths.push_back(new WalletDBPathItem(walletDBPath.generic_string(), fileSize, lastWriteTime));
    }
}

bool StartViewModel::isFindExistingWalletDB()
{
    return !m_walletDBpaths.empty();
}

void StartViewModel::deleteCurrentWalletDB()
{
    try
    {
        auto pathToDB = pathFromStdString(AppModel::getInstance()->getSettings().getWalletStorage());
        boost::filesystem::remove(pathToDB);
    }
    catch (std::exception& e)
    {
        LOG_ERROR() << e.what();
    }
}

void StartViewModel::migrateWalletDB(const QString& path)
{
    try
    {
        auto pathSrc = pathFromStdString(path.toStdString());
        auto pathDst = pathFromStdString(AppModel::getInstance()->getSettings().getWalletStorage());
        boost::filesystem::copy_file(pathSrc, pathDst);
    }
    catch (std::exception& e)
    {
        LOG_ERROR() << e.what();
    }
}

void StartViewModel::copyToClipboard(const QString& text)
{
    QApplication::clipboard()->setText(text);
}

QString StartViewModel::selectCustomWalletDB()
{
    QString filePath = QFileDialog::getOpenFileName(
        nullptr,
        //% "Select the wallet database file"
        qtTrId("start-view-select-db"),
        //% "SQLite database file (*.db)"
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), qtTrId("start-view-db-file-filter"));

    return filePath;
}

QString StartViewModel::defaultPortToListen() const
{
#ifdef BEAM_TESTNET
    return "11005";
#else
    return "10005";
#endif  // BEAM_TESTNET
}

QString StartViewModel::defaultRemoteNodeAddr() const
{
#ifdef BEAM_TESTNET
    return "127.0.0.1:11005";
#else
    return "127.0.0.1:10005";
#endif // BEAM_TESTNET
}

void StartViewModel::checkCapsLock()
{
    emit capsLockStateMayBeChanged();
}
