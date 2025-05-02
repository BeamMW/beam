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

#include "../utility/containers.h"
#include "../core/fly_client.h"

namespace beam {

	class EventsExtractor
	{
		struct Handler
			:public proto::FlyClient::Request::IHandler
		{
			proto::FlyClient::RequestContractLogs::Ptr m_pRequest;

			~Handler();
			bool IsInProgress() const;

			// proto::FlyClient::Request::IHandler
			void OnComplete(proto::FlyClient::Request& r) override;

			IMPLEMENT_GET_PARENT_OBJ(EventsExtractor, m_Handler)
		} m_Handler;

		struct FlyClient
			:public proto::FlyClient
		{
			Block::SystemState::HistoryMap m_Hist;

			// proto::FlyClient
			Block::SystemState::IHistory& get_History() override;
			void OnNewTip() override;
			void OnTipUnchanged() override;
			void OnRolledBack() override;

			IMPLEMENT_GET_PARENT_OBJ(EventsExtractor, m_FlyClient)
		} m_FlyClient;

		void CheckState();
		void OnData();
		void OnRollbackInternal();


	protected:

		proto::FlyClient::NetworkStd m_Network;
		Block::SystemState::HistoryMap& get_Hist() { return m_FlyClient.m_Hist; }

	public:

		HeightPos m_posLast;
		Height m_dh = 0; // allowed gap between tip and last pos, before we ask for vents
		ByteBuffer m_Key;

		EventsExtractor()
			:m_Network(m_FlyClient)
		{
		}

		Height get_Height()
		{
			Block::SystemState::Full s;
			return m_FlyClient.m_Hist.get_Tip(s) ? s.m_Height : 0;
		}

		virtual void OnNewTip() {}
		virtual void OnEvent(const Blob& val) {}
		virtual void OnRolledBack() {}

		void Init(const io::Address& addr);
	};

	class EventsExtractor2
		:public EventsExtractor
	{
		struct Handler
			:public proto::FlyClient::Request::IHandler
		{
			struct Request
				:public proto::FlyClient::RequestContractLogProof
				,public boost::intrusive::list_base_hook<>
			{
				ByteBuffer m_Event;
				virtual ~Request() {} // automatic
			};

			typedef intrusive::list<Request> RequestList;
			RequestList m_lstPending;

			~Handler();
			void Delete(Request& r);

			// proto::FlyClient::Request::IHandler
			void OnComplete(proto::FlyClient::Request& r) override;

			IMPLEMENT_GET_PARENT_OBJ(EventsExtractor2, m_Handler)
		} m_Handler;

	protected:

		// EventsExtractor
		void OnEvent(const Blob& val) override;
		void OnRolledBack() override;

	public:

		enum struct Kind {
			Drop,
			Keep,
			ProofNeeded,
		};

		virtual Kind get_EventKind(const Blob& val)
		{
			return Kind::ProofNeeded;
		}

		virtual void OnEvent2(const HeightPos&, ByteBuffer&&) {}
	};

	class EventsExtractorForeign
	{
		MyThread m_Thread;

		io::AsyncEvent::Ptr m_pEvtStop;
		io::AsyncEvent::Ptr m_pEvtBbs;
		io::AsyncEvent::Ptr m_pEvtData;

		std::mutex m_MutexRcv;
		std::mutex m_MutexSnd;

	public:

		struct Params
		{
			Rules m_Rules;
			ByteBuffer m_Key;
			HeightPos m_Pos0;
			Height m_hDelay;
			io::Address m_Addr;
			const ECC::Scalar::Native* m_pSkBbs;
			BbsChannel m_Channel = 0;
		};

	private:

		struct Extractor;
		Extractor* m_pCtx;

		void RunThreadInternal(Params&& pars, io::Reactor::Ptr&&);
		void OnDataInternal();

	protected:
		const ECC::Scalar::Native* m_pSkBbs;

	public:

		~EventsExtractorForeign() { Stop(); }

		void Stop();
		void Start(Params&& pars);

		struct Event
		{
			enum struct Type {
				Data,
				Rollback,
				BbsMsg,
			};

			struct Base
				:public boost::intrusive::list_base_hook<>
			{
				virtual Type get_Type() = 0;
				virtual ~Base() {}
			};

			typedef intrusive::list_autoclear<Base> List;

			struct Data
				:public Base
			{
				~Data() override {}
				Type get_Type() override { return Type::Data; }

				HeightPos m_Pos;
				ByteBuffer m_Event;
			};

			struct Rollback
				:public Base
			{
				~Rollback() override {}
				Type get_Type() override { return Type::Rollback; }
				Height m_Height;
			};

			struct BbsMsg
				:public Base
			{
				~BbsMsg() override {}
				Type get_Type() override { return Type::BbsMsg; }
				ByteBuffer m_Msg;
			};

		};

		void SendBbs(const Blob& msg, BbsChannel, const PeerID&);

		virtual void OnEvent(Event::Base&&) {}
		// called in another thread
		virtual EventsExtractor2::Kind get_EventKind(const Blob& val) { return EventsExtractor2::Kind::ProofNeeded; }

	protected:

		Event::List m_lstReady;

		struct BbsOut
			:public boost::intrusive::list_base_hook<>
		{
			BbsChannel m_Channel;
			ByteBuffer m_Msg;

			typedef intrusive::list_autoclear<BbsOut> List;
		};

		BbsOut::List m_lstBbsOut;
	};

	struct Node;

	class L2Bridge
	{
		struct Extractor
			:public EventsExtractorForeign
		{
			void OnEvent(Event::Base&&) override;
			EventsExtractor2::Kind get_EventKind(const Blob&) override;

			IMPLEMENT_GET_PARENT_OBJ(L2Bridge, m_Extractor)
		} m_Extractor;

		Node& m_Node;

		void OnMsg(ByteBuffer&&);

		struct Entry
		{
			struct Owner
				:public intrusive::set_base_hook<ECC::Point>
			{
				typedef intrusive::multiset<Owner> Map;
				IMPLEMENT_GET_PARENT_OBJ(Entry, m_Owner)
			} m_Owner;

			struct Mru
				:public boost::intrusive::list_base_hook<>
			{
				typedef boost::intrusive::list<Mru> List;
				IMPLEMENT_GET_PARENT_OBJ(Entry, m_Mru)
			} m_Mru;

			Asset::ID m_Aid;
			Amount m_Amount;

			ECC::Scalar::Native m_skNonce;
			ECC::Point m_pkBbs;
		};

		Entry::Owner::Map m_mapEntries;
		Entry::Mru::List m_Mru;
		ContractID m_cidBridgeL1;
		uint32_t m_iWhiteValidator;

		void Delete(Entry&);
		void ShrinkMru(uint32_t);
		void SendOut(const PeerID&, const Blob&);

		static BbsChannel ChannelFromPeerID(const PeerID&);

		struct GetNonce;
		struct GetSignature;

		template <typename Msg>
		void OnMsgEx(Msg&);

	public:

		L2Bridge(Node& n) :m_Node(n) {}
		~L2Bridge();

		struct Params
		{
			Rules m_Rules;
			ContractID m_cidBridgeL1;
			Height m_hDelay = 0;
			io::Address m_Addr;
		};

		void Init(Params&&);
	};

} // namespace beam
