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
#include "keykeeper/private_key_keeper.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "swaps/swap_offers_board.h"
#endif

#include <thread>
#include <atomic>

namespace beam::wallet
{
    struct WalletStatus
    {
        Amount available = 0;
        Amount receiving = 0;
        Amount receivingIncoming = 0;
        Amount receivingChange = 0;
        Amount sending = 0;
        Amount maturing = 0;

        struct
        {
            Timestamp lastTime;
            int done;
            int total;
        } update;

        Block::SystemState::ID stateID;
    };

    class WalletClient
        : private IWalletObserver
        , private ISwapOffersObserver
        , private IWalletModelAsync
        , private IWalletDB::IRecoveryProgress
        , private IPrivateKeyKeeper::Handler
    {
    public:
        WalletClient(IWalletDB::Ptr walletDB, const std::string& nodeAddr, io::Reactor::Ptr reactor, IPrivateKeyKeeper::Ptr keyKeeper);
        virtual ~WalletClient();

        void start(std::shared_ptr<std::unordered_map<TxType, BaseTransaction::Creator::Ptr>> txCreators = nullptr);

        IWalletModelAsync::Ptr getAsync();
        std::string getNodeAddress() const;
        std::string exportOwnerKey(const beam::SecString& pass) const;
        bool isRunning() const;
        bool isFork1() const;

    protected:
        // Call this before derived class is destructed to ensure
        // that no virtual function calls below will result in purecall
        void stopReactor();

        virtual void onStatus(const WalletStatus& status) = 0;
        virtual void onTxStatus(ChangeAction, const std::vector<TxDescription>& items) = 0;
        virtual void onSyncProgressUpdated(int done, int total) = 0;
        virtual void onChangeCalculated(Amount change) = 0;
        virtual void onAllUtxoChanged(const std::vector<Coin>& utxos) = 0;
        virtual void onAddresses(bool own, const std::vector<WalletAddress>& addresses) = 0;
        virtual void onSwapOffersChanged(ChangeAction action, const std::vector<SwapOffer>& offers) override = 0;
        virtual void onGeneratedNewAddress(const WalletAddress& walletAddr) = 0;
        virtual void onNewAddressFailed() = 0;
        virtual void onChangeCurrentWalletIDs(WalletID senderID, WalletID receiverID) = 0;
        virtual void onNodeConnectionChanged(bool isNodeConnected) = 0;
        virtual void onWalletError(ErrorType error) = 0;
        virtual void FailedToStartWallet() = 0;
        virtual void onSendMoneyVerified() = 0;
        virtual void onCantSendToExpired() = 0;
        virtual void onPaymentProofExported(const TxID& txID, const ByteBuffer& proof) = 0;
        virtual void onCoinsByTx(const std::vector<Coin>& coins) = 0;
        virtual void onAddressChecked(const std::string& addr, bool isValid) = 0;
        virtual void onImportRecoveryProgress(uint64_t done, uint64_t total) = 0;
        virtual void onNoDeviceConnected() = 0;

    private:

        void onCoinsChanged() override;
        void onTransactionChanged(ChangeAction action, const std::vector<TxDescription>& items) override;
        void onSystemStateChanged(const Block::SystemState::ID& stateID) override;
        void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items) override;
        void onSyncProgress(int done, int total) override;

        void sendMoney(const WalletID& receiver, const std::string& comment, Amount&& amount, Amount&& fee) override;
        void sendMoney(const WalletID& sender, const WalletID& receiver, const std::string& comment, Amount&& amount, Amount&& fee) override;
        void startTransaction(TxParameters&& parameters) override;
        void syncWithNode() override;
        void calcChange(Amount&& amount) override;
        void getWalletStatus() override;
        void getTransactions() override;
        void getUtxosStatus() override;
        void getAddresses(bool own) override;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        void getSwapOffers() override;
        void publishSwapOffer(const SwapOffer& offer) override;
#endif
        void cancelTx(const TxID& id) override;
        void deleteTx(const TxID& id) override;
        void getCoinsByTx(const TxID& txId) override;
        void saveAddress(const WalletAddress& address, bool bOwn) override;
        void changeCurrentWalletIDs(const WalletID& senderID, const WalletID& receiverID) override;
        void generateNewAddress() override;
        void deleteAddress(const WalletID& id) override;
        void updateAddress(const WalletID& id, const std::string& name, WalletAddress::ExpirationStatus status) override;
        void setNodeAddress(const std::string& addr) override;
        void changeWalletPassword(const SecString& password) override;
        void getNetworkStatus() override;
        void refresh() override;
        void exportPaymentProof(const TxID& id) override;
        void checkAddress(const std::string& addr) override;
        void importRecovery(const std::string& path) override;

        // implement IWalletDB::IRecoveryProgress
        bool OnProgress(uint64_t done, uint64_t total) override;

        WalletStatus getStatus() const;
        std::vector<Coin> getUtxos() const;

        void nodeConnectionFailed(const proto::NodeConnection::DisconnectReason&);
        void nodeConnectedStatusChanged(bool isNodeConnected);

    private:
        std::shared_ptr<std::thread> m_thread;
        IWalletDB::Ptr m_walletDB;
        io::Reactor::Ptr m_reactor;
        IWalletModelAsync::Ptr m_async;
        std::weak_ptr<proto::FlyClient::INetwork> m_nodeNetwork;
        std::weak_ptr<IWalletMessageEndpoint> m_walletNetwork;
        std::weak_ptr<Wallet> m_wallet;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        std::weak_ptr<SwapOffersBoard> m_offersBulletinBoard;
#endif
        bool m_isConnected;
        boost::optional<ErrorType> m_walletError;
        std::string m_nodeAddrStr;
        IPrivateKeyKeeper::Ptr m_keyKeeper;
    };
}
