// Copyright 2019 The Beam Team
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
#include "wallet/bitcoin/client.h"

class SwapCoinClientModel
    : public QObject
    , public beam::bitcoin::Client
{
    Q_OBJECT
public:
    using Ptr = std::shared_ptr<SwapCoinClientModel>;

    SwapCoinClientModel(beam::bitcoin::Client::CreateBridge bridgeCreator,
        std::unique_ptr<beam::bitcoin::SettingsProvider> settingsProvider,
        beam::io::Reactor& reactor);

    double getAvailable();
    beam::bitcoin::Client::Status getStatus() const;
    bool CanModifySettings() const;

signals:
    void gotStatus(beam::bitcoin::Client::Status status);
    void gotBalance(const beam::bitcoin::Client::Balance& balance);
    void gotCanModifySettings(bool canModify);

    void stateChanged();
    void canModifySettingsChanged();
    void balanceChanged();
    void statusChanged();

private:
    void OnStatus(Status status) override;
    void OnBalance(const Client::Balance& balance) override;
    void OnCanModifySettingsChanged(bool canModify) override;

private slots:
    void onTimer();
    void SetBalance(const beam::bitcoin::Client::Balance& balance);
    void SetStatus(beam::bitcoin::Client::Status status);
    void SetCanModifySettings(bool canModify);

private:
    QTimer m_timer;
    Client::Balance m_balance;
    Status m_status = Status::Unknown;
    bool m_canModifySettings = true;
};