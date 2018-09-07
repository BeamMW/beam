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
    const char* LocalNodeMiningThreads = "localnode/mining_threads";
    const char* LocalNodeVerificationThreads = "localnode/verification_threads";
    const char* LocalNodeGenerateGenesys = "localnode/generate_genesys";
    const char* LocalNodeSynchronized = "localnode/synchronized";
    const char* LocalNodePeers = "localnode/peers";

    const char* SettingsIni = "settings.ini";
}

const char* WalletSettings::WalletCfg = "beam-wallet.cfg";
const char* WalletSettings::LogsFolder = "logs";

WalletSettings::WalletSettings(const QDir& appDataDir)
    : m_data{ appDataDir.filePath(SettingsIni), QSettings::IniFormat }
    , m_appDataDir{appDataDir}
{

}

string WalletSettings::getWalletStorage() const
{
    Lock lock(m_mutex);
    return m_appDataDir.filePath("wallet.db").toStdString();
}

string WalletSettings::getBbsStorage() const
{
    Lock lock(m_mutex);
    return m_appDataDir.filePath("keys.bbs").toStdString();
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
            walletModel->async->setNodeAddress(addr.toStdString());
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

void WalletSettings::emergencyReset()
{
    auto walletModel = AppModel::getInstance()->getWallet();
    if (walletModel)
    {
        walletModel->async->emergencyReset();
    }
}

bool WalletSettings::getGenerateGenesys() const
{
    Lock lock(m_mutex);
    return m_data.value(LocalNodeGenerateGenesys, false).toBool();
}

void WalletSettings::setGenerateGenesys(bool value)
{
    if (getGenerateGenesys() != value)
    {
        {
            Lock lock(m_mutex);
            m_data.setValue(LocalNodeGenerateGenesys, value);
        }
        emit localNodeGenerateGenesysChanged();
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
    return m_data.value(LocalNodePort, 10000).toUInt();
}

void WalletSettings::setLocalNodePort(uint port)
{
    {
        Lock lock(m_mutex);
        m_data.setValue(LocalNodePort, port);
    }
    emit localNodePortChanged();
}

uint WalletSettings::getLocalNodeMiningThreads() const
{
    Lock lock(m_mutex);
    return m_data.value(LocalNodeMiningThreads, 1).toUInt();
}

void WalletSettings::setLocalNodeMiningThreads(uint n)
{
    {
        Lock lock(m_mutex);
        m_data.setValue(LocalNodeMiningThreads, n);
    }
    emit localNodeMiningThreadsChanged();
}

uint WalletSettings::getLocalNodeVerificationThreads() const
{
    Lock lock(m_mutex);
    return m_data.value(LocalNodeVerificationThreads, 1).toUInt();
}

void WalletSettings::setLocalNodeVerificationThreads(uint n)
{
    {
        Lock lock(m_mutex);
        m_data.setValue(LocalNodeVerificationThreads, n);
    }
    emit localNodeVerificationThreadsChanged();
}

bool WalletSettings::getLocalNodeSynchronized() const
{
    Lock lock(m_mutex);
    return m_data.value(LocalNodeSynchronized, false).toBool();
}

void WalletSettings::setLocalNodeSynchronized(bool value)
{
    if (getLocalNodeSynchronized() != value)
    {
        {
            Lock lock(m_mutex);
            m_data.setValue(LocalNodeSynchronized, value);
        }
        emit localNodeSynchronizedChanged();
    }
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
	zipLocalFile(zip, m_appDataDir.filePath(SettingsIni));

	// save .cfg
	zipLocalFile(zip, QDir(QDir::currentPath()).filePath(WalletCfg));

	// create 'logs' folder
	{
		QuaZipFile zipFile(&zip);
		zipFile.open(QIODevice::WriteOnly, QuaZipNewInfo(logsFolder, logsFolder));
		zipFile.close();
	}

	QDirIterator it(m_appDataDir.filePath(LogsFolder));

	while (it.hasNext())
	{
		zipLocalFile(zip, it.next(), logsFolder);
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
