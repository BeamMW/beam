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
#include "../bvm/bvm2.h"
#include "node.h"

namespace Shaders {
#	define HOST_BUILD
#	include "../bvm/Shaders/common.h"
#	include "../bvm/Shaders/l2tst1/contract_l1.h"
} // namespace Shaders

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

	intrusive::list_autoclear<Event::Data> m_lstMaturing;

	struct BbsReceiver
		:public proto::FlyClient::IBbsReceiver
	{
		void OnMsg(proto::BbsMsg&&) override;
		IMPLEMENT_GET_PARENT_OBJ(Extractor, m_BbsRcv)
	} m_BbsRcv;

	Extractor(EventsExtractorForeign& x) :m_This(x) {}

	struct Scope
	{
		struct Trigger
		{
			EventsExtractorForeign& m_This;
			bool m_Do;

			Trigger(EventsExtractorForeign& x, bool bDo)
				:m_This(x)
				,m_Do(bDo)
			{
			}

			~Trigger()
			{
				if (m_Do)
					m_This.m_pEvtData->post();
			}
		};

		Trigger m_Trigger;
		std::scoped_lock<std::mutex> m_Lock;

		Scope(EventsExtractorForeign& x)
			:m_Trigger(x, x.m_lstReady.empty())
			,m_Lock(x.m_MutexRcv)
		{
		}

		~Scope()
		{
			m_Trigger.m_Do = m_Trigger.m_Do && !m_Trigger.m_This.m_lstReady.empty();
		}
	};

	struct BbsOutRequest
		:public proto::FlyClient::RequestBbsMsg
	{
		struct Handler
			:public proto::FlyClient::Request::IHandler
		{
			void OnComplete(Request&) override
			{
				// ignore
			}
		} m_Handler;
	};

	void CheckMaturing();
	void Subscribe(BbsChannel);
	void OnSendBbs();

	// EventsExtractor2
	void OnNewTip() override;
	void OnEvent2(const HeightPos& pos, ByteBuffer&& val) override;
	void OnRolledBack() override;
	Kind get_EventKind(const Blob& val) override;

};

void EventsExtractorForeign::Extractor::CheckMaturing()
{
	if (m_lstMaturing.empty())
		return;

	Height h = get_Height();
	if (m_lstMaturing.front().m_Pos.m_Height + m_hDelay > h)
		return;

	Scope scope(m_This);

	while (!m_lstMaturing.empty())
	{
		auto& x = m_lstMaturing.front();
		if (x.m_Pos.m_Height + m_hDelay > h)
			break;

		m_lstMaturing.pop_front();
		m_This.m_lstReady.push_back(x);
	}
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
		auto pEvt = std::make_unique<Event::Rollback>();
		pEvt->m_Height = h;

		Scope scope(m_This);
		m_This.m_lstReady.push_back(*pEvt.release());
	}

	m_This.m_pEvtData->post();
}

EventsExtractor2::Kind EventsExtractorForeign::Extractor::get_EventKind(const Blob& evt)
{
	return m_This.get_EventKind(evt);
}

void EventsExtractorForeign::Extractor::BbsReceiver::OnMsg(proto::BbsMsg&& msg)
{
	assert(get_ParentObj().m_This.m_pSkBbs);

	Blob msgBody = msg.m_Message;
	if (proto::Bbs::Decrypt((uint8_t*&) msgBody.p, msgBody.n, *get_ParentObj().m_This.m_pSkBbs))
	{
		auto pEvt = std::make_unique<Event::BbsMsg>();
		msgBody.Export(pEvt->m_Msg);

		Scope scope(get_ParentObj().m_This);
		get_ParentObj().m_This.m_lstReady.push_back(*std::move(pEvt.release()));
	}
}

void EventsExtractorForeign::Extractor::Subscribe(BbsChannel channel)
{
	if (m_This.m_pSkBbs)
		m_Network.BbsSubscribe(channel, getTimestamp() - 600, &m_BbsRcv);
}

void EventsExtractorForeign::Extractor::OnSendBbs()
{
	BbsOut::List lst;
	{
		std::scoped_lock<std::mutex> scope(m_This.m_MutexSnd);
		lst.swap(m_This.m_lstBbsOut);
	}

	while (!lst.empty())
	{
		auto& x = lst.front();

		boost::intrusive_ptr<BbsOutRequest> pReq(new BbsOutRequest);
		pReq->m_Msg.m_Message = std::move(x.m_Msg);
		pReq->m_Msg.m_Channel = x.m_Channel;

		lst.Delete(x);

		m_Network.PostRequest(*pReq, pReq->m_Handler);
	}
}

void EventsExtractorForeign::RunThreadInternal(Params&& pars, io::Reactor::Ptr&& pReactor)
{
	io::Reactor::Scope scopeReactor(*pReactor);
	Rules::Scope scopeRules(pars.m_Rules);

	Extractor extr(*this);
	m_pCtx = &extr;

	extr.m_Key = std::move(pars.m_Key);
	extr.m_posLast = pars.m_Pos0;
	extr.m_hDelay = pars.m_hDelay;
	extr.m_dh = pars.m_hDelay / 2;

	extr.Init(pars.m_Addr);
	extr.Subscribe(pars.m_Channel);

	pReactor->run();

}

void EventsExtractorForeign::OnDataInternal()
{
	Event::List lst;

	{
		std::scoped_lock<std::mutex> scope(m_MutexRcv);
		lst.swap(m_lstReady);
	}

	while (!lst.empty())
	{
		std::unique_ptr<Event::Base> pEvt(&lst.front());
		lst.pop_front();

		OnEvent(std::move(*pEvt));
	}
}

void EventsExtractorForeign::Stop()
{
	if (m_pEvtStop)
	{
		if (m_Thread.joinable())
		{
			assert(m_pEvtStop);
			m_pEvtStop->post();

			m_Thread.join();
		}

		m_pEvtData.reset();
		m_pEvtStop.reset();

		m_lstReady.Clear();

		m_lstBbsOut.Clear();
	}
}

void EventsExtractorForeign::Start(Params&& pars)
{
	Stop();

	m_pSkBbs = std::move(pars.m_pSkBbs);

	auto pReactor = io::Reactor::create();
	m_pEvtData = io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { OnDataInternal(); });
	m_pEvtStop = io::AsyncEvent::create(*pReactor, []() { io::Reactor::get_Current().stop(); });
	m_pEvtBbs = io::AsyncEvent::create(*pReactor, [this]() { m_pCtx->OnSendBbs(); });

	m_Thread = MyThread(&EventsExtractorForeign::RunThreadInternal, this, std::move(pars), std::move(pReactor));
}

void EventsExtractorForeign::SendBbs(const Blob& msg, BbsChannel channel, const PeerID& pid)
{
	if (m_pSkBbs)
	{
		ECC::Scalar::Native nonce;
		nonce.GenRandomNnz();

		ByteBuffer res;
		if (proto::Bbs::Encrypt(res, pid, nonce, msg.p, msg.n))
		{
			auto pNode = std::make_unique<BbsOut>();
			pNode->m_Channel = channel;
			pNode->m_Msg = std::move(res);

			{
				std::scoped_lock<std::mutex> scope(m_MutexSnd);
				m_lstBbsOut.push_back(*pNode.release());
			}

			m_pEvtBbs->post();

		}
	}
}

/////////////////////////////////
// L2Bridge
void L2Bridge::Init(Params&& pars)
{
	Extractor::Params p1;
	p1.m_Rules = std::move(pars.m_Rules);
	p1.m_hDelay = pars.m_hDelay;
	p1.m_Addr = std::move(pars.m_Addr);

	p1.m_Pos0 = m_Node.get_Processor().get_DB().BridgeGetLastPos();
	p1.m_Pos0.m_Height++;
	p1.m_Pos0.m_Pos = 0;

	p1.m_pSkBbs = nullptr;
	p1.m_Channel = 0;

	{
		Shaders::Env::Key_T<uint8_t> key;
		key.m_KeyInContract = Shaders::L2Tst1_L1::Tags::s_BridgeExp;
		key.m_Prefix.m_Cid = pars.m_cidBridgeL1;
		key.m_Prefix.m_Tag = Shaders::KeyTag::Internal;

		Blob(&key, sizeof(key)).Export(p1.m_Key);
	}

	m_cidBridgeL1 = pars.m_cidBridgeL1;

	const Rules& r = Rules::get(); // our (L2) rules
	m_iWhiteValidator = std::numeric_limits<uint32_t>::max();
	uint32_t nWhite = 0;
	for (const auto& v : r.m_Pbft.m_vE)
	{
		if (!v.m_White)
			continue;

		if (v.m_Addr == m_Node.m_Keys.m_Validator.m_Addr)
		{
			m_iWhiteValidator = nWhite;

			p1.m_pSkBbs = &m_Node.m_Keys.m_Validator.m_sk;
			p1.m_Channel = ChannelFromPeerID(m_Node.m_Keys.m_Validator.m_Addr);

			break;
		}

		nWhite++;
	}
	m_Extractor.Start(std::move(p1));

}

L2Bridge::~L2Bridge()
{
	m_Extractor.Stop();
	ShrinkMru(0);
}

BbsChannel L2Bridge::ChannelFromPeerID(const PeerID& pid)
{
	// derive the channel from the address
	BbsChannel ch;
	pid.ExportWord<0>(ch);
	ch %= proto::Bbs::s_MaxWalletChannels;
	return ch;
}

void L2Bridge::ShrinkMru(uint32_t n)
{
	while (m_Mru.size() > n)
		Delete(m_Mru.front().get_ParentObj());
}

void L2Bridge::Delete(Entry& e)
{
	m_mapEntries.erase(Entry::Owner::Map::s_iterator_to(e.m_Owner));
	m_Mru.erase(Entry::Mru::List::s_iterator_to(e.m_Mru));
	delete &e;
}

EventsExtractor2::Kind L2Bridge::Extractor::get_EventKind(const Blob& b)
{
	return (sizeof(Shaders::L2Tst1_L1::Method::BridgeOp) == b.n) ?
		EventsExtractor2::Kind::ProofNeeded :
		EventsExtractor2::Kind::Drop;
}

void L2Bridge::Extractor::OnEvent(Event::Base&& evt)
{
	using namespace Shaders::L2Tst1_L1;

	switch (evt.get_Type())
	{
	case Event::Type::Data:
		{
			auto& d = Cast::Up<Event::Data>(evt);
			assert(sizeof(Method::BridgeOp) == d.m_Event.size());

			const auto& x = *(const Method::BridgeOp*) &d.m_Event.front();

			get_ParentObj().m_Node.get_Processor().BridgeAddInfo(Cast::Up<PeerID>(x.m_pk.m_X), d.m_Pos, x.m_Aid, x.m_Amount);
		}
		break;

	case Event::Type::Rollback:
		{
			auto& d = Cast::Up<Event::Rollback>(evt);
			get_ParentObj().m_Node.get_Processor().get_DB().BridgeDeleteFrom(HeightPos(d.m_Height + 1));
		}
		break;

	case Event::Type::BbsMsg:
		{
			auto& d = Cast::Up<Event::BbsMsg>(evt);
			get_ParentObj().OnMsg(std::move(d.m_Msg));
		}
		break;
	}
}

template <>
void L2Bridge::OnMsgEx(Shaders::L2Tst1_L1::Msg::GetNonce& msg)
{
	if (msg.m_ProtoVer != Shaders::L2Tst1_L1::Msg::s_ProtoVer)
		return;

	Entry* pE = nullptr;
	auto it = m_mapEntries.find(msg.m_pkOwner, Entry::Owner::Comparator());

	if (m_mapEntries.end() == it)
	{
		// check if the bridge burn event exists
		NodeProcessor::ForeignDetailsPacked fdp;
		if (!m_Node.get_Processor().FindExternalAssetEmit(Cast::Up<PeerID>(msg.m_pkOwner.m_X), false, fdp))
			return; // not approved

		ShrinkMru(19);

		pE = new Entry;
		pE->m_Owner.m_Key = msg.m_pkOwner;
		m_mapEntries.insert(pE->m_Owner);

		fdp.m_Aid.Export(pE->m_Aid);
		fdp.m_Amount.Export(pE->m_Amount);
	}
	else
	{
		// reuse this entry
		pE = &it->get_ParentObj();
		m_Mru.erase(Entry::Mru::List::s_iterator_to(pE->m_Mru));
	}

	m_Mru.push_back(pE->m_Mru);

	pE->m_pkBbs = msg.m_pkBbs;
	pE->m_skNonce.GenRandomNnz();

	Shaders::L2Tst1_L1::Msg::Nonce msgOut;
	msgOut.m_iValidator = m_iWhiteValidator;
	msgOut.m_m_Nonce = ECC::Context::get().G * pE->m_skNonce;

	SendOut(Cast::Up<PeerID>(msg.m_pkBbs.m_X), Blob(&msgOut, sizeof(msgOut)));
}

template <>
void L2Bridge::OnMsgEx(Shaders::L2Tst1_L1::Msg::GetSignature& msg)
{
	auto it = m_mapEntries.find(msg.m_pkOwner, Entry::Owner::Comparator());
	if (m_mapEntries.end() == it)
		return;
	auto& x = it->get_ParentObj();

	// re-create the kernel
	TxKernelContractInvoke krn;
	krn.m_Cid = m_cidBridgeL1;

	typedef Shaders::L2Tst1_L1::Method::BridgeImport Method;
	krn.m_iMethod = Method::s_iMethod;

	krn.m_Args.resize(sizeof(Method));
	auto& m = *(Method*) &krn.m_Args.front();

	auto msk = msg.m_nApproveMask;

	m.m_Aid = x.m_Aid;
	m.m_Amount = x.m_Amount;
	m.m_Cookie = msg.m_Cookie;
	m.m_pk = msg.m_pkOwner;
	m.m_ApproveMask = msg.m_nApproveMask;

	krn.m_Height.m_Min = msg.m_hMin;
	krn.m_Height.m_Max = msg.m_hMin + Shaders::L2Tst1_L1::Msg::s_dh;

	TxStats s;
	krn.AddStats(s);

	krn.m_Fee = Transaction::FeeSettings::get(msg.m_hMin).CalculateForBvm(s, msg.m_nCharge);

	krn.m_Commitment = msg.m_Commitment;
	krn.m_Signature.m_NoncePub = msg.m_TotalNonce;

	// calculate our partial signature
	krn.CalculateMsg();

	ECC::Hash::Processor hp;
	krn.Prepare(hp, nullptr);

	const Rules& r = Rules::get(); // our (L2) rules
	for (const auto& v : r.m_Pbft.m_vE)
	{
		if (!v.m_White)
			continue;

		if (1u & msk)
		{
			ECC::Point pk;
			pk.m_X = Cast::Down<ECC::uintBig>(v.m_Addr);
			pk.m_Y = 0;
			hp << pk;
		}

		msk >>= 1;
	}

	if (std::numeric_limits<uint32_t>::max() != m_iWhiteValidator)
	{
		ECC::Hash::Value hv;
		hp
			<< krn.m_Commitment
			>> hv;

		ECC::Oracle oracle;
		krn.m_Signature.Expose(oracle, hv);

		ECC::Scalar::Native e;
		for (uint32_t i = 0; i <= m_iWhiteValidator; i++)
			oracle >> e;

		e = e * m_Node.m_Keys.m_Validator.m_sk;
		x.m_skNonce += e;
	}


	Shaders::L2Tst1_L1::Msg::Signature msgOut;
	msgOut.m_iValidator = m_iWhiteValidator;
	msgOut.m_k = x.m_skNonce;
	SendOut(Cast::Up<PeerID>(x.m_pkBbs.m_X), Blob(&msgOut, sizeof(msgOut)));

	Delete(x);
}

void L2Bridge::OnMsg(ByteBuffer&& buf)
{
	using namespace Shaders::L2Tst1_L1;

	if (buf.size() < sizeof(Msg::Base))
		return;

	auto& base = *(Msg::Base*) &buf.front();
	switch (base.m_OpCode)
	{
#define THE_MACRO(id, name) \
	case id: \
		if (buf.size() >= sizeof(Msg::name)) \
		{ \
			auto& msg = Cast::Up<Msg::name>(base); \
			OnMsgEx(msg); \
		} \
		break;

	L2Tst1_Msgs_ToValidator(THE_MACRO)

#undef THE_MACRO
	}
}

void L2Bridge::SendOut(const PeerID& pid, const Blob& msg)
{
	m_Extractor.SendBbs(msg, ChannelFromPeerID(pid), pid);
}

} // namespace beam
