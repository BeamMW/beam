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

		struct Event
			:public boost::intrusive::list_base_hook<>
		{
			HeightPos m_Pos;
			ByteBuffer m_Event;
		};

		typedef intrusive::list_autoclear<Event> EventList;
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

		io::Reactor::Ptr m_pReactor; // used in thread
		io::AsyncEvent::Ptr m_pEvtStop;
		io::AsyncEvent::Ptr m_pEvtData;

		std::mutex m_Mutex;
		EventsExtractor::EventList m_lstReady;

	public:

		struct Params
		{
			Rules m_Rules;
			ByteBuffer m_Key;
			HeightPos m_Pos0;
			Height m_hDelay;
			io::Address m_Addr;
		};

	private:

		struct Extractor;

		void OnEvtStop();
		void RunThreadInternal(Params&& pars);
		void OnDataInternal();

	public:

		~EventsExtractorForeign() { Stop(); }

		void Stop();
		void Start(Params&& pars);

		virtual void OnEvent(const HeightPos& pos, ByteBuffer&&) {}
		virtual void OnRolledBack(Height) {}
	};

} // namespace beam
