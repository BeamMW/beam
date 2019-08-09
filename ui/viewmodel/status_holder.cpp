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
#include "status_holder.h"
#include "model/app_model.h"

StatusHolder::StatusHolder()
    : _model(*AppModel::getInstance().getWallet())
    , _status{ 0, 0, 0, 0, {0, 0, 0}, {}}
{
    connect(&_model, &WalletModel::walletStatus, this, &StatusHolder::onStatusChanged);
}

void StatusHolder::onStatusChanged(const beam::wallet::WalletStatus& status)
{
    bool changed = false;

    if (_status.available != status.available)
    {
        _status.available = status.available;
        changed = true;
    }

    if (_status.receiving != status.receiving)
    {
        _status.receiving = status.receiving;
        changed = true;
    }

    if (_status.sending != status.sending)
    {
        _status.sending = status.sending;
        changed = true;
    }

    if (_status.maturing != status.maturing)
    {
        _status.maturing = status.maturing;
        changed = true;
    }

    if (_status.update.lastTime != status.update.lastTime)
    {
        _status.update.lastTime = status.update.lastTime;
        changed = true;
    }

    if (changed && _changedCB)
    {
        _changedCB();
    }
}

void StatusHolder::refresh()
{
    _model.getAsync()->getWalletStatus();
}
