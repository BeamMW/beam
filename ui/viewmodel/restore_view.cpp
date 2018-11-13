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

#include <QObject>

#include "restore_view.h"
#include "model/app_model.h"

using namespace beam;
using namespace std;

RestoreViewModel::RestoreViewModel()
    : _progress{0.0}
    , _nodeTotal{0}
    , _nodeDone{0}
    , _total{0}
    , _done{0}
    , _walletConnected{false}
    , _hasLocalNode{ AppModel::getInstance()->getSettings().getRunLocalNode() }
{
    connect(AppModel::getInstance()->getWallet().get(), SIGNAL(onSyncProgressUpdated(int, int)),
        SLOT(onSyncProgressUpdated(int, int)));

    //connect(&_model, SIGNAL(onRecoverProgressUpdated(int, int, const QString&)),
    //    SLOT(onRestoreProgressUpdated(int, int, const QString&)));

    if (AppModel::getInstance()->getSettings().getRunLocalNode())
    {
        connect(&AppModel::getInstance()->getNode(), SIGNAL(syncProgressUpdated(int, int)),
            SLOT(onNodeSyncProgressUpdated(int, int)));
    }
    if (!_hasLocalNode && _walletConnected == false)
    {
        syncWithNode();
    }
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

void RestoreViewModel::onSyncProgressUpdated(int done, int total)
{
    _done = done;
    _total = total;
    updateProgress();
}

void RestoreViewModel::onNodeSyncProgressUpdated(int done, int total)
{
    _nodeDone = done;
    _nodeTotal = total;
    updateProgress();
}

void RestoreViewModel::updateProgress()
{
    double nodeSyncProgress = 0.0;
    if (_nodeTotal > 0)
    {
        nodeSyncProgress = static_cast<double>(_nodeDone) / _nodeTotal;
    }

    if (nodeSyncProgress >= 1.0 && _walletConnected == false)
    {
        WalletModel& wallet = *AppModel::getInstance()->getWallet();
        if (wallet.async)
        {
            _walletConnected = true;
            wallet.async->syncWithNode();
        }
    }

    double walletSyncProgress = 0.0;
    if (_total)
    {
        walletSyncProgress = static_cast<double>(_done) / _total;
    }
    double p = 0.0;
    if (AppModel::getInstance()->getSettings().getRunLocalNode())
    {
        p = nodeSyncProgress * 0.7 + walletSyncProgress * 0.3;
    }
    else
    {
        p = walletSyncProgress;
    }
    
    setProgress(p);
    if (p >= 1.0)
    {
        emit syncCompleted();
    }
}

double RestoreViewModel::getProgress() const
{
    return _progress;
}

void RestoreViewModel::setProgress(double value)
{
    if (value > _progress)
    {
        _progress = value;
        emit progressChanged();
    }
}

void RestoreViewModel::syncWithNode()
{
    WalletModel& wallet = *AppModel::getInstance()->getWallet();
    if (wallet.async)
    {
        _walletConnected = true;
        wallet.async->syncWithNode();
    }
}