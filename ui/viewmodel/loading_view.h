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

class LoadingViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double progress READ getProgress WRITE setProgress NOTIFY progressChanged)
    Q_PROPERTY(QString progressMessage READ getProgressMessage WRITE setProgressMessage NOTIFY progressMessageChanged)
    Q_PROPERTY(bool isCreating READ getIsCreating WRITE setIsCreating NOTIFY isCreatingChanged)

public:

    LoadingViewModel();
    ~LoadingViewModel();

    double getProgress() const;
    void setProgress(double value);
    const QString& getProgressMessage() const;
    void setProgressMessage(const QString& value);
    void setIsCreating(bool value);
    bool getIsCreating() const;

    Q_INVOKABLE void resetWallet();

public slots:
    void onSyncProgressUpdated(int done, int total);
    void onNodeSyncProgressUpdated(int done, int total);
    void onNodeConnectionChanged(bool isNodeConnected);
    void onGetWalletError(beam::wallet::ErrorType error);
signals:
    void progressChanged();
    void progressMessageChanged();
    void syncCompleted();
    void walletError(const QString& title, const QString& message);
    void isCreatingChanged();
private:
    void updateProgress();
    void syncWithNode();
private:
    WalletModel& m_walletModel;
    double m_progress;
    int m_nodeTotal;
    int m_nodeDone;
    int m_total;
    int m_done;
    bool m_walletConnected;
    bool m_hasLocalNode;
    QString m_progressMessage;
    bool m_skipProgress;
    bool m_isCreating;
};
