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
#include "model/wallet_model.h"

class SwapCoinClientModel
    : public QObject
    , public beam::bitcoin::Client
{
    Q_OBJECT
public:
    using Ptr = std::shared_ptr<SwapCoinClientModel>;

    SwapCoinClientModel(beam::wallet::AtomicSwapCoin swapCoin,
        beam::bitcoin::Client::CreateBridge bridgeCreator,
        std::unique_ptr<beam::bitcoin::SettingsProvider> settingsProvider,
        beam::io::Reactor& reactor);

    double getAvailable();
    double getReceiving();
    double getSending();
    beam::bitcoin::Client::Status getStatus() const;

signals:
    void gotStatus(beam::bitcoin::Client::Status status);
    void gotBalance(const beam::bitcoin::Client::Balance& balance);
    void stateChanged();

private:
    void OnStatus(Status status) override;
    void OnBalance(const Client::Balance& balance) override;
    void RecalculateAmounts();

private slots:
    void onTimer();
    void onTxStatus(beam::wallet::ChangeAction, const std::vector<beam::wallet::TxDescription>&);

private:
    QTimer m_timer;
    Client::Balance m_balance;
    Status m_status = Status::Unknown;
    double m_receiving = 0;
    double m_sending = 0;
    std::weak_ptr<WalletModel> m_walletModel;
    beam::io::Reactor& m_reactor;
    beam::wallet::AtomicSwapCoin m_swapCoin;

    std::map<beam::wallet::TxID, beam::wallet::TxDescription> m_transactions;
};