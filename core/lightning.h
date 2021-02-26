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

#include "negotiator.h"
#include "fly_client.h"
#include "wallet/core/common.h"
#include <boost/intrusive/list.hpp>

namespace beam {
namespace Lightning {

	using namespace Negotiator;

	const Height kDefaultRevisionMaxLifeTime = 1440 * 14;
	const Height kDefaultLockTime = 1440;
	const Height kDefaultPostLockReserve = 1440;
	const Height kDefaultOpenTxDh = 2 * 60;
	const Height kMaxBlackoutTime = 6;

	class Channel
	{
		void CancelRequest();
		void CancelTx();
		void SendTx(const Transaction& tx);
		void SendTxNoSpam(const Transaction& tx, Height h);
		void ConfirmKernel(const Merkle::Hash& hv, Height h);
		void ConfirmMuSig();
		void CreatePunishmentTx();
		void OnInpCoinsChanged();

		struct MuSigLocator;
		struct KernelLocator;

		void OnRequestComplete(MuSigLocator&);
		void OnRequestComplete(KernelLocator&);
		void OnRequestCompleteInSearch(KernelLocator&);
		void OnRequestComplete(proto::FlyClient::RequestTransaction&);

		struct RequestHandler
			:public proto::FlyClient::Request::IHandler
		{
			// proto::FlyClient::Request::IHandler
			virtual void OnComplete(proto::FlyClient::Request&) override;

			IMPLEMENT_GET_PARENT_OBJ(Channel, m_RequestHandler)
		} m_RequestHandler;

		proto::FlyClient::Request::Ptr m_pRequest;
		proto::FlyClient::RequestTransaction::Ptr m_pPendingTx;

		// current negotiation
		struct NegotiationCtx {
			enum Enum {
				Open,
				Update,
				Close, // graceful closing
			};

			Enum m_eType;
			Storage::Map m_Data;

			virtual ~NegotiationCtx() {}
		};

		std::unique_ptr<NegotiationCtx> m_pNegCtx;

		struct NegotiationCtx_Open :public NegotiationCtx {
			ChannelOpen m_Inst;
		};

		struct NegotiationCtx_Update :public NegotiationCtx {
			ChannelUpdate m_Inst;
		};

		struct NegotiationCtx_Close :public NegotiationCtx {
			MultiTx m_Inst;
		};

	public:

		struct DataOpen
		{
			ECC::Point m_Comm0;
			CoinID m_ms0; // my part of msig0
			// opening tx
			HeightRange m_hrLimit;
			Transaction m_txOpen; // Set iff Role==0.
			Merkle::Hash m_hvKernel0; // open tx kernel. To ensure opening confirmed we should query the kernel, not the Comm0, because it may be spent already
			// confirmation
			Height m_hOpened;
			// initial funds
			std::vector<CoinID> m_vInp;
			CoinID m_cidChange;
		};

		struct DataUpdate
			:public ChannelUpdate::Result
			,public boost::intrusive::list_base_hook<>
		{
			CoinID m_msMy; // my part of msigN for my withdrawal
			CoinID m_msPeer; // my part of msigN for peer withdrawal
			CoinID m_Outp; // phase2 output

			enum struct Type : uint32_t
			{
				None, // not ready yet
				TimeLocked, // standard 2-phase withdrawal
				Punishment,
				Direct, // single phase, no time-lock. Used for the graceful channel closure

			} m_Type;

			const Transaction& get_TxPhase2(bool bInitiator) const;
			void get_Phase2ID(Merkle::Hash& hv, bool bInitiator) const;
			void get_Phase1ID(Merkle::Hash& hv, bool bInitiator) const;

			static bool get_KernelIDSafe(Merkle::Hash& hv, const Transaction&);

			void CheckStdType();
		};

		std::unique_ptr<DataOpen> m_pOpen;

		struct UpdateList
			:public boost::intrusive::list<DataUpdate>
		{
			~UpdateList() { Clear(); }
			void Clear();
			void DeleteFront();
			void DeleteBack();

			DataUpdate* get_NextSafe(DataUpdate&) const;
			DataUpdate* get_PrevSafe(DataUpdate&) const;
		};

		UpdateList m_lstUpdates;
		uint32_t m_nRevision = 0;

		struct Params
		{
			Height m_hRevisionMaxLifeTime = kDefaultRevisionMaxLifeTime;
			Height m_hLockTime = kDefaultLockTime; // withdrawal lock. Same for all
			Height m_hPostLockReserve = kDefaultPostLockReserve; // max height diff of 2nd-stage withdrawal, in addition to m_hLockTime. Same for all
			Amount m_Fee = 0; // for all txs
		} m_Params;

		struct State
		{
			// generic (used not only for opening)
			Height m_hTxSentLast = 0;
			Height m_hQueryLast = 0; // the last height of the current unsuccessful query finish.

			enum Enum {
				None = 0,
				Opening0, // negotiating, no-return barrier not crossed yet. Safe to forget
				Opening1, // negotiating, no-return barrier crossed, waiting confirmation
				Opening2, // negoiation is over, waiting for confirmation
				OpenFailed, // no open confirmation up to max height
				Open,
				Updating, // negotiating to create a new update point
				Closing1, // decided to close
				Closing2, // Phase1 close confirmed
				Closed, // Phase2 close confirmed
				Expired
			};

			// termination status
			struct Close
			{
				// set iff confirmed withdrawal msigN
				DataUpdate* m_pPath = nullptr;
				uint32_t m_nRevision = 0;
				bool m_Initiator; // who initiated the withdrawal
				Height m_hPhase1 = 0; // msig0 -> msigN confirmed
				Height m_hPhase2 = 0; // msigN -> outputs confirmed. In case of "unfair" withdrawal (initiated by the malicious peer) the "outputs" are mine only.

			} m_Close;

			bool m_Terminate = false; // we're forcibly closing the channel

		} m_State;

		State::Enum get_State() const;
		bool IsUnfairPeerClosed() const;
		void DiscardLastRevision();


		bool Open(Amount nMy, Amount nOther, Height hOpenTxDh);

		bool Transfer(Amount, bool bCloseGraceful = false);

		void Close(); // initiate non-cooperative closing

		void OnRolledBack();

		void Update(); // w.r.t. blockchain

		void OnPeerData(Storage::Map& dataIn);

		bool IsSafeToForget() const; // returns true if the channel is either closed or couldn't be opened (i.e. no chance), and it's safe w.r.t. max rollback depth.
		void Forget(); // If the channel didn't open - the locked inputs will are unlocked

		bool IsNegotiating() const { return m_pNegCtx != nullptr; }

		virtual ~Channel();
		virtual Height get_Tip() const = 0;
		virtual proto::FlyClient::INetwork& get_Net() = 0;
		virtual void get_Kdf(Key::IKdf::Ptr&) = 0;
		virtual void AllocTxoID(CoinID&) = 0; // Type, Asset and Value are fixed. Should adjust ID and SubIdx
		virtual Amount SelectInputs(std::vector<CoinID>& vInp, Amount valRequired, Asset::ID) { return 0; }
		virtual void SendPeer(Storage::Map&& dataOut) = 0;

		enum struct CoinState {
			Locked,
			Confirmed,
			Spent,
		};

		virtual void OnCoin(const CoinID&, Height, CoinState, bool bReverse) {}

		void OnCoin(const std::vector<CoinID>&, Height, CoinState, bool bReverse);

		virtual DataUpdate* SelectWithdrawalPath(); // By default selects the most recent available withdrawal. Override to try to use outdated (fraudulent) revisions, for testing.
		virtual void OnRevisionOutdated(uint32_t) {} // just informative, override to log/notify
	
	protected:
		virtual bool TransferInternal(Amount nMyNew, uint32_t iRole, Height h, bool bCloseGraceful);
		uint32_t m_iRole = 0;
		bool m_gracefulClose = false;


	private:
		DataUpdate& CreateUpdatePoint(uint32_t iRole, const CoinID& msA, const CoinID& msB, const CoinID& outp);
		bool OpenInternal(uint32_t iRole, Amount nMy, Amount nOther, const HeightRange& hr0);
		void UpdateNegotiator(Storage::Map& dataIn, Storage::Map& dataOut);
		void SendPeerInternal(Storage::Map&);
		void SetWithdrawParams(WithdrawTx::CommonParam&, const Height& h, Height& h1, Height& h2) const;
		void ForgetOutdatedRevisions(Height hTip);
	};



} // namespace Lightning
} // namespace beam
