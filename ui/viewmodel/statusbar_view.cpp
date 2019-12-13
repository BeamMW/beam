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
    : m_model(*AppModel::getInstance().getWallet())
    , m_isOnline(false)
    , m_isSyncInProgress(false)
    , m_isFailedStatus(false)
    , m_isConnectionTrusted(false)
    , m_nodeSyncProgress(0)
    , m_nodeDone(0)
    , m_nodeTotal(0)
    , m_done(0)
    , m_total(0)
    , m_errorMsg{}

{
    connect(&m_model, SIGNAL(nodeConnectionChanged(bool)),
        SLOT(onNodeConnectionChanged(bool)));

    connect(&m_model, SIGNAL(walletError(beam::wallet::ErrorType)),
        SLOT(onGetWalletError(beam::wallet::ErrorType)));

    connect(&m_model, SIGNAL(syncProgressUpdated(int, int)),
        SLOT(onSyncProgressUpdated(int, int)));

    connect(&AppModel::getInstance().getNode(), SIGNAL(syncProgressUpdated(int, int)),
            SLOT(onNodeSyncProgressUpdated(int, int)));
    
    connect(&AppModel::getInstance().getNode(), SIGNAL(failedToSyncNode(beam::wallet::ErrorType)),
            SLOT(onGetWalletError(beam::wallet::ErrorType)));

    m_model.getAsync()->getNetworkStatus();
}

bool StatusbarViewModel::getIsOnline() const
{
    return m_isOnline;
}

bool StatusbarViewModel::getIsFailedStatus() const
{
    return m_isFailedStatus;
}

bool StatusbarViewModel::getIsSyncInProgress() const
{
    return m_isSyncInProgress;
}

bool StatusbarViewModel::getIsConnectionTrusted() const
{
    return m_isConnectionTrusted;
}

int StatusbarViewModel::getNodeSyncProgress() const
{
    return m_nodeSyncProgress;
}

QString StatusbarViewModel::getBranchName() const
{
#ifdef BEAM_MAINNET
    return QString();
#else
    if (BRANCH_NAME.empty())
        return QString();

    return QString::fromStdString(" (" + BRANCH_NAME + ")");
#endif
}

QString StatusbarViewModel::getWalletStatusErrorMsg() const
{
    return m_errorMsg;
}

void StatusbarViewModel::setIsOnline(bool value)
{
    if (m_isOnline != value)
    {
        m_isOnline = value;
        emit isOnlineChanged();
    }
}

void StatusbarViewModel::setIsFailedStatus(bool value)
{
    if (m_isFailedStatus != value)
    {
        m_isFailedStatus = value;
        emit isFailedStatusChanged();
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

void StatusbarViewModel::setIsConnectionTrusted(bool value)
{
    if (m_isConnectionTrusted != value)
    {
        m_isConnectionTrusted = value;
        emit isConnectionTrustedChanged();
    }
}

void StatusbarViewModel::setWalletStatusErrorMsg(const QString& value)
{
    if (m_errorMsg != value)
    {
        m_errorMsg = value;
        emit statusErrorChanged();
    }
}

void StatusbarViewModel::onNodeConnectionChanged(bool isNodeConnected)
{
    setIsConnectionTrusted(m_model.isConnectionTrusted());

    if (isNodeConnected == getIsOnline())
    {
        return;
    }

    if (isNodeConnected)
    {
        setIsFailedStatus(false);
        setIsOnline(true);
        return;
    }

    setIsOnline(false);

    if (!m_isFailedStatus)
    {
        // Failed status must have arrived already
        //% "Wallet is not connected to the node"
        setWalletStatusErrorMsg(qtTrId("status-bar-view-not-connected"));
        setIsFailedStatus(true);
    }
}

void StatusbarViewModel::onGetWalletError(beam::wallet::ErrorType error)
{
    setIsOnline(false);
    setWalletStatusErrorMsg(m_model.GetErrorString(error));
    setIsFailedStatus(true);
}

void StatusbarViewModel::onSyncProgressUpdated(int done, int total)
{
    m_done = done;
    m_total = total;
    setIsSyncInProgress(!((m_done + m_nodeDone) == (m_total + m_nodeTotal)));
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
}
