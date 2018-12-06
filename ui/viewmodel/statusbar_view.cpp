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

#include "statusbar_view.h"
#include "model/app_model.h"
#include "version.h"

StatusbarViewModel::StatusbarViewModel()
    : m_model(*AppModel::getInstance()->getWallet())
    , m_isSyncInProgress(false)
    , m_isOfflineStatus(false)
    , m_isFailedStatus(false)
    , m_nodeSyncProgress(0)
    , m_nodeDone(0)
    , m_nodeTotal(0)
    , m_done(0)
    , m_total(0)

{
    connect(&m_model, SIGNAL(nodeConnectionChanged(bool)),
        SLOT(onNodeConnectionChanged(bool)));

    connect(&m_model, SIGNAL(nodeConnectionFailed()),
        SLOT(onNodeConnectionFailed()));

    connect(&m_model, SIGNAL(onSyncProgressUpdated(int, int)),
        SLOT(onSyncProgressUpdated(int, int)));

    if (AppModel::getInstance()->getSettings().getRunLocalNode())
    {
        connect(&AppModel::getInstance()->getNode(), SIGNAL(syncProgressUpdated(int, int)),
            SLOT(onNodeSyncProgressUpdated(int, int)));
    }
}

bool StatusbarViewModel::getIsFailedStatus() const
{
    return m_isFailedStatus;
}

bool StatusbarViewModel::getIsOfflineStatus() const
{
    return m_isOfflineStatus;
}

bool StatusbarViewModel::getIsSyncInProgress() const
{
    return m_isSyncInProgress;
}

int StatusbarViewModel::getNodeSyncProgress() const
{
    return m_nodeSyncProgress;
}

QString StatusbarViewModel::getBranchName() const
{
    if (BRANCH_NAME.empty())
        return QString();

    return QString::fromStdString(" (" + BRANCH_NAME + ")");
}

QString StatusbarViewModel::getWalletStatusErrorMsg() const
{
    return QString{};
}

void StatusbarViewModel::setIsFailedStatus(bool value)
{
}

void StatusbarViewModel::setIsOfflineStatus(bool value)
{
    if (m_isOfflineStatus != value)
    {
        m_isOfflineStatus = value;
        emit isOfflineStatusChanged();
    }
}

void StatusbarViewModel::setNodeSyncProgress(int value)
{
    if (m_nodeSyncProgress != value)
    {
        m_nodeSyncProgress = value;
        emit nodeSyncProgressChanged();
    }
}

void StatusbarViewModel::setIsSyncInProgress(bool value)
{
    if (m_isSyncInProgress != value)
    {
        m_isSyncInProgress = value;
        emit isSyncInProgressChanged();
    }
}

void StatusbarViewModel::onNodeConnectionChanged(bool isNodeConnected)
{
    if (isNodeConnected && getIsOfflineStatus())
    {
        setIsOfflineStatus(false);
    }
}

void StatusbarViewModel::onNodeConnectionFailed()
{
    setIsOfflineStatus(true);
}

void StatusbarViewModel::onSyncProgressUpdated(int done, int total)
{
    m_done = done;
    m_total = total;
    setIsSyncInProgress(!((m_done + m_nodeDone) == (m_total + m_nodeTotal)));

    emit stateChanged();
}

void StatusbarViewModel::onNodeSyncProgressUpdated(int done, int total)
{
    m_nodeDone = done;
    m_nodeTotal = total;

    if (total > 0)
    {
        setNodeSyncProgress(static_cast<int>(done * 100) / total);
    }

    setIsSyncInProgress(!((m_done + m_nodeDone) == (m_total + m_nodeTotal)));

    if (done == total)
    {
        auto& settings = AppModel::getInstance()->getSettings();
        if (!settings.getLocalNodeSynchronized())
        {
            settings.setLocalNodeSynchronized(true);
            settings.applyChanges();
        }
    }
}