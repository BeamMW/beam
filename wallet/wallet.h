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

#include "wallet/wallet_db.h"
#include "wallet/wallet_transaction.h"
#include <deque>
#include "core/proto.h"

namespace beam
{
    struct IWalletObserver : IWalletDbObserver
    {
        virtual void onSyncProgress(int done, int total) = 0;
    };

    struct IWallet
		: public proto::FlyClient
    {
        using Ptr = std::shared_ptr<IWallet>;
        virtual ~IWallet() {}

        virtual void subscribe(IWalletObserver* observer) = 0;
        virtual void unsubscribe(IWalletObserver* observer) = 0;

        virtual void cancel_tx(const TxID& id) = 0;
        virtual void delete_tx(const TxID& id) = 0;

		virtual void OnWalletMsg(const WalletID& peerID, wallet::SetTxParameter&&) = 0;

		// wallet-wallet comm
		struct INetwork
		{
			virtual void Send(const WalletID& peerID, wallet::SetTxParameter&& msg) = 0;
		};
    };


    class Wallet
		: public IWallet
        , public wallet::INegotiatorGateway
    {
        using Callback = std::function<void()>;
    public:
        using TxCompletedAction = std::function<void(const TxID& tx_id)>;

        Wallet(IWalletDB::Ptr walletDB, TxCompletedAction&& action = TxCompletedAction());
        virtual ~Wallet();

		void set_Network(proto::FlyClient::INetwork&, INetwork&);

        TxID transfer_money(const WalletID& from, const WalletID& to, Amount amount, Amount fee = 0, bool sender = true, ByteBuffer&& message = {} );
        TxID swap_coins(const WalletID& from, const WalletID& to, Amount amount, Amount fee, wallet::AtomicSwapCoin swapCoin, Amount swapAmount);
        void resume_tx(const TxDescription& tx);
        void resume_all_tx();

        // IWallet
        void subscribe(IWalletObserver* observer) override;
        void unsubscribe(IWalletObserver* observer) override;
        void cancel_tx(const TxID& txId) override;
        void delete_tx(const TxID& txId) override;
        
    private:
        void on_tx_completed(const TxID& txID) override;

        void confirm_outputs(const std::vector<Coin>&) override;
        void confirm_kernel(const TxID&, const TxKernel&) override;
        bool get_tip(Block::SystemState::Full& state) const override;
        void send_tx_params(const WalletID& peerID, wallet::SetTxParameter&&) override;
        void register_tx(const TxID& txId, Transaction::Ptr) override;

		void OnWalletMsg(const WalletID& peerID, wallet::SetTxParameter&&) override;

		// FlyClient
		void OnNewTip() override;
        void OnTipUnchanged() override;
		void OnRolledBack() override;
		void get_Kdf(Key::IKdf::Ptr&) override;
		Block::SystemState::IHistory& get_History() override;
		void OnOwnedNode(const PeerID&, bool bUp) override;

		struct RequestHandler
			: public proto::FlyClient::Request::IHandler
		{
			virtual void OnComplete(Request&) override;
			IMPLEMENT_GET_PARENT_OBJ(Wallet, m_RequestHandler)
		} m_RequestHandler;

		uint32_t SyncRemains() const;
		void CheckSyncDone();
		void getUtxoProof(const Coin::ID&);
        void report_sync_progress();
        void notifySyncProgress();
        void updateTransaction(const TxID& txID);
        void saveKnownState();
		void RequestUtxoEvents();
		void AbortUtxoEvents();
		void ProcessUtxoEvent(const proto::UtxoEvent&, Height hTip);
		void SetUtxoEventsHeight(Height);
		Height GetUtxoEventsHeight();

        wallet::BaseTransaction::Ptr getTransaction(const WalletID& myID, const wallet::SetTxParameter& msg);
        wallet::BaseTransaction::Ptr constructTransaction(const TxID& id, wallet::TxType type);

    private:

		static const char s_szLastUtxoEvt[];

#define REQUEST_TYPES_Sync(macro) \
		macro(Utxo) \
		macro(Kernel) \
		macro(UtxoEvents)

		struct AllTasks {
#define THE_MACRO(type, msgOut, msgIn) struct type { static const bool b = false; };
			REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO
		};

		struct SyncTasks :public AllTasks {
#define THE_MACRO(type) struct type { static const bool b = true; };
			REQUEST_TYPES_Sync(THE_MACRO)
#undef THE_MACRO
		};

		struct ExtraData :public AllTasks {
			struct Transaction { TxID m_TxID; };
			struct Utxo { Coin::ID m_CoinID; };
			struct Kernel { TxID m_TxID; };
		};

#define THE_MACRO(type, msgOut, msgIn) \
		struct MyRequest##type \
			:public Request##type \
			,public boost::intrusive::set_base_hook<> \
			,public ExtraData::type \
		{ \
			typedef boost::intrusive_ptr<MyRequest##type> Ptr; \
			bool operator < (const MyRequest##type&) const; \
			virtual ~MyRequest##type() {} \
		}; \
		 \
		typedef boost::intrusive::multiset<MyRequest##type> RequestSet##type; \
		RequestSet##type m_Pending##type; \
		 \
		void DeleteReq(MyRequest##type& r) \
		{ \
			m_Pending##type.erase(RequestSet##type::s_iterator_to(r)); \
			r.m_pTrg = NULL; \
			r.Release(); \
		} \
		void OnRequestComplete(MyRequest##type&); \
		 \
		void AddReq(MyRequest##type& x) \
		{ \
			m_Pending##type.insert(x); \
			x.AddRef(); \
		} \
		bool PostReqUnique(MyRequest##type& x) \
		{ \
			if (m_Pending##type.end() != m_Pending##type.find(x)) \
				return false; \
			AddReq(x); \
			m_pNodeNetwork->PostRequest(x, m_RequestHandler); \
			 \
			if (SyncTasks::type::b) \
				m_LastSyncTotal++; \
			return true; \
		}

		REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO

		IWalletDB::Ptr m_WalletDB;
		proto::FlyClient::INetwork* m_pNodeNetwork;
		INetwork* m_pWalletNetwork;
        std::map<TxID, wallet::BaseTransaction::Ptr> m_transactions;
        std::set<wallet::BaseTransaction::Ptr> m_TransactionsToUpdate;
        TxCompletedAction m_tx_completed_action;
		uint32_t m_LastSyncTotal;
		uint32_t m_OwnedNodesOnline;

        std::vector<IWalletObserver*> m_subscribers;
    };
}
