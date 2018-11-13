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
    : //_model(*AppModel::getInstance()->getWallet())
    _progress{0.0}
    , _nodeTotal{0}
    , _nodeDone{0}
    , _total{0}
    , _done{0}
{
  //  connect(&_model, SIGNAL(onSyncProgressUpdated(int, int)),
  //      SLOT(onSyncProgressUpdated(int, int)));

    //connect(&_model, SIGNAL(onRecoverProgressUpdated(int, int, const QString&)),
    //    SLOT(onRestoreProgressUpdated(int, int, const QString&)));

    if (AppModel::getInstance()->getSettings().getRunLocalNode())
    {
        connect(&AppModel::getInstance()->getNode(), SIGNAL(syncProgressUpdated(int, int)),
            SLOT(onNodeSyncProgressUpdated(int, int)));
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
    int total = _nodeTotal + _total;
    int done = _nodeDone + _done;
    if (total > 0)
    {
        double p = static_cast<double>(done) / total;
        setProgress(p);
        if (total == done)
        {
            emit syncCompleted();
        }
    }
}

double RestoreViewModel::getProgress() const
{
    return _progress;
}

void RestoreViewModel::setProgress(double value)
{
    if (value != _progress)
    {
        _progress = value;
        emit progressChanged();
    }
}