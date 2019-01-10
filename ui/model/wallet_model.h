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

#include "wallet/wallet_client.h"

class WalletModel
    : public QObject
    , public WalletClient
{
    Q_OBJECT
public:

    using Ptr = std::shared_ptr<WalletModel>;

    WalletModel(beam::IWalletDB::Ptr walletDB, const std::string& nodeAddr);
    ~WalletModel() override;

    QString GetErrorString(beam::wallet::ErrorType type);

signals:
    void walletStatus(const WalletStatus& status);
    void txStatus(beam::ChangeAction, const std::vector<beam::TxDescription>& items);
    void syncProgressUpdated(int done, int total);
    void changeCalculated(beam::Amount change);
    void allUtxoChanged(const std::vector<beam::Coin>& utxos);
    void adrresses(bool own, const std::vector<beam::WalletAddress>& addresses);
    void generatedNewAddress(const beam::WalletAddress& walletAddr);
    void changeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID);
    void nodeConnectionChanged(bool isNodeConnected);
    void walletError(beam::wallet::ErrorType error);

private:
    void onStatus(const WalletStatus& status) override;
    void onTxStatus(beam::ChangeAction, const std::vector<beam::TxDescription>& items) override;
    void onSyncProgressUpdated(int done, int total) override;
    void onChangeCalculated(beam::Amount change) override;
    void onAllUtxoChanged(const std::vector<beam::Coin>& utxos) override;
    void onAdrresses(bool own, const std::vector<beam::WalletAddress>& addrs) override;
    void onGeneratedNewAddress(const beam::WalletAddress& walletAddr) override;
    void onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID) override;
    void onNodeConnectionChanged(bool isNodeConnected) override;
    void onWalletError(beam::wallet::ErrorType error) override;
    void FailedToStartWallet() override;
};
