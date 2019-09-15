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

#include "proto.h"
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive_ptr.hpp>

namespace beam {
namespace proto {

	struct FlyClient
	{
#define REQUEST_TYPES_All(macro) \
		macro(Utxo,			GetProofUtxo,		ProofUtxo) \
		macro(Kernel,		GetProofKernel,		ProofKernel) \
		macro(Kernel2,		GetProofKernel2,	ProofKernel2) \
		macro(UtxoEvents,	GetUtxoEvents,		UtxoEvents) \
		macro(Transaction,	NewTransaction,		Status) \
		macro(BbsMsg,		BbsMsg,				Pong)

		class Request
		{
			int m_Refs = 0;
			friend void intrusive_ptr_add_ref(Request* p) { p->AddRef(); }
			friend void intrusive_ptr_release(Request* p) { p->Release(); }
		public:

			typedef boost::intrusive_ptr<Request> Ptr;

			void AddRef() { m_Refs++; }
			void Release() { if (!--m_Refs) delete this; }

			enum Type {
#define THE_MACRO(type, msgOut, msgIn) type,
				REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO
				count
			};

			virtual ~Request() {}
			virtual Type get_Type() const = 0;

			struct IHandler {
				virtual void OnComplete(Request&) = 0;
			};

			IHandler* m_pTrg = NULL; // set to NULL if aborted.
		};

#define THE_MACRO(type, msgOut, msgIn) \
		struct Request##type :public Request { \
			typedef boost::intrusive_ptr<Request##type> Ptr; \
			Request##type() :m_Msg(Zero), m_Res(Zero) {} \
			virtual ~Request##type() {} \
			virtual Type get_Type() const { return Type::type; } \
			proto::msgOut m_Msg; \
			proto::msgIn m_Res; \
		};

		REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO

		virtual ~FlyClient() {}
		virtual void OnNewTip() {} // tip already added
		virtual void OnTipUnchanged() {} // we have connected to node, but the tip has not changed
		virtual void OnRolledBack() {} // reversed states are already removed
		virtual void get_Kdf(Key::IKdf::Ptr&) {} // get the master kdf. Optional
        virtual void get_OwnerKdf(Key::IPKdf::Ptr&) {} // get the owner kdf. Optional
		virtual Block::SystemState::IHistory& get_History() = 0;
		virtual void OnOwnedNode(const PeerID&, bool bUp) {}

		struct IBbsReceiver
		{
			virtual void OnMsg(proto::BbsMsg&&) = 0;
		};

		struct INetwork
		{
			virtual ~INetwork() {}

			virtual void Connect() = 0;
			virtual void Disconnect() = 0;
			virtual void PostRequestInternal(Request&) = 0;
			virtual void BbsSubscribe(BbsChannel, Timestamp, IBbsReceiver*) {} // duplicates should be handled internally

			void PostRequest(Request&, Request::IHandler&);
		};

		struct NetworkStd
			:public INetwork
		{
			FlyClient& m_Client;

			NetworkStd(FlyClient& fc) :m_Client(fc) {}
			virtual ~NetworkStd();

			struct RequestNode
				:public boost::intrusive::list_base_hook<>
			{
				Request::Ptr m_pRequest;
			};

			struct RequestList
				:public boost::intrusive::list<RequestNode>
			{
				void Delete(RequestNode& n);
				void Finish(RequestNode& n);
				void Clear();
				~RequestList() { Clear(); }
			};
			
			RequestList m_lst; // idle
			void OnNewRequests();

			struct Config {
				std::vector<io::Address> m_vNodes;
				uint32_t m_PollPeriod_ms = 0; // set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks
				uint32_t m_ReconnectTimeout_ms = 5000;
                uint32_t m_CloseConnectionDelay_ms = 1000;
			} m_Cfg;

			class Connection
				:public NodeConnection
				,public boost::intrusive::list_base_hook<>
			{
				struct SyncCtx
				{
					typedef std::unique_ptr<SyncCtx> Ptr;

					std::vector<Block::SystemState::Full> m_vConfirming;
					Block::SystemState::Full m_Confirmed;
					Block::SystemState::Full m_TipBeforeGap;
					Height m_LowHeight;
				};

				SyncCtx::Ptr m_pSync;

				size_t m_iIndex; // for callbacks only

				struct StateArray;

				bool ShouldSync() const;
				void StartSync();
				void SearchBelow(Height, uint32_t nCount);
				void RequestChainworkProof();
				void PostChainworkProof(const StateArray&, Height hLowHeight);
				void PrioritizeSelf();
				Request& get_FirstRequestStrict(Request::Type);
				void OnFirstRequestDone(bool bStillSupported);

				io::Timer::Ptr m_pTimer;
				void OnTimer();
				void SetTimer(uint32_t);
				void KillTimer();

				void ResetInternal();
				void ResetVars();

			public:
				NetworkStd& m_This;

				Connection(NetworkStd& x, size_t iIndex);
				virtual ~Connection();

				void ResetAll();

				io::Address m_Addr;
				PeerID m_NodeID;

				// most recent tip of the Node, according to which all the proofs are interpreted
				Block::SystemState::Full m_Tip;

				RequestList m_lst; // in progress
				void AssignRequests();
				void AssignRequest(RequestNode&);

				bool IsAtTip() const;
				uint8_t m_LoginFlags;
				uint8_t m_Flags;

				struct Flags {
					static const uint8_t Node = 1;
					static const uint8_t Owned = 2;
					static const uint8_t ReportedConnected = 4;
				};

				// NodeConnection
				virtual void OnConnectedSecure() override;
				virtual void OnLogin(Login&&) override;
				virtual void SetupLogin(Login&) override;
				virtual void OnDisconnect(const DisconnectReason&) override;
				virtual void OnMsg(proto::Authentication&& msg) override;
				virtual void OnMsg(proto::GetBlockFinalization&& msg) override;
				virtual void OnMsg(proto::NewTip&& msg) override;
				virtual void OnMsg(proto::ProofCommonState&& msg) override;
				virtual void OnMsg(proto::ProofChainWork&& msg) override;
				virtual void OnMsg(proto::BbsMsg&& msg) override;
#define THE_MACRO(type, msgOut, msgIn) \
				virtual void OnMsg(proto::msgIn&&) override; \
				bool IsSupported(Request##type&); \
				void OnRequestData(Request##type&);
				REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO

				template <typename Req> void SendRequest(Req& r) { Send(r.m_Msg); }
				void SendRequest(RequestBbsMsg&);
			};

			typedef boost::intrusive::list<Connection> ConnectionList;
			ConnectionList m_Connections;

			typedef std::map<BbsChannel, std::pair<IBbsReceiver*, Timestamp> > BbsSubscriptions;
			BbsSubscriptions m_BbsSubscriptions;

			// INetwork
			virtual void Connect() override;
			virtual void Disconnect() override;
			virtual void PostRequestInternal(Request&) override;
			virtual void BbsSubscribe(BbsChannel, Timestamp, IBbsReceiver*) override;

			// more events
			virtual void OnNodeConnected(size_t iNodeIdx, bool) {}
			virtual void OnConnectionFailed(size_t iNodeIdx, const NodeConnection::DisconnectReason&) {}
		};
	};

} // namespace proto
} // namespace beam
