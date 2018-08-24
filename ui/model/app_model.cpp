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

#include "app_model.h"

AppModel* AppModel::s_instance = nullptr;

AppModel* AppModel::getInstance()
{
    assert(s_instance != nullptr);
    return s_instance;
}

AppModel::AppModel(WalletSettings& settings)
    : m_settings{settings}
{
    assert(s_instance == nullptr);
    s_instance = this;
}

AppModel::~AppModel()
{
    s_instance = nullptr;
}


WalletModel::Ptr AppModel::getWallet() const
{
    return m_wallet;
}

void AppModel::setWallet(WalletModel::Ptr wallet)
{
    assert(wallet);
    m_wallet = wallet;
}

WalletSettings& AppModel::getSettings()
{
    return m_settings;
}

MessageManager& AppModel::getMessages()
{
    return m_messages;
}