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

#include "bridge.h"

namespace beam {

/////////////////////////////////
// EventsExtractor
EventsExtractor::Handler::~Handler()
{
	if (m_pRequest)
		m_pRequest->m_pTrg = nullptr;
}

void EventsExtractor::Handler::OnComplete(proto::FlyClient::Request& r)
{
	assert(this == r.m_pTrg);
	assert(m_pRequest.get() == &r);
	r.m_pTrg = nullptr;

	get_ParentObj().OnData();
}

bool EventsExtractor::Handler::IsInProgress() const
{
	if (!m_pRequest || !m_pRequest->m_pTrg)
		return false;

	assert(this == m_pRequest->m_pTrg);
	return true;
}

void EventsExtractor::CheckState()
{
	if (m_Handler.IsInProgress())
		return;

	Height h = get_Height();
	if (m_posLast.m_Height + m_dh >= h)
		return;

	// start the request
	if (!m_Handler.m_pRequest)
	{
		m_Handler.m_pRequest.reset(new proto::FlyClient::RequestContractLogs);
		auto& r = *m_Handler.m_pRequest;
		r.m_Msg.m_KeyMin = m_Key;
		r.m_Msg.m_KeyMax = m_Key;
		r.m_Msg.m_PosMax.m_Height = MaxHeight;
	}

	auto& r = *m_Handler.m_pRequest;
	r.m_Msg.m_PosMin = m_posLast;

	// inc
	if (!++r.m_Msg.m_PosMin.m_Pos)
		r.m_Msg.m_PosMin.m_Height++;

	m_Network.PostRequest(r, m_Handler);
	assert(m_Handler.IsInProgress());
}

void EventsExtractor::OnData()
{
	assert(m_Handler.m_pRequest);
	auto& r = *m_Handler.m_pRequest;

	// parse
	if (r.m_Res.m_Result.empty())
		Exc::Test(!r.m_Res.m_bMore);
	else
	{
		proto::ContractLogsReader clr;
		clr.m_Inp = r.m_Res.m_Result;
		clr.m_Pos = r.m_Msg.m_PosMin;

		while (true)
		{
			clr.ReadOnceStrict();

			Exc::Test(m_posLast < clr.m_Pos);

			m_posLast = clr.m_Pos;
			OnEvent(clr.m_Val);

			if (!clr.m_Inp.n)
				break;
		}

		r.m_Res.m_Result.clear(); // for more safety
	}

	if (r.m_Res.m_bMore)
		CheckState(); // maybe ask for more
	else
	{
		m_posLast.m_Height = get_Height();
		m_posLast.m_Pos = std::numeric_limits<uint32_t>::max();
	}
}

void EventsExtractor::OnRollbackInternal()
{
	if (m_Handler.IsInProgress())
	{
		// cancel ongoing fetch
		m_Handler.m_pRequest->m_pTrg = nullptr;
		m_Handler.m_pRequest.reset();
	}

	Height h = get_Height();
	if (m_posLast.m_Height <= h)
		return;

	OnRolledBack();

	if (m_posLast.m_Height >= h)
	{
		m_posLast.m_Height = h;
		m_posLast.m_Pos = 0;
	}
}

Block::SystemState::IHistory& EventsExtractor::FlyClient::get_History()
{
	return m_Hist;
}

void EventsExtractor::FlyClient::OnNewTip()
{
	get_ParentObj().CheckState();
	get_ParentObj().OnNewTip();
}

void EventsExtractor::FlyClient::OnTipUnchanged()
{
	get_ParentObj().CheckState();
}

void EventsExtractor::FlyClient::OnRolledBack()
{
	get_ParentObj().OnRollbackInternal();
	// will be followed by new tip
}


void EventsExtractor::Init(const io::Address& addr)
{
	m_Network.m_Cfg.m_vNodes.push_back(addr);
	m_Network.Connect();
}

/////////////////////////////////
// EventsExtractor2
EventsExtractor2::Handler::~Handler()
{
	while (!m_lstPending.empty())
		Delete(m_lstPending.front());
}

void EventsExtractor2::Handler::Delete(Request& r)
{
	m_lstPending.erase(RequestList::s_iterator_to(r));
	r.m_pTrg = nullptr;
	r.Release();
}

void EventsExtractor2::Handler::OnComplete(proto::FlyClient::Request& r)
{
	assert(this == r.m_pTrg);

	assert(r.get_Type() == proto::FlyClient::Request::Type::ContractLogProof);
	auto& req = Cast::Up<Request>(r);

	Merkle::Hash hv;
	Block::get_HashContractLog(hv, get_ParentObj().m_Key, req.m_Event, req.m_Msg.m_Pos.m_Pos);

	Block::SystemState::Full sTip;
	get_ParentObj().get_Hist().get_Tip(sTip);
	sTip.IsValidProofLog(hv, req.m_Res.m_Proof);

	// confirmed
	r.m_pTrg = nullptr;

	while (!m_lstPending.empty())
	{
		auto& x = m_lstPending.front();
		if (x.m_pTrg)
			break;

		get_ParentObj().OnEvent2(x.m_Msg.m_Pos, std::move(x.m_Event));
		Delete(x);
	}
}

void EventsExtractor2::OnEvent(const Blob& val)
{
	auto kind = get_EventKind(val);
	if (Kind::Drop == kind)
		return;

	if (m_Handler.m_lstPending.empty() && (Kind::Keep == kind))
	{
		ByteBuffer buf;
		val.Export(buf);
		OnEvent2(m_posLast, std::move(buf));
	}
	else
	{
		auto* pReq = new Handler::Request;
		m_Handler.m_lstPending.push_back(*pReq);
		pReq->AddRef();

		val.Export(pReq->m_Event);
		pReq->m_Msg.m_Pos = m_posLast;

		if (Kind::ProofNeeded == kind)
			m_Network.PostRequest(*pReq, m_Handler);
	}
}

void EventsExtractor2::OnRolledBack()
{
	Height h = get_Height();

	while (!m_Handler.m_lstPending.empty())
	{
		auto& x = m_Handler.m_lstPending.back();
		if (x.m_Msg.m_Pos.m_Height <= h)
			break;
		m_Handler.Delete(x);
	}
}


/////////////////////////////////
// EventsExtractorForeign

struct EventsExtractorForeign::Extractor
	:public EventsExtractor2
{
	EventsExtractorForeign& m_This;
	Height m_hDelay;

	EventsExtractor2::EventList m_lstMaturing;

	Extractor(EventsExtractorForeign& x) :m_This(x) {}

	void CheckMaturing();

	// EventsExtractor2
	void OnNewTip() override;
	void OnEvent2(const HeightPos& pos, ByteBuffer&& val) override;
	void OnRolledBack() override;
};

void EventsExtractorForeign::Extractor::CheckMaturing()
{
	if (m_lstMaturing.empty())
		return;

	Height h = get_Height();
	if (m_lstMaturing.front().m_Pos.m_Height + m_hDelay > h)
		return;

	bool bNewData;
	{
		std::scoped_lock<std::mutex> scope(m_This.m_Mutex);
		bNewData = m_This.m_lstReady.empty();

		while (!m_lstMaturing.empty())
		{
			auto& x = m_lstMaturing.front();
			if (x.m_Pos.m_Height + m_hDelay > h)
				break;

			m_lstMaturing.pop_front();
			m_This.m_lstReady.push_back(x);
		}
	}

	if (bNewData)
		m_This.m_pEvtData->post();
}

void EventsExtractorForeign::Extractor::OnNewTip()
{
	CheckMaturing();
}

void EventsExtractorForeign::Extractor::OnEvent2(const HeightPos& pos, ByteBuffer&& val)
{
	auto* pEvt = m_lstMaturing.Create_back();
	pEvt->m_Pos = pos;
	pEvt->m_Event = std::move(val);

	CheckMaturing();
}

void EventsExtractorForeign::Extractor::OnRolledBack()
{
	EventsExtractor2::OnRolledBack();

	auto h = get_Height();

	while (!m_lstMaturing.empty())
	{
		auto& x = m_lstMaturing.back();
		if (x.m_Pos.m_Height >= h)
			return; // no other action needed

		m_lstMaturing.Delete(x);
	}

	{
		std::scoped_lock<std::mutex> scope(m_This.m_Mutex);

		while (!m_This.m_lstReady.empty())
		{
			auto& x = m_This.m_lstReady.back();
			if (x.m_Pos.m_Height >= h)
				return; // no other action needed

			m_lstMaturing.Delete(x);
		}

		auto* pEvt = m_This.m_lstReady.Create_back();
		pEvt->m_Pos.m_Height = h;
		pEvt->m_Pos.m_Pos = std::numeric_limits<uint32_t>::max();
	}

	m_This.m_pEvtData->post();
}

void EventsExtractorForeign::OnEvtStop()
{
	m_pReactor->stop();
}

void EventsExtractorForeign::RunThreadInternal(Params&& pars)
{
	io::Reactor::Scope scopeReactor(*m_pReactor);
	Rules::Scope scopeRules(pars.m_Rules);

	Extractor extr(*this);


	extr.m_Key = std::move(pars.m_Key);
	extr.m_posLast = pars.m_Pos0;
	extr.m_hDelay = pars.m_hDelay;
	extr.m_dh = pars.m_hDelay / 2;

	extr.Init(pars.m_Addr);


	m_pReactor->run();

}

void EventsExtractorForeign::OnDataInternal()
{
	EventsExtractor2::EventList lst;

	{
		std::scoped_lock<std::mutex> scope(m_Mutex);
		lst.swap(m_lstReady);
	}

	while (!lst.empty())
	{
		auto& x = lst.front();

		if ((std::numeric_limits<uint32_t>::max() == x.m_Pos.m_Pos) && x.m_Event.empty())
			OnRolledBack(x.m_Pos.m_Height);
		else
			OnEvent(x.m_Pos, std::move(x.m_Event));

		lst.Delete(x);
	}
}

void EventsExtractorForeign::Stop()
{
	if (m_pReactor)
	{
		if (m_Thread.joinable())
		{
			assert(m_pEvtStop);
			m_pEvtStop->post();

			m_Thread.join();
		}

		m_pEvtData.reset();
		m_pEvtStop.reset();
		m_pReactor.reset();

		m_lstReady.Clear();
	}
}

void EventsExtractorForeign::Start(Params&& pars)
{
	Stop();

	m_pReactor = io::Reactor::create();
	m_pEvtStop = io::AsyncEvent::create(*m_pReactor, [this]() { OnEvtStop(); });
	m_pEvtData = io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { OnDataInternal(); });

	m_Thread = MyThread(&EventsExtractorForeign::RunThreadInternal, this, std::move(pars));
}

} // namespace beam
