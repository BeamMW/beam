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

class RestoreViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double progress READ getProgress WRITE setProgress NOTIFY progressChanged)

public:

    RestoreViewModel();
    ~RestoreViewModel();

    double getProgress() const;
    void setProgress(double value);

    Q_INVOKABLE void restoreFromBlockchain();

public slots:
    void onRestoreProgressUpdated(int, int, const QString&);
    void onSyncProgressUpdated(int done, int total);
    void onNodeSyncProgressUpdated(int done, int total);
signals:
    void progressChanged();
    void syncCompleted();
private:
    void updateProgress();
    void syncWithNode();
private:
    double _progress;
    int _nodeTotal;
    int _nodeDone;
    int _total;
    int _done;
    bool _walletConnected;
    bool _hasLocalNode;
};
