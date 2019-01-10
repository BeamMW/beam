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
#include "wallet/secstring.h"
#include <memory>

class AppModel : public QObject
{
    Q_OBJECT
public:

    static AppModel* getInstance();

    AppModel(WalletSettings& settings);
    ~AppModel();

    WalletModel::Ptr getWallet() const;

    bool createWallet(const beam::SecString& seed, const beam::SecString& pass);
    bool openWallet(const beam::SecString& pass);
	bool checkWalletPassword(const beam::SecString& pass) const;
    void changeWalletPassword(const std::string& pass);

    void applySettingsChanges();

    WalletSettings& getSettings();
    MessageManager& getMessages();
    NodeModel& getNode();
    void resetWallet();

public slots:
	void startedNode();
    void stoppedNode();

private:
    void start();
	void OnWalledOpened(const beam::SecString& pass);
    void resetWalletImpl();

private:

    WalletModel::Ptr m_wallet;
    NodeModel m_nodeModel;
    WalletSettings& m_settings;
    MessageManager m_messages;
	ECC::NoLeak<ECC::uintBig> m_passwordHash;
    beam::IWalletDB::Ptr m_db;
    static AppModel* s_instance;
};
