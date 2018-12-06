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
#include "model/wallet_model.h"

class StatusbarViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isFailedStatus READ getIsFailedStatus WRITE setIsFailedStatus NOTIFY isFailedStatusChanged)
    Q_PROPERTY(bool isOfflineStatus READ getIsOfflineStatus WRITE setIsOfflineStatus NOTIFY isOfflineStatusChanged)
    Q_PROPERTY(bool isSyncInProgress READ getIsSyncInProgress WRITE setIsSyncInProgress NOTIFY isSyncInProgressChanged)
    Q_PROPERTY(int nodeSyncProgress READ getNodeSyncProgress WRITE setNodeSyncProgress NOTIFY nodeSyncProgressChanged)
    Q_PROPERTY(QString branchName READ getBranchName CONSTANT)
    Q_PROPERTY(QString walletStatusErrorMsg READ getWalletStatusErrorMsg NOTIFY stateChanged)

public:

    StatusbarViewModel();

    bool getIsFailedStatus() const;
    bool getIsOfflineStatus() const;
    bool getIsSyncInProgress() const;
    int getNodeSyncProgress() const;
    QString getBranchName() const;
    QString getWalletStatusErrorMsg() const;

    void setIsFailedStatus(bool value);
    void setIsOfflineStatus(bool value);
    void setIsSyncInProgress(bool value);
    void setNodeSyncProgress(int value);

public slots:

    void onNodeConnectionChanged(bool isNodeConnected);
    void onNodeConnectionFailed();
    void onSyncProgressUpdated(int done, int total);
    void onNodeSyncProgressUpdated(int done, int total);

signals:

    void isFailedStatusChanged();
    void isOfflineStatusChanged();
    void isSyncInProgressChanged();
    void nodeSyncProgressChanged();
    void stateChanged();

private:
    WalletModel& m_model;

    bool m_isSyncInProgress;
    bool m_isOfflineStatus;
    bool m_isFailedStatus;
    int m_nodeSyncProgress;

    int m_nodeDone;
    int m_nodeTotal;
    int m_done;
    int m_total;
};