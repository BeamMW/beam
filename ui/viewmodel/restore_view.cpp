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

#include "restore_view.h"
#include "model/app_model.h"

using namespace beam;
using namespace std;

RestoreViewModel::RestoreViewModel()
    : _model(*AppModel::getInstance()->getWallet())
{
   // connect(&_model, SIGNAL(onStatus(const WalletStatus&)), SLOT(onStatus(const WalletStatus&)));

    connect(&_model, SIGNAL(onRecoverProgressUpdated(int, int, const QString&)),
        SLOT(onRestoreProgressUpdated(int, int, const QString&)));
}

RestoreViewModel::~RestoreViewModel()
{
}


void RestoreViewModel::restoreFromBlockchain()
{
    WalletModel& wallet = *AppModel::getInstance()->getWallet();
    if (wallet.async)
    {
        wallet.async->restoreFromBlockchain();
    }

    
}

void RestoreViewModel::onRestoreProgressUpdated(int, int, const QString&)
{

}