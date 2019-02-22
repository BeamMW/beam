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

#include "common.h"
#include "wallet.h"
#include "wallet_db.h"
#include "wallet_network.h"
#include "wallet_model_async.h"

#include <thread>
#include <atomic>

struct WalletStatus
{
    beam::Amount available = 0;
    beam::Amount receiving = 0;
    beam::Amount sending = 0;
    beam::Amount maturing = 0;

    struct
    {
        beam::Timestamp lastTime;
        int done;
        int total;
    } update;

    beam::Block::SystemState::ID stateID;
};

class WalletClient
    : private beam::IWalletObserver
    , private IWalletModelAsync
{
public:
    WalletClient(beam::IWalletDB::Ptr walletDB, const std::string& nodeAddr);
    virtual ~WalletClient();

    void start();
    
    IWalletModelAsync::Ptr getAsync();
    std::string getNodeAddress() const;
    bool isRunning() const;

protected:

    virtual void onStatus(const WalletStatus& status) = 0;
    virtual void onTxStatus(beam::ChangeAction, const std::vector<beam::TxDescription>& items) = 0;
    virtual void onSyncProgressUpdated(int done, int total) = 0;
    virtual void onChangeCalculated(beam::Amount change) = 0;
    virtual void onAllUtxoChanged(const std::vector<beam::Coin>& utxos) = 0;
    virtual void onAddresses(bool own, const std::vector<beam::WalletAddress>& addresses) = 0;
    virtual void onGeneratedNewAddress(const beam::WalletAddress& walletAddr) = 0;
    virtual void onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID) = 0;
    virtual void onNodeConnectionChanged(bool isNodeConnected) = 0;
    virtual void onWalletError(beam::wallet::ErrorType error) = 0;
    virtual void FailedToStartWallet() = 0;
    virtual void onSendMoneyVerified() = 0;
    virtual void onCantSendToExpired() = 0;
    virtual void onPaymentProofExported(const beam::TxID& txID, const beam::ByteBuffer& proof) = 0;

private:

    void onCoinsChanged() override;
    void onTransactionChanged(beam::ChangeAction action, std::vector<beam::TxDescription>&& items) override;
    void onSystemStateChanged() override;
    void onAddressChanged() override;
    void onSyncProgress(int done, int total) override;

    void sendMoney(const beam::WalletID& receiver, const std::string& comment, beam::Amount&& amount, beam::Amount&& fee) override;
    void syncWithNode() override;
    void calcChange(beam::Amount&& amount) override;
    void getWalletStatus() override;
    void getUtxosStatus() override;
    void getAddresses(bool own) override;
    void cancelTx(const beam::TxID& id) override;
    void deleteTx(const beam::TxID& id) override;
    void saveAddress(const beam::WalletAddress& address, bool bOwn) override;
    void changeCurrentWalletIDs(const beam::WalletID& senderID, const beam::WalletID& receiverID) override;
    void generateNewAddress() override;
    void deleteAddress(const beam::WalletID& id) override;
    void saveAddressChanges(const beam::WalletID& id, const std::string& name, bool isNever, bool makeActive, bool makeExpired) override;
    void setNodeAddress(const std::string& addr) override;
    void changeWalletPassword(const beam::SecString& password) override;
    void getNetworkStatus() override;
    void refresh() override;
    void exportPaymentProof(const beam::TxID& id) override;

    WalletStatus getStatus() const;
    std::vector<beam::Coin> getUtxos() const;

    void nodeConnectionFailed(const beam::proto::NodeConnection::DisconnectReason&);
    void nodeConnectedStatusChanged(bool isNodeConnected);

private:
    std::shared_ptr<std::thread> m_thread;
    beam::IWalletDB::Ptr m_walletDB;
    beam::io::Reactor::Ptr m_reactor;
    IWalletModelAsync::Ptr m_async;
    std::weak_ptr<beam::proto::FlyClient::INetwork> m_nodeNetwork;
    std::weak_ptr<beam::IWalletNetwork> m_walletNetwork;
    std::weak_ptr<beam::Wallet> m_wallet;
    bool m_isConnected;
    boost::optional<beam::wallet::ErrorType> m_walletError;

    std::string m_nodeAddrStr;

    std::atomic<bool> m_isRunning;
};