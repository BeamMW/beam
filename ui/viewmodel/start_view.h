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
#include <functional>

#include "wallet/wallet_db.h"

#include "messages_view.h"

class StartViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool walletExists READ walletExists NOTIFY walletExistsChanged)
public:

    using DoneCallback = std::function<bool (beam::IKeyChain::Ptr db, const std::string& walletPass)>;

    StartViewModel();
    ~StartViewModel();

    bool walletExists() const;

    Q_INVOKABLE void setupLocalNode(int port, int miningThreads, bool generateGenesys = false);
    Q_INVOKABLE void setupRemoteNode(const QString& nodeAddress);
    Q_INVOKABLE void setupTestnetNode();

signals:
    void walletExistsChanged();
    void generateGenesysyBlockChanged();

public slots:
    bool createWallet(const QString& seed, const QString& pass);
    bool openWallet(const QString& pass);
};
