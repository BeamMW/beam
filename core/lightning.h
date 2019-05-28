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

namespace beam {
namespace Lightning {

	using namespace Negotiator;

	class Channel
	{
		void CancelRequest();
		void CancelTx();
		void SwapIfRole(IBase& x, Key::IDV*& pA, Key::IDV*& pB);
		void AddUpdatePoint();
		void SendTx(const Transaction& tx);
		void SendTxNoSpam(const Transaction& tx, Height h);
		void ConfirmKernel(const Merkle::Hash& hv, Height h);
		void CreatePunishmentTx();

		struct MuSigLocator;

		void OnRequestComplete(MuSigLocator&);
		void OnRequestComplete(proto::FlyClient::RequestKernel&);
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

	public:

		struct DataOpen
		{
			ECC::Point m_Comm0;
			Key::IDV m_ms0; // my part of msig0
			// opening tx
			HeightRange m_hrLimit;
			Transaction m_txOpen; // Set iff Role==0.
			Merkle::Hash m_hvKernel0; // open tx kernel. To ensure opening confirmed we should query the kernel, not the Comm0, because it may be spent already
			// confirmation
			Height m_hOpened;
		};

		struct DataUpdate
			:public ChannelUpdate::Result
		{
			Key::IDV m_msMy; // my part of msigN for my withdrawal
			Key::IDV m_msPeer; // my part of msigN for peer withdrawal

			const Transaction& get_TxPhase2(bool bInitiator) const;
			void get_Phase2ID(Merkle::Hash& hv, bool bInitiator) const;

			bool IsPhase2UnfairPeerPunish() const;
		};

		std::unique_ptr<DataOpen> m_pOpen;
		std::vector< std::unique_ptr<DataUpdate> > m_vUpdates;

		struct Params
		{
			Height m_hLockTime = 1440; // withdrawal lock. Same for all
			Amount m_Fee = 0; // for all txs
		} m_Params;

		struct State
		{
			// generic (used not only for opening)
			Height m_hTxSentLast = 0;
			Height m_hQueryLast = 0; // the last height of the current unsuccessful query finish.

			enum Enum {
				None,
				Opening0, // negotiating, no-return barrier not crossed yet. Safe to forget
				Opening1, // negotiating, no-return barrier crossed, waiting confirmation (shouldn't happen if the peer is sane)
				Opening2, // waiting for confirmation
				OpenFailed, // no open confirmation up to max height
				Open,
				Closing1, // decided to close
				Closing2, // Phase1 close confirmed
				Closed // Phase2 close confirmed
			};

			// termination status
			struct Close
			{
				// set iff confirmed withdrawal msigN
				size_t m_iPath;
				bool m_Initiator; // who initiated the withdrawal
				Height m_hPhase1 = 0; // msig0 -> msigN confirmed
				Height m_hPhase2 = 0; // msigN -> outputs confirmed. In case of "unfair" withdrawal (initiated by the malicious peer) the "outputs" are mine only.

			} m_Close;

			bool m_Terminate = false; // we're forcibly closing the channel

		} m_State;

		State::Enum get_State() const;
		bool IsUnfairPeerClosed() const;



		bool Open(const std::vector<Key::IDV>& vIn, uint32_t iRole, Amount nMy, Amount nOther, const HeightRange& hr0);

		bool UpdateBalance(Amount nMyNew);

		void Close(); // initiate non-cooperative closing

		void OnRolledBack();

		void UpdateNegotiation(Storage::Map& dataIn, Storage::Map& dataOut);
		void Update(); // w.r.t. blockchain

		virtual ~Channel();
		virtual Height get_Tip() const = 0;
		virtual proto::FlyClient::INetwork& get_Net() = 0;
		virtual void get_Kdf(Key::IKdf::Ptr&) = 0;

		virtual void AllocTxoID(Key::IDV&) = 0; // Type and Value are fixed. Should adjust ID and SubIdx
	};



} // namespace Lightning
} // namespace beam
