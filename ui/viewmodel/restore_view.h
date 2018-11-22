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
#include <QTimer>

#include "model/wallet_model.h"
#include "ui_helpers.h"

class RestoreViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double progress READ getProgress WRITE setProgress NOTIFY progressChanged)
    Q_PROPERTY(QString progressMessage READ getProgressMessage WRITE setProgressMessage NOTIFY progressMessageChanged)

public:

    RestoreViewModel();
    ~RestoreViewModel();

    double getProgress() const;
    void setProgress(double value);
    const QString& getProgressMessage() const;
    void setProgressMessage(const QString& value);

public slots:
    void onSyncProgressUpdated(int done, int total);
    void onNodeSyncProgressUpdated(int done, int total);
    void onUpdateTimer();
    void onNodeConnectionChanged(bool isNodeConnected);
    void onNodeConnectionFailed();
signals:
    void progressChanged();
    void progressMessageChanged();
    void syncCompleted();
private:
    void updateProgress();
    void syncWithNode();
private:
    WalletModel::Ptr m_walletModel;
    double m_progress;
    int m_nodeTotal;
    int m_nodeDone;
    int m_total;
    int m_done;
    bool m_walletConnected;
    bool m_hasLocalNode;
    QString m_progressMessage;
    uint64_t m_estimationUpdateDeltaMs;
    double m_prevProgress;
    uint64_t m_prevUpdateTimeMs;
    QTimer m_updateTimer;
    beamui::Filter m_speedFilter;
    uint64_t m_currentEstimationSec;
    bool m_skipProgress;
};
