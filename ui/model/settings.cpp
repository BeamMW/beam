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

#include "app_model.h"

using namespace std;

namespace
{
    const char* NodeAddressName = "node/address";
}

WalletSettings::WalletSettings(const QString& iniPath)
    : _data{ iniPath, QSettings::IniFormat }
{

}

void WalletSettings::setWalletStorage(const string& path)
{
    _walletStorage = path;
}

const string& WalletSettings::getWalletStorage() const
{
    return _walletStorage;
}

void WalletSettings::setBbsStorage(const string& path)
{
    _bbsStorage = path;
}

const string& WalletSettings::getBbsStorage() const
{
    return _bbsStorage;
}

QString WalletSettings::getNodeAddress() const
{
    return _data.value(NodeAddressName).toString();
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
        _data.setValue(NodeAddressName, addr);
        emit nodeAddressChanged();
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
