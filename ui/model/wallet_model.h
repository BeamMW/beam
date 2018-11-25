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
#include <QThread>

#include "wallet/wallet.h"
#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"

struct IWalletModelAsync
{
    using Ptr = std::shared_ptr<IWalletModelAsync>;

    virtual void sendMoney(const beam::WalletID& sender, const beam::WalletID& receiver, beam::Amount&& amount, beam::Amount&& fee = 0) = 0;
    virtual void sendMoney(const beam::WalletID& receiver, const std::string& comment, beam::Amount&& amount, beam::Amount&& fee = 0) = 0;
    virtual void syncWithNode() = 0;
    virtual void calcChange(beam::Amount&& amount) = 0;
    virtual void getWalletStatus() = 0;
    virtual void getUtxosStatus() = 0;
    virtual void getAddresses(bool own) = 0;
    virtual void cancelTx(const beam::TxID& id) = 0;
    virtual void deleteTx(const beam::TxID& id) = 0;
    virtual void createNewAddress(beam::WalletAddress&& address, bool bOwn) = 0;
    virtual void generateNewWalletID() = 0;
    virtual void changeCurrentWalletIDs(const beam::WalletID& senderID, const beam::WalletID& receiverID) = 0;

    virtual void deleteAddress(const beam::WalletID& id) = 0;
    virtual void deleteOwnAddress(const beam::WalletID& id) = 0 ;

    virtual void setNodeAddress(const std::string& addr) = 0;

    virtual void changeWalletPassword(const beam::SecString& password) = 0;

    virtual ~IWalletModelAsync() {}
};

struct WalletStatus
{
    beam::Amount available;
    beam::Amount received;
    beam::Amount sent;
    beam::Amount unconfirmed;
    struct
    {
        beam::Timestamp lastTime;
        int done;
        int total;
    } update;

    beam::Block::SystemState::ID stateID;
};

class WalletModel
    : public QThread
    , private beam::IWalletObserver
    , private IWalletModelAsync
{
    Q_OBJECT
public:

    using Ptr = std::shared_ptr<WalletModel>;

    WalletModel(beam::IWalletDB::Ptr walletDB, const std::string& nodeAddr);
    ~WalletModel();

    void run() override;

public:
    
    IWalletModelAsync::Ptr getAsync();
    bool check_receiver_address(const std::string& addr);

signals:
    void onStatus(const WalletStatus& status);
    void onTxStatus(beam::ChangeAction, const std::vector<beam::TxDescription>& items);
    void onTxPeerUpdated(const std::vector<beam::TxPeer>& peers);
    void onSyncProgressUpdated(int done, int total);
    void onChangeCalculated(beam::Amount change);
    void onAllUtxoChanged(const std::vector<beam::Coin>& utxos);
    void onAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses);
    void onGeneratedNewWalletID(const beam::WalletID& walletID);
    void onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID);
    void nodeConnectionChanged(bool isNodeConnected);
    void nodeConnectionFailed();


private:
    void onCoinsChanged() override;
    void onTransactionChanged(beam::ChangeAction action, std::vector<beam::TxDescription>&& items) override;
    void onSystemStateChanged() override;
    void onTxPeerChanged() override;
    void onAddressChanged() override;
    void onSyncProgress(int done, int total) override;

    void sendMoney(const beam::WalletID& sender, const beam::WalletID& receiver, beam::Amount&& amount, beam::Amount&& fee) override;
    void sendMoney(const beam::WalletID& receiver, const std::string& comment, beam::Amount&& amount, beam::Amount&& fee) override;
    void syncWithNode() override;
    void calcChange(beam::Amount&& amount) override;
    void getWalletStatus() override;
    void getUtxosStatus() override;
    void getAddresses(bool own) override;
    void cancelTx(const beam::TxID& id) override;
    void deleteTx(const beam::TxID& id) override;
    void createNewAddress(beam::WalletAddress&& address, bool bOwn) override;
    void changeCurrentWalletIDs(const beam::WalletID& senderID, const beam::WalletID& receiverID) override;
    void generateNewWalletID() override;
    void deleteAddress(const beam::WalletID& id) override;
    void deleteOwnAddress(const beam::WalletID& id) override;
    void setNodeAddress(const std::string& addr) override;
    void changeWalletPassword(const beam::SecString& password) override;

    void onNodeConnectedStatusChanged(bool isNodeConnected);
    void onNodeConnectionFailed();

    void onStatusChanged();
    WalletStatus getStatus() const;
    std::vector<beam::Coin> getUtxos() const;
private:

    beam::IWalletDB::Ptr _walletDB;
    beam::io::Reactor::Ptr _reactor;
    IWalletModelAsync::Ptr _async;
    std::weak_ptr<beam::proto::FlyClient::INetwork> _nnet;
	std::weak_ptr<beam::Wallet::INetwork> _wnet;
    std::weak_ptr<beam::Wallet> _wallet;
    beam::io::Timer::Ptr _logRotateTimer;

    std::string _nodeAddrStr;
};
