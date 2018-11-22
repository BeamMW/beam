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
    : m_walletModel{ AppModel::getInstance()->getWallet() }
    , m_progress{0.0}
    , m_nodeTotal{0}
    , m_nodeDone{0}
    , m_total{0}
    , m_done{0}
    , m_walletConnected{false}
    , m_hasLocalNode{ AppModel::getInstance()->getSettings().getRunLocalNode() }
    , m_estimationUpdateDeltaMs{ 0UL }
    , m_prevProgress{0.0}
    , m_prevUpdateTimeMs{ GetTime_ms() }
    , m_speedFilter{24}
    , m_currentEstimationSec{0}
    , m_skipProgress{false}
{
    connect(AppModel::getInstance()->getWallet().get(), SIGNAL(onSyncProgressUpdated(int, int)),
        SLOT(onSyncProgressUpdated(int, int)));

    if (AppModel::getInstance()->getSettings().getRunLocalNode())
    {
        connect(&AppModel::getInstance()->getNode(), SIGNAL(syncProgressUpdated(int, int)),
            SLOT(onNodeSyncProgressUpdated(int, int)));
    }

    connect(m_walletModel.get(), SIGNAL(nodeConnectionChanged(bool)),
        SLOT(onNodeConnectionChanged(bool)));

    connect(m_walletModel.get(), SIGNAL(nodeConnectionFailed()),
        SLOT(onNodeConnectionFailed()));

    connect(&m_updateTimer, SIGNAL(timeout()), this, SLOT(onUpdateTimer()));

    if (!m_hasLocalNode)
    {
        syncWithNode();
    }

    m_updateTimer.start(1000);
}

RestoreViewModel::~RestoreViewModel()
{
}

void RestoreViewModel::onSyncProgressUpdated(int done, int total)
{
    m_done = done;
    m_total = total;
    if (done == 0 && total == 0)
    {
        m_skipProgress = true;
    }
    updateProgress();
}

void RestoreViewModel::onNodeSyncProgressUpdated(int done, int total)
{
    if (!m_walletConnected) // ignore node progress if wallet is connected
    {
        m_nodeDone = done;
        m_nodeTotal = total;
        if (done == 0 && total == 0)
        {
            m_skipProgress = true;
        }
        updateProgress();
    }
}

void RestoreViewModel::updateProgress()
{
    double nodeSyncProgress = 0.0;
    QString progressMessage = tr("Waiting for node data...");
    if (m_nodeTotal > 0)
    {
        int blocksDiff = m_nodeTotal / 2;
        if (m_nodeDone <= blocksDiff)
        {
            progressMessage = QString::asprintf(tr("Downloading block headers %d/%d").toStdString().c_str(), m_nodeDone, blocksDiff);
        }
        else
        {
            progressMessage = QString::asprintf(tr("Downloading blocks %d/%d").toStdString().c_str(), m_nodeDone - blocksDiff, blocksDiff);
        }
        nodeSyncProgress = static_cast<double>(m_nodeDone) / m_nodeTotal;
    }
    
    if (nodeSyncProgress >= 1.0 && m_walletConnected == false)
    {
        syncWithNode();
    }

    double walletSyncProgress = 0.0;
    if (m_walletConnected && m_total)
    {
        progressMessage = QString::asprintf(tr("Scanning UTXO %d/%d").toStdString().c_str(), m_done, m_total);
        walletSyncProgress = static_cast<double>(m_done) / m_total;
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
    uint64_t timeDelta = currentTime - m_prevUpdateTimeMs;
    m_prevUpdateTimeMs = currentTime;
    m_estimationUpdateDeltaMs += timeDelta;

    if (p > 0)
    {
        if (m_estimationUpdateDeltaMs > 1000) // update estimation ~every  second
        {
            double progressDelta = p - m_prevProgress;
            m_prevProgress = p;

            double speed = progressDelta / m_estimationUpdateDeltaMs;
            m_speedFilter.addSample(speed);

            m_estimationUpdateDeltaMs = 0UL;
            auto currentSpeed = m_speedFilter.getAverage();
            if (currentSpeed > 0.0)
            {
                m_currentEstimationSec = ((1.0 - p) / currentSpeed) / 1000;
            }
        }

        if (m_currentEstimationSec > 0 )
        {
            progressMessage.append(tr(", estimated time:"));

            int hours = m_currentEstimationSec / 3600;
            if (hours > 0)
            {
                progressMessage.append(QString::asprintf(tr(" %d h").toStdString().c_str(), hours));
            }
            int minutes = (m_currentEstimationSec - 3600 * hours) / 60;
            if (minutes > 0)
            {
                progressMessage.append(QString::asprintf(tr(" %d min").toStdString().c_str(), minutes));
            }
            int seconds = m_currentEstimationSec % 60;
            progressMessage.append(QString::asprintf(tr(" %d sec").toStdString().c_str(), seconds));
        }
    }

    setProgressMessage(progressMessage);
    setProgress(p);
    if (p >= 1.0 || m_skipProgress)
    {
        m_updateTimer.stop();
        emit syncCompleted();
    }
}

double RestoreViewModel::getProgress() const
{
    return m_progress;
}

void RestoreViewModel::setProgress(double value)
{
    if (value > m_progress)
    {
        m_progress = value;
        emit progressChanged();
    }
}

const QString& RestoreViewModel::getProgressMessage() const
{
    return m_progressMessage;
}
void RestoreViewModel::setProgressMessage(const QString& value)
{
    if (m_progressMessage != value)
    {
        m_progressMessage = value;
        emit progressMessageChanged();
    }
}

void RestoreViewModel::syncWithNode()
{
    m_walletModel->getAsync()->syncWithNode();
}

void RestoreViewModel::onUpdateTimer()
{
    updateProgress();
}

void RestoreViewModel::onNodeConnectionChanged(bool isNodeConnected)
{
    m_walletConnected = isNodeConnected;
}

void RestoreViewModel::onNodeConnectionFailed()
{
    m_skipProgress = true;
    updateProgress();
}