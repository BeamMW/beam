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

class AppModel
{
public:

    static AppModel* getInstance();

    AppModel(WalletSettings& settings);
    ~AppModel();

    WalletModel::Ptr getWallet() const;
    void setWallet(WalletModel::Ptr wallet);

    WalletSettings& getSettings();
    MessageManager& getMessages();

private:

    WalletModel::Ptr m_wallet;
    WalletSettings& m_settings;
    MessageManager m_messages;
    static AppModel* s_instance;
};