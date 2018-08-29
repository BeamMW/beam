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

#pragma once

#include <QObject>
#include <QSettings>
#include <QDir>

#include "model/wallet_model.h"

class WalletSettings : public QObject
{
    Q_OBJECT
public:
    WalletSettings(const QDir& appDataDir);

    QString getNodeAddress() const;
    void setNodeAddress(const QString& value);

    void initModel(WalletModel::Ptr model);
    std::string getWalletStorage() const;
    std::string getBbsStorage() const;
    void emergencyReset();
	void reportProblem();

    bool getRunLocalNode() const;
    void setRunLocalNode(bool value);

    short getLocalNodePort() const;
    void setLocalNodePort(short port);
    int getLocalNodeMiningThreads() const;
    void setLocalNodeMiningThreads(int n);
    int getLocalNodeVerificationThreads() const;
    void setLocalNodeVerificationThreads(int n);
    std::string getLocalNodeStorage() const;
    std::string getTempDir() const;

signals:
    void nodeAddressChanged();
    void localNodeRunChanged();
    void localNodePortChanged();
    void localNodeMiningThreadsChanged();
    void localNodeVerificationThreadsChanged();

private:
    QSettings m_data;
    QDir m_appDataDir;
};