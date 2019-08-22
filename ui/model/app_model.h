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

#include "wallet_model.h"
#include "settings.h"
#include "messages.h"
#include "node_model.h"
#include "helpers.h"
#include "wallet/secstring.h"
#include "wallet/private_key_keeper.h"
#include <memory>

class AppModel final: public QObject
{
    Q_OBJECT
public:
    static AppModel& getInstance();

    AppModel(WalletSettings& settings);
    ~AppModel() override;

    bool createWallet(const beam::SecString& seed, const beam::SecString& pass);
    bool openWallet(const beam::SecString& pass);
    bool checkWalletPassword(const beam::SecString& pass) const;
    void changeWalletPassword(const std::string& pass);

    void applySettingsChanges();
    void nodeSettingsChanged();
    void resetWallet();

    WalletModel::Ptr getWallet() const;
    WalletSettings& getSettings() const;
    MessageManager& getMessages();
    NodeModel& getNode();

public slots:
    void onStartedNode();
    void onFailedToStartNode(beam::wallet::ErrorType errorCode);

signals:
    void walletReseted();

private:
    void start();
    void startNode();
    void resetWalletImpl();
    void onWalledOpened(const beam::SecString& pass);

private:
    WalletModel::Ptr m_wallet;
    beam::wallet::IPrivateKeyKeeper::Ptr m_keyKeeper;
    NodeModel m_nodeModel;
    WalletSettings& m_settings;
    MessageManager m_messages;
    ECC::NoLeak<ECC::uintBig> m_passwordHash;
    beam::io::Reactor::Ptr m_walletReactor;
    beam::wallet::IWalletDB::Ptr m_db;
    Connections m_nsc; // [n]ode [s]tarting [c]connections
    static AppModel* s_instance;
};
