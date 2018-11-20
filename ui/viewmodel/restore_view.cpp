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

#include "restore_view.h"
#include "model/app_model.h"

using namespace beam;
using namespace std;

RestoreViewModel::RestoreViewModel()
    : _walletModel{ AppModel::getInstance()->getWallet() }
    , _progress{0.0}
    , _nodeTotal{0}
    , _nodeDone{0}
    , _total{0}
    , _done{0}
    , _walletConnected{false}
    , _hasLocalNode{ AppModel::getInstance()->getSettings().getRunLocalNode() }
    , _estimationUpdateDeltaMs{ 0UL }
    , _prevProgress{0.0}
    , _prevUpdateTimeMs{ GetTime_ms() }
    , _startTimeout{30} // sec
    , _creating{false}
    , _speedFilter{24}
    , _currentEstimationSec{0}
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
    if (!_hasLocalNode)
    {
        syncWithNode();
    }

    connect(&_updateTimer, SIGNAL(timeout()), this, SLOT(onUpdateTimer()));
    _updateTimer.start(1000);
}

RestoreViewModel::~RestoreViewModel()
{
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
    if (!_walletConnected) // ignore node progress if wallet is connected
    {
        _nodeDone = done;
        _nodeTotal = total;
        updateProgress();
    }
}

void RestoreViewModel::updateProgress()
{
    double nodeSyncProgress = 0.0;
    QString progressMessage = tr("Waiting for node data...");
    if (_nodeTotal > 0)
    {
        int blocksDiff = _nodeTotal / 2;
        if (_nodeDone <= blocksDiff)
        {
            progressMessage = QString::asprintf(tr("Downloading block headers %d/%d").toStdString().c_str(), _nodeDone, blocksDiff);
        }
        else
        {
            progressMessage = QString::asprintf(tr("Downloading blocks %d/%d").toStdString().c_str(), _nodeDone - blocksDiff, blocksDiff);
        }
        nodeSyncProgress = static_cast<double>(_nodeDone) / _nodeTotal;
    }
    
    if (nodeSyncProgress >= 1.0 && _walletConnected == false)
    {
        syncWithNode();
    }

    double walletSyncProgress = 0.0;
    if (_total)
    {
        progressMessage = QString::asprintf(tr("Scanning UTXO %d/%d").toStdString().c_str(), _done, _total);
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

    auto currentTime = GetTime_ms();
    uint64_t timeDelta = currentTime - _prevUpdateTimeMs;
    _prevUpdateTimeMs = currentTime;
    _estimationUpdateDeltaMs += timeDelta;

    if (p > 0)
    {
        if (_estimationUpdateDeltaMs > 1000) // update estimation ~every  second
        {
            double progressDelta = p - _prevProgress;
            _prevProgress = p;

            double speed = progressDelta / _estimationUpdateDeltaMs;
            _speedFilter.addSample(speed);

            _estimationUpdateDeltaMs = 0UL;
            auto currentSpeed = _speedFilter.getAverage();
            if (currentSpeed > 0.0)
            {
                _currentEstimationSec = ((1.0 - p) / currentSpeed) / 1000;
            }
        }

        if (_currentEstimationSec > 0 )
        {
            progressMessage.append(tr(", estimated time:"));

            int hours = _currentEstimationSec / 3600;
            if (hours > 0)
            {
                progressMessage.append(QString::asprintf(tr(" %d h").toStdString().c_str(), hours));
            }
            int minutes = (_currentEstimationSec - 3600 * hours) / 60;
            if (minutes > 0)
            {
                progressMessage.append(QString::asprintf(tr(" %d min").toStdString().c_str(), minutes));
            }
            int seconds = _currentEstimationSec % 60;
            progressMessage.append(QString::asprintf(tr(" %d sec").toStdString().c_str(), seconds));
        }
    }
    else if  (!_creating)
    {
        --_startTimeout;
        if (_startTimeout == 0)
        {
            progressMessage = tr("Failed to connect to node. Starting offline");
        }
        else if (_startTimeout < 0)
        {
            p = 1.0;
        }
    }

    setProgressMessage(progressMessage);
    setProgress(p);
    if (p >= 1.0)
    {
        _updateTimer.stop();
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

bool RestoreViewModel::getCreating() const
{
    return _creating;
}

void RestoreViewModel::setCreating(bool value)
{
    if (_creating != value)
    {
        _creating = value;
        emit creatingChanged();
    }
}

const QString& RestoreViewModel::getProgressMessage() const
{
    return _progressMessage;
}
void RestoreViewModel::setProgressMessage(const QString& value)
{
    if (_progressMessage != value)
    {
        _progressMessage = value;
        emit progressMessageChanged();
    }
}

void RestoreViewModel::syncWithNode()
{
    _walletConnected = true;
    _walletModel->getAsync()->syncWithNode();
}

void RestoreViewModel::onUpdateTimer()
{
    updateProgress();
}