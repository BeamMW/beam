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

#include "model/wallet.h"

class WalletSettings : public QObject
{
    Q_OBJECT
public:
    WalletSettings(const QString& iniPath);

    QString getNodeAddress() const;
    void setNodeAddress(const QString& value);

    void initModel(WalletModel::Ptr model);
    void loadSettings(const QString& iniPath);
    void setWalletStorage(const std::string& path);
    const std::string& getWalletStorage() const;
    void setBbsStorage(const std::string& path);
    const std::string& getBbsStorage() const;
    void emergencyReset();

signals:
    void nodeAddressChanged();

private:
    QSettings _data;
    std::string _walletStorage;
    std::string _bbsStorage;
};