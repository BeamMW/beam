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

#include "wallet.h"
#include "settings.h"
#include "messages.h"
#include "node.h"
#include "wallet/secstring.h"
#include <memory>
#include "wallet/keystore.h"

class AppModel
{
public:

    static AppModel* getInstance();

    AppModel(WalletSettings& settings);
    ~AppModel();

    WalletModel::Ptr getWallet() const;

    bool createWallet(const beam::SecString& seed, const beam::SecString& pass);
    bool openWallet(const beam::SecString& pass);

    WalletSettings& getSettings();
    MessageManager& getMessages();
private:
    void start(beam::IKeyChain::Ptr db, beam::IKeyStore::Ptr);

private:

    WalletModel::Ptr m_wallet;
    std::unique_ptr<NodeModel> m_node;
    WalletSettings& m_settings;
    MessageManager m_messages;
    static AppModel* s_instance;
};