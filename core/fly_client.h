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
#include "../utility/containers.h"
#include <boost/intrusive_ptr.hpp>

namespace beam {
namespace proto {

	struct FlyClient
	{
#define REQUEST_TYPES_All(macro) \
		macro(Utxo) \
		macro(Kernel) \
		macro(Kernel2) \
		macro(Events) \
		macro(EnsureSync) \
		macro(Transaction) \
		macro(ShieldedList) \
		macro(ProofShieldedInp) \
		macro(ProofShieldedOutp) \
		macro(BbsMsg) \
		macro(Asset) \
		macro(StateSummary) \
		macro(EnumHdrs) \
		macro(ContractVars) \
		macro(ContractLogs) \
		macro(ContractVar) \
		macro(ContractLogProof) \
		macro(ShieldedOutputsAt) \
		macro(BodyPack) \
		macro(Body)


#define REQUEST_TYPES_Std(macro) \
        macro(Utxo,              GetProofUtxo,         ProofUtxo) \
        macro(Kernel,            GetProofKernel,       ProofKernel) \
        macro(Asset,             GetProofAsset,        ProofAsset) \
        macro(Kernel2,           GetProofKernel2,      ProofKernel2) \
        macro(Events,            GetEvents,            Events) \
        macro(Transaction,       NewTransaction,       Status) \
        macro(ShieldedList,      GetShieldedList,      ShieldedList) \
        macro(ProofShieldedInp,  GetProofShieldedInp,  ProofShieldedInp) \
        macro(ProofShieldedOutp, GetProofShieldedOutp, ProofShieldedOutp) \
        macro(StateSummary,      GetStateSummary,      StateSummary) \
        macro(ContractVar,       GetContractVar,       ContractVar) \
        macro(ContractLogProof,  GetContractLogProof,  ContractLogProof) \
        macro(ShieldedOutputsAt, GetShieldedOutputsAt, ShieldedOutputsAt) \
        macro(BodyPack,          GetBodyPack,          BodyPack) \
        macro(Body,              GetBodyPack,          Body)


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
#define THE_MACRO(type) type,
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

			template <typename T>
			T& As()
			{
				if (get_Type() != T::s_Type)
					proto::NodeConnection::ThrowUnexpected();

				return Cast::Up<T>(*this);
			}
		};

		struct Data
		{
			struct Std {};

#define THE_MACRO(type, msgOut, msgIn) \
			struct type :public Std { \
				proto::msgOut m_Msg; \
				proto::msgIn m_Res; \
			};
			REQUEST_TYPES_Std(THE_MACRO)
#undef THE_MACRO

			struct BbsMsg {
				proto::BbsMsg m_Msg;
			};

			struct DecodedHdrPack {
				std::vector<Block::SystemState::Full> m_vStates;
				bool DecodeAndCheck(const HdrPack& msg);
			};
			struct EnumHdrs :public DecodedHdrPack {
				proto::EnumHdrs m_Msg;
			};
			struct ContractVars :public Std {
				std::unique_ptr<Merkle::Hash> m_pCtx;
				proto::ContractVarsEnum m_Msg;
				proto::ContractVars m_Res;
			};
			struct ContractLogs :public Std {
				std::unique_ptr<Merkle::Hash> m_pCtx;
				proto::ContractLogsEnum m_Msg;
				proto::ContractLogs m_Res;
			};
			struct EnsureSync {
				bool m_IsDependent;
			};
		};

#define THE_MACRO(type) \
		struct Request##type \
			:public Request \
			,public Data::type \
		{ \
			typedef boost::intrusive_ptr<Request##type> Ptr; \
			virtual ~Request##type() {} \
			static const Type s_Type = Type::type; \
			virtual Type get_Type() const { return s_Type; } \
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
		virtual void OnEventsSerif(const ECC::Hash::Value&, Height) {}
		virtual void OnNewPeer(const PeerID& id, io::Address address) {}
		virtual void OnDependentStateChanged() {}

		struct IBbsReceiver
		{
			virtual void OnMsg(proto::BbsMsg&&) = 0;
		};

		struct INetwork
		{
			using Ptr = std::shared_ptr<INetwork>;
			virtual ~INetwork() {}

			virtual void Connect() = 0;
			virtual void Disconnect() = 0;
			virtual void PostRequestInternal(Request&) = 0;
			virtual void BbsSubscribe(BbsChannel, Timestamp, IBbsReceiver*) {} // duplicates should be handled internally
			virtual const Merkle::Hash* get_DependentState(uint32_t& nCount) { nCount = 0; return nullptr; }

			void PostRequest(Request&, Request::IHandler&);
		};

		struct NetworkStd
			:public INetwork
		{
			using Ptr = std::shared_ptr<NetworkStd>;
			FlyClient& m_Client;

			NetworkStd(FlyClient& fc) :m_Client(fc) {}
			virtual ~NetworkStd();

			struct RequestNode
				:public boost::intrusive::list_base_hook<>
			{
				Request::Ptr m_pRequest;
			};

			struct RequestList
				:public intrusive::list_autoclear<RequestNode>
			{
				void Finish(RequestNode& n);
			};
			
			RequestList m_lst; // idle
			void OnNewRequests();

			struct Config {
				std::vector<io::Address> m_vNodes;
				uint32_t m_PollPeriod_ms = 0; // set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks
				uint32_t m_ReconnectTimeout_ms = 5000;
                uint32_t m_CloseConnectionDelay_ms = 1000;
				bool m_UseProxy = false;
				io::Address m_ProxyAddr;
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

				struct StateArray;

				bool ShouldSync() const;
				void StartSync();
				void SearchBelow(Height, uint32_t nCount);
				void RequestChainworkProof();
				void PostChainworkProof(const StateArray&, Height hLowHeight);
				void PrioritizeSelf();
				RequestNode& get_FirstRequest();
				void OnDone(RequestNode&, bool bMaybeRetry = true);

				io::Timer::Ptr m_pTimer;
				void OnTimer();
				void SetTimer(uint32_t);
				void KillTimer();

				void ResetInternal();
				void ResetVars();

			public:
				NetworkStd& m_This;

				Connection(NetworkStd& x);
				virtual ~Connection();

				void ResetAll();

				io::Address m_Addr;
				PeerID m_NodeID;

				struct DependentContext
				{
					std::unique_ptr<Merkle::Hash> m_pQuery; // affects remote querying
					std::vector<Merkle::Hash> m_vec;
				} m_Dependent;

				// most recent tip of the Node, according to which all the proofs are interpreted
				Block::SystemState::Full m_Tip;

				RequestList m_lst; // in progress
				void AssignRequests();
				void AssignRequest(RequestNode&);

				bool IsAtTip() const;
				uint8_t m_Flags;

				struct Flags {
					static const uint8_t Node = 1;
					static const uint8_t Owned = 2;
					static const uint8_t ReportedConnected = 4;
				};

				// NodeConnection
				void OnConnectedSecure() override;
				void OnLogin(Login&&, uint32_t nFlagsPrev) override;
				void SetupLogin(Login&) override;
				void OnDisconnect(const DisconnectReason&) override;
				void OnMsg(proto::Authentication&& msg) override;
				void OnMsg(proto::GetBlockFinalization&& msg) override;
				void OnMsg(proto::NewTip&& msg) override;
				void OnMsg(proto::ProofCommonState&& msg) override;
				void OnMsg(proto::ProofChainWork&& msg) override;
				void OnMsg(proto::BbsMsg&& msg) override;
				void OnMsg(proto::EventsSerif&& msg) override;
				void OnMsg(proto::DataMissing&& msg) override;
				void OnMsg(proto::PeerInfo&& msg) override;
				void OnMsg(proto::DependentContextChanged&& msg) override;

#define THE_MACRO(type) bool SendRequest(Request##type&);
				REQUEST_TYPES_All(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(type, msgOut, msgIn) void OnMsg(proto::msgIn&&) override;
				REQUEST_TYPES_Std(THE_MACRO)
#undef THE_MACRO

				void OnMsg(proto::Pong&& msg) override;
				void OnMsg(proto::HdrPack&& msg) override;
				void OnMsg(proto::ContractVars&& msg) override;
				void OnMsg(proto::ContractLogs&& msg) override;

				bool IsSupported(const Data::Std&) { return true; }
				bool IsSupported(RequestEvents&);
				bool IsSupported(RequestTransaction&);

				void OnRequestData(const Data::Std&) {}
				void OnRequestData(RequestUtxo&);
				void OnRequestData(RequestKernel&);
				void OnRequestData(RequestKernel2&);
				void OnRequestData(RequestAsset&);
				void OnRequestData(RequestProofShieldedInp&);
				void OnRequestData(RequestProofShieldedOutp&);
				void OnRequestData(RequestContractVar&);

				bool SendTrgCtx(const std::unique_ptr<Merkle::Hash>&);
			};

			typedef boost::intrusive::list<Connection> ConnectionList;
			ConnectionList m_Connections;

			Connection* get_ActiveConnection();

			typedef std::map<BbsChannel, std::pair<IBbsReceiver*, Timestamp> > BbsSubscriptions;
			BbsSubscriptions m_BbsSubscriptions;

			// INetwork
			virtual void Connect() override;
			virtual void Disconnect() override;
			virtual void PostRequestInternal(Request&) override;
			virtual void BbsSubscribe(BbsChannel, Timestamp, IBbsReceiver*) override;
			virtual const Merkle::Hash* get_DependentState(uint32_t& nCount) override;

			// more events
			virtual void OnNodeConnected(bool) {}
			virtual void OnConnectionFailed(const NodeConnection::DisconnectReason&) {}
			virtual void OnLoginSetup(Login& msg) {}
		};
	};

} // namespace proto
} // namespace beam
