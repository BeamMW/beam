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

#include "settings.h"

#include <algorithm>
#include <map>

#include <QFileDialog>
#include <QtQuick>

#include "ui/model/app_model.h"
#include "version.h"

#include "quazip/quazip.h"
#include "quazip/quazipfile.h"

using namespace std;

namespace
{
    const char* kNodeAddressName = "node/address";
    const char* kLocaleName = "locale";
    const char* kLockTimeoutName = "lock_timeout";
    const char* kRequirePasswordToSpendMoney = "require_password_to_spend_money";
    const char* kIsAlowedBeamMWLink = "beam_mw_links_allowed";

    const char* kLocalNodeRun = "localnode/run";
    const char* kLocalNodePort = "localnode/port";
    const char* kLocalNodePeers = "localnode/peers";

    const char* kDefaultLocale = "en_US";

    const std::map<QString, QString> kSupportedLangs { 
        { "en_US", "English" }, 
        { "ru_RU", "Русский" } 
    };
}

const char* WalletSettings::WalletCfg = "beam-wallet.cfg";
const char* WalletSettings::LogsFolder = "logs";
const char* WalletSettings::SettingsFile = "settings.ini";
const char* WalletSettings::WalletDBFile = "wallet.db";
const char* WalletSettings::NodeDBFile = "node.db";

WalletSettings::WalletSettings(const QDir& appDataDir)
    : m_data{ appDataDir.filePath(SettingsFile), QSettings::IniFormat }
    , m_appDataDir{appDataDir}
{

}

string WalletSettings::getWalletStorage() const
{
    Lock lock(m_mutex);

    auto version = QString::fromStdString(PROJECT_VERSION);
    if (!m_appDataDir.exists(version))
    {
        m_appDataDir.mkdir(version);
    }
    
    return m_appDataDir.filePath(version + "/" + WalletDBFile).toStdString();
}

string WalletSettings::getAppDataPath() const
{
    Lock lock(m_mutex);
    return m_appDataDir.path().toStdString();
}

QString WalletSettings::getNodeAddress() const
{
    Lock lock(m_mutex);
    return m_data.value(kNodeAddressName).toString();
}

void WalletSettings::setNodeAddress(const QString& addr)
{
    if (addr != getNodeAddress())
    {
        auto walletModel = AppModel::getInstance()->getWallet();
        if (walletModel)
        {
            walletModel->getAsync()->setNodeAddress(addr.toStdString());
        }
        {
            Lock lock(m_mutex);
            m_data.setValue(kNodeAddressName, addr);
        }
        
        emit nodeAddressChanged();
    }
    
}

int WalletSettings::getLockTimeout() const
{
    Lock lock(m_mutex);
    return m_data.value(kLockTimeoutName, 0).toInt();
}

void WalletSettings::setLockTimeout(int value)
{
    if (value != getLockTimeout())
    {
        {
            Lock lock(m_mutex);
            m_data.setValue(kLockTimeoutName, value);
        }
        emit lockTimeoutChanged();
    }
}

bool WalletSettings::isPasswordReqiredToSpendMoney() const
{
    Lock lock(m_mutex);
    return m_data.value(kRequirePasswordToSpendMoney, false).toBool();
}

void WalletSettings::setPasswordReqiredToSpendMoney(bool value)
{
    Lock lock(m_mutex);
    m_data.setValue(kRequirePasswordToSpendMoney, value);
}

bool WalletSettings::isAllowedBeamMWLinks() const
{
    Lock lock(m_mutex);
    return m_data.value(kIsAlowedBeamMWLink, false).toBool();
}

void WalletSettings::setAllowedBeamMWLinks(bool value)
{
    Lock lock(m_mutex);
    m_data.setValue(kIsAlowedBeamMWLink, value);
}

bool WalletSettings::getRunLocalNode() const
{
    Lock lock(m_mutex);
    return m_data.value(kLocalNodeRun, false).toBool();
}

void WalletSettings::setRunLocalNode(bool value)
{
    {
        Lock lock(m_mutex);
        m_data.setValue(kLocalNodeRun, value);
    }
    emit localNodeRunChanged();
}

uint WalletSettings::getLocalNodePort() const
{
    Lock lock(m_mutex);
#ifdef BEAM_TESTNET
    return m_data.value(kLocalNodePort, 11005).toUInt();
#else
    return m_data.value(kLocalNodePort, 10005).toUInt();
#endif // BEAM_TESTNET
}

void WalletSettings::setLocalNodePort(uint port)
{
    {
        Lock lock(m_mutex);
        m_data.setValue(kLocalNodePort, port);
    }
    emit localNodePortChanged();
}

string WalletSettings::getLocalNodeStorage() const
{
    Lock lock(m_mutex);
    return m_appDataDir.filePath(NodeDBFile).toStdString();
}

string WalletSettings::getTempDir() const
{
    Lock lock(m_mutex);
    return m_appDataDir.filePath("./temp").toStdString();
}

static void zipLocalFile(QuaZip& zip, const QString& path, const QString& folder = QString())
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
    {
        QuaZipFile zipFile(&zip);

        zipFile.open(QIODevice::WriteOnly, QuaZipNewInfo((folder.isEmpty() ? "" : folder) + QFileInfo(file).fileName(), file.fileName()));
        zipFile.write(file.readAll());
        file.close();
        zipFile.close();
    }
}

QStringList WalletSettings::getLocalNodePeers() const
{
    Lock lock(m_mutex);
    return m_data.value(kLocalNodePeers).value<QStringList>();
}

void WalletSettings::setLocalNodePeers(const QStringList& qPeers)
{
    {
        Lock lock(m_mutex);
        m_data.setValue(kLocalNodePeers, QVariant::fromValue(qPeers));
    }
    emit localNodePeersChanged();
}

QString WalletSettings::getLocale() const
{
    QString savedLocale;
    {
        Lock lock(m_mutex);
        savedLocale = m_data.value(kLocaleName).toString();
    }

    if (!savedLocale.isEmpty()) {
        const auto& it = kSupportedLangs.find(savedLocale);
        if (it != kSupportedLangs.end())
        {
            return savedLocale;
        }
    }

    return QString::fromUtf8(kDefaultLocale);
}

QString WalletSettings::getLanguageName() const
{
    return kSupportedLangs.at(getLocale());
}

void WalletSettings::setLocaleByLanguageName(const QString& language)
{
    const auto& it = std::find_if(
            kSupportedLangs.begin(),
            kSupportedLangs.end(),
            [language] (const auto& mapedObject) -> bool
            {
                return mapedObject.second == language;
            });
    auto locale = 
            it != kSupportedLangs.end()
                ? it->first
                : QString::fromUtf8(kDefaultLocale);
    {
        Lock lock(m_mutex);
        m_data.setValue(kLocaleName, locale);
    }
    emit localeChanged();
}

// static
QStringList WalletSettings::getSupportedLanguages()
{
    QStringList languagesNames;
    std::transform(kSupportedLangs.begin(),
                   kSupportedLangs.end(),
                   std::back_inserter(languagesNames),
                   [] (const auto& lang) -> QString {
                       return lang.second;
                   });
    return languagesNames;
}

// static
void WalletSettings::openFolder(const QString& path)
{
    QFileInfo fileInfo(path);
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(
            fileInfo.isFile() ? fileInfo.absolutePath() : path));
}

void WalletSettings::reportProblem()
{
    auto logsFolder = QString::fromStdString(LogsFolder) + "/";

    QFile zipFile = m_appDataDir.filePath("beam v" + QString::fromStdString(PROJECT_VERSION) 
        + " " + QSysInfo::productType().toLower() + " report.zip");

    QuaZip zip(zipFile.fileName());
    zip.open(QuaZip::mdCreate);

    // save settings.ini
    zipLocalFile(zip, m_appDataDir.filePath(SettingsFile));

    // save .cfg
    zipLocalFile(zip, QDir(QDir::currentPath()).filePath(WalletCfg));

    // create 'logs' folder
    {
        QuaZipFile zipLogsFile(&zip);
        zipLogsFile.open(QIODevice::WriteOnly, QuaZipNewInfo(logsFolder, logsFolder));
        zipLogsFile.close();
    }

    {
        QDirIterator it(m_appDataDir.filePath(LogsFolder));

        while (it.hasNext())
        {
            zipLocalFile(zip, it.next(), logsFolder);
        }
    }

    {
        QDirIterator it(m_appDataDir);

        while (it.hasNext())
        {
            const auto& name = it.next();
            if (QFileInfo(name).completeSuffix() == "dmp")
            {
                zipLocalFile(zip, m_appDataDir.filePath(name));
            }
        }
    }

    zip.close();

    QString path = QFileDialog::getSaveFileName(nullptr, "Save problem report", 
        QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).filePath(QFileInfo(zipFile).fileName()),
        "Archives (*.zip)");

    if (path.isEmpty())
    {
        zipFile.remove();
    }
    else
    {
        {
            QFile file(path);
            if(file.exists())
                file.remove();
        }

        zipFile.rename(path);
    }
}

void WalletSettings::applyChanges()
{
    AppModel::getInstance()->applySettingsChanges();
}
