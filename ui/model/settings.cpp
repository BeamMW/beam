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
#include <QtQuick>
#include <QFileDialog>

#include "app_model.h"

#include "version.h"

#include "quazip/quazip.h"
#include "quazip/quazipfile.h"

using namespace std;

namespace
{
    const char* NodeAddressName = "node/address";
    const char* LockTimeoutName = "general/lock_timeout";

    const char* LocalNodeRun = "localnode/run";
    const char* LocalNodePort = "localnode/port";
    const char* LocalNodePeers = "localnode/peers";
#ifdef BEAM_USE_GPU
    const char* LocalNodeUseGpu = "localnode/use_gpu";
    const char* LocalNodeMiningDevices = "localnode/mining_devices";
#endif
}

const char* WalletSettings::WalletCfg = "beam-wallet.cfg";
const char* WalletSettings::LogsFolder = "logs";
const char* WalletSettings::SettingsFile = "settings.ini";
const char* WalletSettings::WalletDBFile = "wallet.db";

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
    return m_data.value(NodeAddressName).toString();
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
            m_data.setValue(NodeAddressName, addr);
        }
        
        emit nodeAddressChanged();
    }
    
}

int WalletSettings::getLockTimeout() const
{
    Lock lock(m_mutex);
    return m_data.value(LockTimeoutName, 0).toInt();
}

void WalletSettings::setLockTimeout(int value)
{
    if (value != getLockTimeout())
    {
        {
            Lock lock(m_mutex);
            m_data.setValue(LockTimeoutName, value);
        }
        emit lockTimeoutChanged();
    }
}

bool WalletSettings::getRunLocalNode() const
{
    Lock lock(m_mutex);
    return m_data.value(LocalNodeRun, false).toBool();
}

void WalletSettings::setRunLocalNode(bool value)
{
    {
        Lock lock(m_mutex);
        m_data.setValue(LocalNodeRun, value);
    }
    emit localNodeRunChanged();
}

uint WalletSettings::getLocalNodePort() const
{
    Lock lock(m_mutex);
    return m_data.value(LocalNodePort, 10005).toUInt();
}

void WalletSettings::setLocalNodePort(uint port)
{
    {
        Lock lock(m_mutex);
        m_data.setValue(LocalNodePort, port);
    }
    emit localNodePortChanged();
}

string WalletSettings::getLocalNodeStorage() const
{
    Lock lock(m_mutex);
    return m_appDataDir.filePath("node.db").toStdString();
}

string WalletSettings::getTempDir() const
{
    Lock lock(m_mutex);
    return m_appDataDir.filePath("./temp").toStdString();
}

#ifdef BEAM_USE_GPU
bool WalletSettings::getUseGpu() const
{
    Lock lock(m_mutex);
    return m_data.value(LocalNodeUseGpu, false).toBool();
}

void WalletSettings::setUseGpu(bool value)
{
    if (getUseGpu() != value)
    {
        {
            Lock lock(m_mutex);
            m_data.setValue(LocalNodeUseGpu, value);
        }
        emit localNodeUseGpuChanged();
    }
}

vector<int32_t> WalletSettings::getMiningDevices() const
{
    Lock lock(m_mutex);
    auto t = m_data.value(LocalNodeMiningDevices).value<QStringList>();
    vector<int32_t> v;
    for (const auto& i : t)
    {
        v.push_back(i.toInt());
    }

    return v;
}

void WalletSettings::setMiningDevices(const vector<int32_t>& value)
{
    if (getMiningDevices() != value)
    {
        {
            Lock lock(m_mutex);
            QStringList t;
            for (auto i : value)
            {
                t.push_back(QString::asprintf("%d", i));
            }
            if (t.empty())
            {
                m_data.remove(LocalNodeMiningDevices);
            }
            else
            {
                m_data.setValue(LocalNodeMiningDevices, QVariant::fromValue(t));
            }
        }
        emit localNodeMiningDevicesChanged();
    }
}

#endif

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
    return m_data.value(LocalNodePeers).value<QStringList>();
}

void WalletSettings::setLocalNodePeers(const QStringList& qPeers)
{
    {
        Lock lock(m_mutex);
        m_data.setValue(LocalNodePeers, QVariant::fromValue(qPeers));
    }
    emit localNodePeersChanged();
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
