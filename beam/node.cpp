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

#include "node.h"
#include "../core/serialization_adapters.h"
#include "../core/proto.h"
#include "../core/ecc_native.h"

#include "../p2p/protocol.h"
#include "../p2p/connection.h"

#include "../utility/io/tcpserver.h"
#include "../utility/logger.h"
#include "../utility/logger_checkpoints.h"

namespace beam {

void Node::RefreshCongestions()
{
	for (TaskSet::iterator it = m_setTasks.begin(); m_setTasks.end() != it; it++)
		it->m_bRelevant = false;

	m_Processor.EnumCongestions();

	for (TaskList::iterator it = m_lstTasksUnassigned.begin(); m_lstTasksUnassigned.end() != it; )
	{
		Task& t = *(it++);
		if (!t.m_bRelevant)
			DeleteUnassignedTask(t);
	}
}

void Node::DeleteUnassignedTask(Task& t)
{
	assert(!t.m_pOwner);
	m_lstTasksUnassigned.erase(TaskList::s_iterator_to(t));
	m_setTasks.erase(TaskSet::s_iterator_to(t));
	delete &t;
}

uint32_t Node::WantedTx::get_Timeout_ms()
{
	return get_ParentObj().m_Cfg.m_Timeout.m_GetTx_ms;
}

void Node::WantedTx::OnExpired(const KeyType& key)
{
	proto::GetTransaction msg;
	msg.m_ID = key;

	for (PeerList::iterator it = get_ParentObj().m_lstPeers.begin(); get_ParentObj().m_lstPeers.end() != it; it++)
	{
		Peer& peer = *it;
		if (peer.m_Config.m_SpreadingTransactions)
			peer.Send(msg);
	}
}

void Node::Bbs::CalcMsgKey(NodeDB::WalkerBbs::Data& d)
{
	ECC::Hash::Processor hp;
	hp.Write(d.m_Message.p, d.m_Message.n);
	hp << d.m_Channel >> d.m_Key;
}

uint32_t Node::Bbs::WantedMsg::get_Timeout_ms()
{
	return get_ParentObj().get_ParentObj().m_Cfg.m_Timeout.m_GetBbsMsg_ms;
}

void Node::Bbs::WantedMsg::OnExpired(const KeyType& key)
{
	proto::BbsGetMsg msg;
	msg.m_Key = key;

	for (PeerList::iterator it = get_ParentObj().get_ParentObj().m_lstPeers.begin(); get_ParentObj().get_ParentObj().m_lstPeers.end() != it; it++)
	{
		Peer& peer = *it;
		if (peer.m_Config.m_Bbs)
			peer.Send(msg);
	}

	get_ParentObj().MaybeCleanup();
}

void Node::Wanted::Clear()
{
	while (!m_lst.empty())
		DeleteInternal(m_lst.back());
}

void Node::Wanted::DeleteInternal(Item& n)
{
	m_lst.erase(List::s_iterator_to(n));
	m_set.erase(Set::s_iterator_to(n));
	delete &n;
}

void Node::Wanted::Delete(Item& n)
{
	bool bFront = (&m_lst.front() == &n);
	DeleteInternal(n);

	if (bFront)
		SetTimer();
}

bool Node::Wanted::Delete(const KeyType& key)
{
	Item n;
	n.m_Key = key;

	Set::iterator it = m_set.find(n);
	if (m_set.end() == it)
		return false;

	Delete(*it);
	return true;
}

bool Node::Wanted::Add(const KeyType& key)
{
	Item n;
	n.m_Key = key;
	Set::iterator it = m_set.find(n);
	if (m_set.end() != it)
		return false; // already waiting for it

	bool bEmpty = m_lst.empty();

	Item* p = new Item;
	p->m_Key = key;
	p->m_Advertised_ms = GetTime_ms();

	m_set.insert(*p);
	m_lst.push_back(*p);

	if (bEmpty)
		SetTimer();

	return true;
}

void Node::Wanted::SetTimer()
{
	if (m_lst.empty())
	{
		if (m_pTimer)
			m_pTimer->cancel();
	}
	else
	{
		if (!m_pTimer)
			m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());

		uint32_t dt = GetTime_ms() - m_lst.front().m_Advertised_ms;
		const uint32_t timeout_ms = get_Timeout_ms();

		m_pTimer->start((timeout_ms > dt) ? (timeout_ms - dt) : 0, false, [this]() { OnTimer(); });
	}
}

void Node::Wanted::OnTimer()
{
	uint32_t t_ms = GetTime_ms();
	const uint32_t timeout_ms = get_Timeout_ms();

	while (!m_lst.empty())
	{
		Item& n = m_lst.front();
		if (t_ms - n.m_Advertised_ms < timeout_ms)
			break;

		OnExpired(n.m_Key); // should not invalidate our structure
		Delete(n); // will also reschedule the timer
	}
}

void Node::TryAssignTask(Task& t, const PeerID* pPeerID)
{
	while (true)
	{
		Peer* pSel = NULL;

		if (pPeerID)
		{
			bool bCreate = false;
			PeerMan::PeerInfoPlus* pInfo = (PeerMan::PeerInfoPlus*) m_PeerMan.Find(*pPeerID, bCreate);

			if (pInfo && pInfo->m_pLive && pInfo->m_pLive->m_bPiRcvd)
				pSel = pInfo->m_pLive;
		}

		for (PeerList::iterator it = m_lstPeers.begin(); !pSel && (m_lstPeers.end() != it); it++)
		{
			Peer& p = *it;
			if (ShouldAssignTask(t, p))
			{
				pSel = &p;
				break;
			}
		}

		if (!pSel)
			break;

		try {
			AssignTask(t, *pSel);
			return; // done
		}
		catch (const std::exception& e) {
			pSel->OnExc(e);
		}

		//  retry
	}
}

void Node::AssignTask(Task& t, Peer& p)
{
	if (t.m_Key.second)
	{
		proto::GetBody msg;
		msg.m_ID = t.m_Key.first;
		p.Send(msg);
	}
	else
	{
		proto::GetHdr msg;
		msg.m_ID = t.m_Key.first;
		p.Send(msg);
	}

	bool bEmpty = p.m_lstTasks.empty();

	assert(!t.m_pOwner);
	t.m_pOwner = &p;

	m_lstTasksUnassigned.erase(TaskList::s_iterator_to(t));
	p.m_lstTasks.push_back(t);

	if (bEmpty)
		p.SetTimerWrtFirstTask();
}

void Node::Peer::SetTimerWrtFirstTask()
{
	if (m_lstTasks.empty())
		KillTimer();
	else
		SetTimer(m_lstTasks.front().m_Key.second ? m_This.m_Cfg.m_Timeout.m_GetBlock_ms : m_This.m_Cfg.m_Timeout.m_GetState_ms);
}

bool Node::ShouldAssignTask(Task& t, Peer& p)
{
	if (p.m_TipHeight < t.m_Key.first.m_Height)
		return false;

	// Current design: don't ask anything from non-authenticated peers
	if (!(p.m_bPiRcvd && p.m_pInfo))
		return false;

	// check if the peer currently transfers a block
	for (TaskList::iterator it = p.m_lstTasks.begin(); p.m_lstTasks.end() != it; it++)
		if (it->m_Key.second)
			return false;

	return p.m_setRejected.end() == p.m_setRejected.find(t.m_Key);
}

void Node::Processor::RequestData(const Block::SystemState::ID& id, bool bBlock, const PeerID* pPreferredPeer)
{
	Task tKey;
	tKey.m_Key.first = id;
	tKey.m_Key.second = bBlock;

	TaskSet::iterator it = get_ParentObj().m_setTasks.find(tKey);
	if (get_ParentObj().m_setTasks.end() == it)
	{
		LOG_INFO() << "Requesting " << (bBlock ? "block" : "header") << " " << id;

		Task* pTask = new Task;
		pTask->m_Key = tKey.m_Key;
		pTask->m_bRelevant = true;
		pTask->m_pOwner = NULL;

		get_ParentObj().m_setTasks.insert(*pTask);
		get_ParentObj().m_lstTasksUnassigned.push_back(*pTask);

		get_ParentObj().TryAssignTask(*pTask, pPreferredPeer);

	} else
		it->m_bRelevant = true;
}

void Node::Processor::OnPeerInsane(const PeerID& peerID)
{
	bool bCreate = false;
	PeerMan::PeerInfoPlus* pInfo = (PeerMan::PeerInfoPlus*) get_ParentObj().m_PeerMan.Find(peerID, bCreate);

	if (pInfo)
	{
		if (pInfo->m_pLive)
			pInfo->m_pLive->DeleteSelf(true, proto::NodeConnection::ByeReason::Ban);
		else
			get_ParentObj().m_PeerMan.Ban(*pInfo);
	}
}

void Node::Processor::OnNewState()
{
	if (!m_Cursor.m_Sid.m_Row)
		return;

	proto::Hdr msgHdr;
	msgHdr.m_Description = m_Cursor.m_Full;

	proto::NewTip msg;
	msgHdr.m_Description.get_ID(msg.m_ID);
	msg.m_ChainWork = m_Cursor.m_ChainWork;

	LOG_INFO() << "My Tip: " << msg.m_ID;

	get_ParentObj().m_TxPool.DeleteOutOfBound(msg.m_ID.m_Height + 1);

	get_ParentObj().m_Miner.HardAbortSafe();

	get_ParentObj().m_Miner.SetTimer(0, true); // don't start mined block construction, because we're called in the context of NodeProcessor, which holds the DB transaction.

	for (PeerList::iterator it = get_ParentObj().m_lstPeers.begin(); get_ParentObj().m_lstPeers.end() != it; it++)
	{
		Peer& peer = *it;

		if (peer.m_bConnected && (peer.m_TipWork <= msg.m_ChainWork))
		{
			peer.Send(msg);

			if (peer.m_Config.m_AutoSendHdr)
				peer.Send(msgHdr);
		}
	}

	if (get_ParentObj().m_Compressor.m_bEnabled)
		get_ParentObj().m_Compressor.OnNewState();

	get_ParentObj().RefreshCongestions();
}

void Node::Processor::OnRolledBack()
{
	LOG_INFO() << "Rolled back to: " << m_Cursor.m_ID;

	if (get_ParentObj().m_Compressor.m_bEnabled)
		get_ParentObj().m_Compressor.OnRolledBack();
}

bool Node::Processor::VerifyBlock(const Block::BodyBase& block, TxBase::IReader&& r, const HeightRange& hr)
{
	uint32_t nThreads = get_ParentObj().m_Cfg.m_VerificationThreads;
	if (!nThreads)
	{
		std::unique_ptr<Verifier::MyBatch> p(new Verifier::MyBatch);
		p->m_bEnableBatch = true;
		Verifier::MyBatch::Scope scope(*p);

		return
			NodeProcessor::VerifyBlock(block, std::move(r), hr) &&
			p->Flush();
	}

	Verifier& v = m_Verifier; // alias
	std::unique_lock<std::mutex> scope(v.m_Mutex);

	if (v.m_vThreads.empty())
	{
		v.m_iTask = 1;

		v.m_vThreads.resize(nThreads);
		for (uint32_t i = 0; i < nThreads; i++)
			v.m_vThreads[i] = std::thread(&Verifier::Thread, &v, i);
	}


	v.m_iTask ^= 2;
	v.m_pTx = &block;
	v.m_pR = &r;
	v.m_bFail = false;
	v.m_Remaining = nThreads;
	v.m_Context.m_bBlockMode = true;
	v.m_Context.m_Height = hr;
	v.m_Context.m_nVerifiers = nThreads;

	v.m_TaskNew.notify_all();

	while (v.m_Remaining)
		v.m_TaskFinished.wait(scope);

	return !v.m_bFail && v.m_Context.IsValidBlock(block, m_Cursor.m_SubsidyOpen);
}

void Node::Processor::Verifier::Thread(uint32_t iVerifier)
{
	std::unique_ptr<Verifier::MyBatch> p(new Verifier::MyBatch);
	p->m_bEnableBatch = true;
	Verifier::MyBatch::Scope scope(*p);

	for (uint32_t iTask = 1; ; )
	{
		{
			std::unique_lock<std::mutex> scope(m_Mutex);

			while (m_iTask == iTask)
				m_TaskNew.wait(scope);

			if (!m_iTask)
				return;

			iTask = m_iTask;
		}

		p->Reset();

		assert(m_Remaining);

		TxBase::Context ctx;
		ctx.m_bBlockMode = true;
		ctx.m_Height = m_Context.m_Height;
		ctx.m_nVerifiers = m_Context.m_nVerifiers;
		ctx.m_iVerifier = iVerifier;
		ctx.m_pAbort = &m_bFail; // obsolete actually

		TxBase::IReader::Ptr pR;
		m_pR->Clone(pR);

		bool bValid = ctx.ValidateAndSummarize(*m_pTx, std::move(*pR)) && p->Flush();

		std::unique_lock<std::mutex> scope(m_Mutex);

		verify(m_Remaining--);

		if (bValid && !m_bFail)
			bValid = m_Context.Merge(ctx);

		if (!bValid)
			m_bFail = true;

		if (!m_Remaining)
			m_TaskFinished.notify_one();
	}
}

Node::Peer* Node::AllocPeer(const beam::io::Address& addr)
{
	Peer* pPeer = new Peer(*this);
	m_lstPeers.push_back(*pPeer);

	pPeer->m_pInfo = NULL;
	pPeer->m_bConnected = false;
	pPeer->m_bPiRcvd = false;
	pPeer->m_bOwner = false;
	pPeer->m_Port = 0;
	pPeer->m_TipHeight = 0;
	pPeer->m_TipWork = ECC::Zero;
	pPeer->m_RemoteAddr = addr;
	ZeroObject(pPeer->m_Config);

	LOG_INFO() << "+Peer " << addr;

	return pPeer;
}

void Node::Initialize()
{
	m_Processor.m_Horizon = m_Cfg.m_Horizon;
	m_Processor.Initialize(m_Cfg.m_sPathLocal.c_str());
    m_Processor.m_Kdf.m_Secret = m_Cfg.m_WalletKey;

	ECC::GenRandom(m_SChannelSeed.V.m_pData, sizeof(m_SChannelSeed.V.m_pData));

	m_MyPrivateID.V.m_Value = ECC::Zero;

	NodeDB::Blob blob(m_MyPrivateID.V.m_Value.m_pData, sizeof(m_MyPrivateID.V.m_Value.m_pData));
	bool bNewID = !m_Processor.get_DB().ParamGet(NodeDB::ParamID::MyID, NULL, &blob);

	if (bNewID)
		ECC::Hash::Processor() << "myid" << m_SChannelSeed.V >> m_MyPrivateID.V.m_Value;

	ECC::Scalar::Native sk = m_MyPrivateID.V;
	proto::Sk2Pk(m_MyPublicID, sk);

	if (bNewID)
	{
		m_MyPrivateID.V = sk; // may have been negated
		m_Processor.get_DB().ParamSet(NodeDB::ParamID::MyID, NULL, &blob);
	}

	ECC::Kdf& kdf = m_Processor.m_Kdf;

	DeriveKey(sk, kdf, 0, KeyType::Identity);
	proto::Sk2Pk(m_MyOwnerID, sk);

	LOG_INFO() << "Node ID=" << m_MyPublicID << ", Owner=" << m_MyOwnerID;
	LOG_INFO() << "Initial Tip: " << m_Processor.m_Cursor.m_ID;

	if (m_Cfg.m_VerificationThreads < 0)
	{
		uint32_t numCores = std::thread::hardware_concurrency();
		m_Cfg.m_VerificationThreads = (numCores > m_Cfg.m_MiningThreads + 1) ? (numCores - m_Cfg.m_MiningThreads) : 0;
	}

	RefreshCongestions();

	if (m_Cfg.m_Listen.port())
	{
		m_Server.Listen(m_Cfg.m_Listen);
		if (m_Cfg.m_BeaconPeriod_ms)
			m_Beacon.Start();
	}

	for (uint32_t i = 0; i < m_Cfg.m_Connect.size(); i++)
	{
		PeerID id0;
		id0 = ECC::Zero;
		m_PeerMan.OnPeer(id0, m_Cfg.m_Connect[i], true);
	}

	// peers
	m_PeerMan.m_pTimerUpd = io::Timer::create(io::Reactor::get_Current().shared_from_this());
	m_PeerMan.m_pTimerUpd->start(m_Cfg.m_Timeout.m_PeersUpdate_ms, true, [this]() { m_PeerMan.Update(); });

	m_PeerMan.m_pTimerFlush = io::Timer::create(io::Reactor::get_Current().shared_from_this());
	m_PeerMan.m_pTimerFlush->start(m_Cfg.m_Timeout.m_PeersDbFlush_ms, true, [this]() { m_PeerMan.OnFlush(); });

	{
		NodeDB::WalkerPeer wlk(m_Processor.get_DB());
		for (m_Processor.get_DB().EnumPeers(wlk); wlk.MoveNext(); )
		{
			if (wlk.m_Data.m_ID == m_MyPublicID)
				continue; // could be left from previous run?

			PeerMan::PeerInfo* pPi = m_PeerMan.OnPeer(wlk.m_Data.m_ID, io::Address::from_u64(wlk.m_Data.m_Address), false);
			if (!pPi)
				continue;

			// set rating (akward, TODO - fix this)
			uint32_t r = wlk.m_Data.m_Rating;
			if (!r)
				m_PeerMan.Ban(*pPi);
			else
				if (r > pPi->m_RawRating.m_Value)
					m_PeerMan.ModifyRating(*pPi, r - pPi->m_RawRating.m_Value, true);
				else
					m_PeerMan.ModifyRating(*pPi, pPi->m_RawRating.m_Value - r, false);

			pPi->m_LastSeen = wlk.m_Data.m_LastSeen;
		}
	}

	if (m_Cfg.m_MiningThreads)
	{
		m_Miner.m_pEvtMined = io::AsyncEvent::create(io::Reactor::get_Current().shared_from_this(), [this]() { m_Miner.OnMined(); });

		m_Miner.m_vThreads.resize(m_Cfg.m_MiningThreads);
		for (uint32_t i = 0; i < m_Cfg.m_MiningThreads; i++)
		{
			PerThread& pt = m_Miner.m_vThreads[i];
			pt.m_pReactor = io::Reactor::create();
			pt.m_pEvt = io::AsyncEvent::create(pt.m_pReactor, [this, i]() { m_Miner.OnRefresh(i); });
			pt.m_Thread = std::thread(&io::Reactor::run, pt.m_pReactor);
		}

		m_Miner.SetTimer(0, true); // async start mining, since this method may be followed by ImportMacroblock.
	}

	ZeroObject(m_Compressor.m_hrNew);
	m_Compressor.m_bEnabled = !m_Cfg.m_HistoryCompression.m_sPathOutput.empty();

	if (m_Compressor.m_bEnabled)
		m_Compressor.Init();

	m_Bbs.Cleanup();
}

void Node::Bbs::Cleanup()
{
	get_ParentObj().m_Processor.get_DB().BbsDelOld(getTimestamp() - get_ParentObj().m_Cfg.m_Timeout.m_BbsMessageTimeout_s);
	m_LastCleanup_ms = GetTime_ms();

	FindRecommendedChannel();
}

void Node::Bbs::FindRecommendedChannel()
{
	NodeDB& db = get_ParentObj().m_Processor.get_DB(); // alias

	uint32_t nChannel = 0, nCount = 0, nCountFound;
	bool bFound = false;

	NodeDB::WalkerBbs wlk(db);
	for (db.EnumAllBbs(wlk); ; )
	{
		bool bMoved = wlk.MoveNext();

		if (bMoved && (wlk.m_Data.m_Channel == nChannel))
			nCount++;
		else
		{
			if ((nCount <= get_ParentObj().m_Cfg.m_BbsIdealChannelPopulation) && (!bFound || (nCountFound < nCount)))
			{
				bFound = true;
				nCountFound = nCount;
				m_RecommendedChannel = nChannel;
			}

			if (!bFound && (nChannel + 1 != wlk.m_Data.m_Channel)) // fine also for !bMoved
			{
				bFound = true;
				nCountFound = 0;
				m_RecommendedChannel = nChannel + 1;
			}

			if (!bMoved)
				break;

			nChannel = wlk.m_Data.m_Channel;
			nCount = 1;
		}
	}

	assert(bFound);
}

void Node::Bbs::MaybeCleanup()
{
	uint32_t dt_ms = GetTime_ms() - m_LastCleanup_ms;
	if (dt_ms >= get_ParentObj().m_Cfg.m_Timeout.m_BbsCleanupPeriod_ms)
		Cleanup();
}

void Node::ImportMacroblock(Height h)
{
	if (!m_Compressor.m_bEnabled)
		throw std::runtime_error("History path not specified");

	Block::BodyBase::RW rw;
	m_Compressor.FmtPath(rw, h, NULL);
	if (!rw.Open(true))
		std::ThrowIoError();

	if (!m_Processor.ImportMacroBlock(rw))
		throw std::runtime_error("import failed");

	if (m_Processor.m_Cursor.m_Sid.m_Row)
		m_Processor.get_DB().MacroblockIns(m_Processor.m_Cursor.m_Sid.m_Row);
}

Node::~Node()
{
	LOG_INFO() << "Node stopping...";

	m_Miner.HardAbortSafe();

	for (size_t i = 0; i < m_Miner.m_vThreads.size(); i++)
	{
		PerThread& pt = m_Miner.m_vThreads[i];
		if (pt.m_pReactor)
			pt.m_pReactor->stop();

		if (pt.m_Thread.joinable())
			pt.m_Thread.join();
	}
	m_Miner.m_vThreads.clear();

	m_Compressor.StopCurrent();

	for (PeerList::iterator it = m_lstPeers.begin(); m_lstPeers.end() != it; it++)
		ZeroObject(it->m_Config); // prevent re-assigning of tasks in the next loop

	while (!m_lstPeers.empty())
		m_lstPeers.front().DeleteSelf(false, proto::NodeConnection::ByeReason::Stopping);

	while (!m_lstTasksUnassigned.empty())
		DeleteUnassignedTask(m_lstTasksUnassigned.front());

	assert(m_setTasks.empty());

	Processor::Verifier& v = m_Processor.m_Verifier; // alias
	if (!v.m_vThreads.empty())
	{
		{
			std::unique_lock<std::mutex> scope(v.m_Mutex);
			v.m_iTask = 0;
			v.m_TaskNew.notify_all();
		}

		for (size_t i = 0; i < v.m_vThreads.size(); i++)
			if (v.m_vThreads[i].joinable())
				v.m_vThreads[i].join();
	}

	LOG_INFO() << "Node stopped";
}

void Node::Peer::SetTimer(uint32_t timeout_ms)
{
	if (!m_pTimer)
		m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());

	m_pTimer->start(timeout_ms, false, [this]() { OnTimer(); });
}

void Node::Peer::KillTimer()
{
	assert(m_pTimer);
	m_pTimer->cancel();
}

void Node::Peer::OnTimer()
{
	if (m_bConnected)
	{
		assert(!m_lstTasks.empty());

		LOG_WARNING() << "Peer " << m_RemoteAddr << " request timeout";

		if (m_pInfo)
			m_This.m_PeerMan.ModifyRating(*m_pInfo, PeerMan::Rating::PenaltyTimeout, false); // task (request) wasn't handled in time.

		DeleteSelf(false, ByeReason::Timeout);
	}
	else
		// Connect didn't finish in time
		DeleteSelf(true, 0);
}

void Node::Peer::OnResendPeers()
{
	PeerMan& pm = m_This.m_PeerMan;
	const PeerMan::RawRatingSet& rs = pm.get_Ratings();
	uint32_t nRemaining = pm.m_Cfg.m_DesiredHighest;

	for (PeerMan::RawRatingSet::const_iterator it = rs.begin(); nRemaining && (rs.end() != it); it++)
	{
		const PeerMan::PeerInfo& pi = it->get_ParentObj();
		if (m_bPiRcvd && (&pi == m_pInfo))
			continue; // skip

		proto::PeerInfo msg;
		msg.m_ID = pi.m_ID.m_Key;
		msg.m_LastAddr = pi.m_Addr.m_Value;
		Send(msg);
	}
}

void Node::Peer::GenerateSChannelNonce(ECC::Scalar::Native& nonce)
{
	ECC::uintBig& hv = m_This.m_SChannelSeed.V; // alias

	ECC::Hash::Processor() << "sch.nonce" << hv << GetTime_ms() >> hv;

	nonce.GenerateNonce(m_This.m_Cfg.m_WalletKey.V, hv, NULL);
}

void Node::Peer::OnConnectedSecure()
{
	LOG_INFO() << "Peer " << m_RemoteAddr << " Connected";

	m_bConnected = true;

	if (m_Port && m_This.m_Cfg.m_Listen.port())
	{
		// we've connected to the peer, let it now know our port
		proto::PeerInfoSelf msgPi;
		msgPi.m_Port = m_This.m_Cfg.m_Listen.port();
		Send(msgPi);
	}

	ECC::Scalar::Native sk = m_This.m_MyPrivateID.V;
	ProveID(sk, proto::IDType::Node);

	proto::Config msgCfg;
	msgCfg.m_CfgChecksum = Rules::get().Checksum;
	msgCfg.m_SpreadingTransactions = true;
	msgCfg.m_Bbs = true;
	msgCfg.m_AutoSendHdr = false;
	msgCfg.m_SendPeers = true;
	Send(msgCfg);

	if (m_This.m_Processor.m_Cursor.m_Sid.m_Row)
	{
		proto::NewTip msg;
		msg.m_ID = m_This.m_Processor.m_Cursor.m_ID;
		msg.m_ChainWork = m_This.m_Processor.m_Cursor.m_ChainWork;
		Send(msg);
	}
}

void Node::Peer::OnMsg(proto::Authentication&& msg)
{
	proto::NodeConnection::OnMsg(std::move(msg));
	LOG_INFO() << "Peer " << m_RemoteAddr << " Auth. Type=" << msg.m_IDType << ", ID=" << msg.m_ID;

	if (proto::IDType::Owner == msg.m_IDType)
	{
		if (msg.m_ID == m_This.m_MyOwnerID)
			m_bOwner = true;
	}

	if (proto::IDType::Node != msg.m_IDType)
		return;

	if (m_bPiRcvd || (msg.m_ID == ECC::Zero))
		ThrowUnexpected();

	m_bPiRcvd = true;
	LOG_INFO() << m_RemoteAddr << " received PI";

	PeerMan& pm = m_This.m_PeerMan; // alias

	if (m_pInfo)
	{
		// probably we connected by the address
		if (m_pInfo->m_ID.m_Key == msg.m_ID)
		{
			pm.OnSeen(*m_pInfo);
			return; // all settled (already)
		}

		// detach from it
		m_pInfo->m_pLive = NULL;

		if (m_pInfo->m_ID.m_Key == ECC::Zero)
		{
			LOG_INFO() << "deleted anonymous PI";
			pm.Delete(*m_pInfo); // it's anonymous.
		}
		else
		{
			LOG_INFO() << "PeerID is different";
			pm.OnActive(*m_pInfo, false);
			pm.RemoveAddr(*m_pInfo); // turned-out to be wrong
		}

		m_pInfo = NULL;
	}

	if (msg.m_ID == m_This.m_MyPublicID)
	{
		LOG_WARNING() << "Loopback connection";
		DeleteSelf(false, ByeReason::Loopback);
		return;
	}

	io::Address addr;

	bool bAddrValid = (m_Port > 0);
	if (bAddrValid)
	{
		addr = m_RemoteAddr;
		addr.port(m_Port);
	} else
		LOG_INFO() << "No PI port"; // doesn't accept incoming connections?
		

	PeerMan::PeerInfoPlus* pPi = (PeerMan::PeerInfoPlus*) pm.OnPeer(msg.m_ID, addr, bAddrValid);
	assert(pPi);

	if (pPi->m_pLive)
	{
		LOG_INFO() << "Duplicate connection with the same PI.";
		// Duplicate connection. In this case we have to choose wether to terminate this connection, or the previous. The best is to do it asymmetrically.
		// We decide this based on our Node IDs.

		if (m_This.m_MyPublicID > msg.m_ID)
		{
			pPi->m_pLive->DeleteSelf(false, ByeReason::Duplicate);
			assert(!pPi->m_pLive);
		}
		else
		{
			DeleteSelf(false, ByeReason::Duplicate);
			return;
		}
	}

	if (!pPi->m_RawRating.m_Value)
	{
		LOG_INFO() << "Banned PI. Ignoring";
		DeleteSelf(false, ByeReason::Ban);
		return;
	}

	// attach to it
	pPi->m_pLive = this;
	m_pInfo = pPi;
	pm.OnActive(*pPi, true);
	pm.OnSeen(*pPi);

	LOG_INFO() << *m_pInfo << " connected, info updated";
}

void Node::Peer::OnDisconnect(const DisconnectReason& dr)
{
	LOG_WARNING() << m_RemoteAddr << ": " << dr;

	bool bIsErr = true;
	uint8_t nByeReason = 0;

	switch (dr.m_Type)
	{
	default: assert(false);

	case DisconnectReason::Io:
		break;

	case DisconnectReason::Bye:
		bIsErr = false;
		break;

	case DisconnectReason::ProcessingExc:
		nByeReason = ByeReason::Ban;
		break;
	}

	DeleteSelf(bIsErr, nByeReason);
}

void Node::Peer::ReleaseTasks()
{
	while (!m_lstTasks.empty())
		ReleaseTask(m_lstTasks.front());
}

void Node::Peer::ReleaseTask(Task& t)
{
	assert(this == t.m_pOwner);
	t.m_pOwner = NULL;

	m_lstTasks.erase(TaskList::s_iterator_to(t));
	m_This.m_lstTasksUnassigned.push_back(t);

	if (t.m_bRelevant)
		m_This.TryAssignTask(t, NULL);
	else
		m_This.DeleteUnassignedTask(t);
}

void Node::Peer::DeleteSelf(bool bIsError, uint8_t nByeReason)
{
	LOG_INFO() << "-Peer " << m_RemoteAddr;

	if (nByeReason && m_bConnected)
	{
		proto::Bye msg;
		msg.m_Reason = nByeReason;
		Send(msg);
	}

	m_TipHeight = 0; // prevent reassigning the tasks
	m_TipWork = ECC::Zero;

	ReleaseTasks();
	Unsubscribe();

	if (m_pInfo)
	{
		// detach
		assert(this == m_pInfo->m_pLive);
		m_pInfo->m_pLive = NULL;

		m_This.m_PeerMan.OnActive(*m_pInfo, false);

		if (bIsError)
			m_This.m_PeerMan.OnRemoteError(*m_pInfo, ByeReason::Ban == nByeReason);
	}

	m_This.m_lstPeers.erase(PeerList::s_iterator_to(*this));
	delete this;
}

void Node::Peer::Unsubscribe(Bbs::Subscription& s)
{
	m_This.m_Bbs.m_Subscribed.erase(Bbs::Subscription::BbsSet::s_iterator_to(s.m_Bbs));
	m_Subscriptions.erase(Bbs::Subscription::PeerSet::s_iterator_to(s.m_Peer));
	delete &s;
}

void Node::Peer::Unsubscribe()
{
	while (!m_Subscriptions.empty())
		Unsubscribe(m_Subscriptions.begin()->get_ParentObj());
}

void Node::Peer::TakeTasks()
{
	for (TaskList::iterator it = m_This.m_lstTasksUnassigned.begin(); m_This.m_lstTasksUnassigned.end() != it; )
	{
		Task& t = *(it++);

		if (m_This.ShouldAssignTask(t, *this))
			m_This.AssignTask(t, *this);
	}
}

void Node::Peer::OnMsg(proto::Ping&&)
{
	proto::Pong msg;
	Send(msg);
}

void Node::Peer::OnMsg(proto::NewTip&& msg)
{
	if (msg.m_ChainWork < m_TipWork)
		ThrowUnexpected();

	m_TipHeight = msg.m_ID.m_Height;
	m_TipWork = msg.m_ChainWork;
	m_setRejected.clear();

	LOG_INFO() << "Peer " << m_RemoteAddr << " Tip: " << msg.m_ID;

	TakeTasks();

	if (m_This.m_Processor.IsStateNeeded(msg.m_ID))
		m_This.m_Processor.RequestData(msg.m_ID, false, m_pInfo ? &m_pInfo->m_ID.m_Key : NULL);
}

Node::Task& Node::Peer::get_FirstTask()
{
	if (m_lstTasks.empty())
		ThrowUnexpected();
	return m_lstTasks.front();
}

void Node::Peer::OnFirstTaskDone()
{
	ReleaseTask(get_FirstTask());
	SetTimerWrtFirstTask();
}

void Node::Peer::OnMsg(proto::DataMissing&&)
{
	Task& t = get_FirstTask();
	m_setRejected.insert(t.m_Key);

	OnFirstTaskDone();
}

void Node::Peer::OnMsg(proto::GetHdr&& msg)
{
	uint64_t rowid = m_This.m_Processor.get_DB().StateFindSafe(msg.m_ID);
	if (rowid)
	{
		proto::Hdr msgHdr;
		m_This.m_Processor.get_DB().get_State(rowid, msgHdr.m_Description);
		Send(msgHdr);
	} else
	{
		proto::DataMissing msgMiss;
		Send(msgMiss);
	}
}

void Node::Peer::OnMsg(proto::Hdr&& msg)
{
	Task& t = get_FirstTask();

	if (t.m_Key.second)
		ThrowUnexpected();

	Block::SystemState::ID id;
	msg.m_Description.get_ID(id);
	if (id != t.m_Key.first)
		ThrowUnexpected();

	assert(m_bPiRcvd && m_pInfo);
	m_This.m_PeerMan.ModifyRating(*m_pInfo, PeerMan::Rating::RewardHeader, true);

	NodeProcessor::DataStatus::Enum eStatus = m_This.m_Processor.OnState(msg.m_Description, m_pInfo->m_ID.m_Key);
	OnFirstTaskDone(eStatus);
}

void Node::Peer::OnMsg(proto::GetBody&& msg)
{
	uint64_t rowid = m_This.m_Processor.get_DB().StateFindSafe(msg.m_ID);
	if (rowid)
	{
		proto::Body msgBody;
		ByteBuffer bbRollback;
		m_This.m_Processor.get_DB().GetStateBlock(rowid, msgBody.m_Buffer, bbRollback);

		if (!msgBody.m_Buffer.empty())
		{
			Send(msgBody);
			return;
		}

	}

	proto::DataMissing msgMiss;
	Send(msgMiss);
}

void Node::Peer::OnMsg(proto::Body&& msg)
{
	Task& t = get_FirstTask();

	if (!t.m_Key.second)
		ThrowUnexpected();

	assert(m_bPiRcvd && m_pInfo);
	m_This.m_PeerMan.ModifyRating(*m_pInfo, PeerMan::Rating::RewardBlock, true);

	const Block::SystemState::ID& id = t.m_Key.first;

	NodeProcessor::DataStatus::Enum eStatus = m_This.m_Processor.OnBlock(id, msg.m_Buffer, m_pInfo->m_ID.m_Key);
	OnFirstTaskDone(eStatus);
}

void Node::Peer::OnFirstTaskDone(NodeProcessor::DataStatus::Enum eStatus)
{
	if (NodeProcessor::DataStatus::Invalid == eStatus)
		ThrowUnexpected();

	get_FirstTask().m_bRelevant = false;
	OnFirstTaskDone();

	if (NodeProcessor::DataStatus::Accepted == eStatus)
		m_This.RefreshCongestions(); // NOTE! Can call OnPeerInsane()
}

void Node::Peer::OnMsg(proto::NewTransaction&& msg)
{
	if (!msg.m_Transaction)
		ThrowUnexpected(); // our deserialization permits NULL Ptrs.
	// However the transaction body must have already been checked for NULLs

	proto::Boolean msgOut;
	msgOut.m_Value = OnNewTransaction(std::move(msg.m_Transaction));
	Send(msgOut);
}

bool Node::Peer::OnNewTransaction(Transaction::Ptr&& ptx)
{
	NodeProcessor::TxPool::Element::Tx key;
	ptx->get_Key(key.m_Key);

	NodeProcessor::TxPool::TxSet::iterator it = m_This.m_TxPool.m_setTxs.find(key);
	if (m_This.m_TxPool.m_setTxs.end() != it)
		return true;

	m_This.m_Wtx.Delete(key.m_Key);

	// new transaction
	const Transaction& tx = *ptx;
	Transaction::Context ctx;

	bool bValid = !tx.m_vInputs.empty() && !tx.m_vKernelsOutput.empty();
	if (bValid)
		bValid = m_This.m_Processor.ValidateTx(tx, ctx);

	{
		// Log it
		std::ostringstream os;

		os << "Tx " << key.m_Key << " from " << m_RemoteAddr;

		for (size_t i = 0; i < tx.m_vInputs.size(); i++)
			os << "\n\tI: " << tx.m_vInputs[i]->m_Commitment;

		for (size_t i = 0; i < tx.m_vOutputs.size(); i++)
		{
			const Output& outp = *tx.m_vOutputs[i];
			os << "\n\tO: " << outp.m_Commitment;

			if (outp.m_Incubation)
				os << ", Incubation +" << outp.m_Incubation;

			if (outp.m_pPublic)
				os << ", Sum=" << outp.m_pPublic->m_Value;

			if (outp.m_pConfidential)
				os << ", Confidential";
		}

		for (size_t i = 0; i < tx.m_vKernelsOutput.size(); i++)
			os << "\n\tK: Fee=" << tx.m_vKernelsOutput[i]->m_Fee;

		os << "\n\tValid: " << bValid;
		LOG_INFO() << os.str();
	}

	if (!bValid)
		return false;

	proto::HaveTransaction msgOut;
	msgOut.m_ID = key.m_Key;

	for (PeerList::iterator it = m_This.m_lstPeers.begin(); m_This.m_lstPeers.end() != it; it++)
	{
		Peer& peer = *it;
		if (this == &peer)
			continue;
		if (!peer.m_Config.m_SpreadingTransactions)
			continue;

		peer.Send(msgOut);
	}

	m_This.m_TxPool.AddValidTx(std::move(ptx), ctx, key.m_Key);
	m_This.m_TxPool.ShrinkUpTo(m_This.m_Cfg.m_MaxPoolTransactions);
	m_This.m_Miner.SetTimer(m_This.m_Cfg.m_Timeout.m_MiningSoftRestart_ms, false);

	return true;
}

void Node::Peer::OnMsg(proto::Config&& msg)
{
	if (msg.m_CfgChecksum != Rules::get().Checksum)
		ThrowUnexpected("Incompatible peer cfg!");

	if (!m_Config.m_AutoSendHdr && msg.m_AutoSendHdr && m_This.m_Processor.m_Cursor.m_Sid.m_Row)
	{
		proto::Hdr msgHdr;
		msgHdr.m_Description = m_This.m_Processor.m_Cursor.m_Full;
		Send(msgHdr);
	}

	if (!m_Config.m_SpreadingTransactions && msg.m_SpreadingTransactions)
	{
		proto::HaveTransaction msgOut;

		for (NodeProcessor::TxPool::TxSet::iterator it = m_This.m_TxPool.m_setTxs.begin(); m_This.m_TxPool.m_setTxs.end() != it; it++)
		{
			msgOut.m_ID = it->m_Key;
			Send(msgOut);
		}
	}

	if (m_Config.m_SendPeers != msg.m_SendPeers)
	{
		if (msg.m_SendPeers)
		{
			if (!m_pTimerPeers)
				m_pTimerPeers = io::Timer::create(io::Reactor::get_Current().shared_from_this());

			m_pTimerPeers->start(m_This.m_Cfg.m_Timeout.m_TopPeersUpd_ms, true, [this]() { OnResendPeers(); });

			OnResendPeers();
		}
		else
			if (m_pTimerPeers)
				m_pTimerPeers->cancel();
	}

	if (!m_Config.m_Bbs && msg.m_Bbs)
	{
		proto::BbsHaveMsg msgOut;

		NodeDB& db = m_This.m_Processor.get_DB();
		NodeDB::WalkerBbs wlk(db);

		for (db.EnumAllBbs(wlk); wlk.MoveNext(); )
		{
			msgOut.m_Key = wlk.m_Data.m_Key;
			Send(msgOut);
		}
	}

	m_Config = msg;
}

void Node::Peer::OnMsg(proto::HaveTransaction&& msg)
{
	NodeProcessor::TxPool::Element::Tx key;
	key.m_Key = msg.m_ID;

	NodeProcessor::TxPool::TxSet::iterator it = m_This.m_TxPool.m_setTxs.find(key);
	if (m_This.m_TxPool.m_setTxs.end() != it)
		return; // already have it

	if (!m_This.m_Wtx.Add(key.m_Key))
		return; // already waiting for it

	proto::GetTransaction msgOut;
	msgOut.m_ID = msg.m_ID;
	Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetTransaction&& msg)
{
	NodeProcessor::TxPool::Element::Tx key;
	key.m_Key = msg.m_ID;

	NodeProcessor::TxPool::TxSet::iterator it = m_This.m_TxPool.m_setTxs.find(key);
	if (m_This.m_TxPool.m_setTxs.end() == it)
		return; // don't have it

	// temporarily move the transaction to the Msg object, but make sure it'll be restored back, even in case of the exception.
	struct Guard
	{
		proto::NewTransaction m_Msg;
		Transaction::Ptr* m_ppVal;

		void Swap() { m_ppVal->swap(m_Msg.m_Transaction); }

		~Guard() { Swap(); }
	};

	Guard g;
	g.m_ppVal = &it->get_ParentObj().m_pValue;
	g.Swap();

	Send(g.m_Msg);
}

void Node::Peer::OnMsg(proto::GetMined&& msg)
{
	proto::Mined msgOut;

	if (m_bOwner || !m_This.m_Cfg.m_RestrictMinedReportToOwner)
	{
		NodeDB& db = m_This.m_Processor.get_DB();
		NodeDB::WalkerMined wlk(db);
		for (db.EnumMined(wlk, msg.m_HeightMin); wlk.MoveNext(); )
		{
			msgOut.m_Entries.resize(msgOut.m_Entries.size() + 1);
			proto::PerMined& x = msgOut.m_Entries.back();

			x.m_Fees = wlk.m_Amount;
			x.m_Active = 0 != (db.GetStateFlags(wlk.m_Sid.m_Row) & NodeDB::StateFlags::Active);

			Block::SystemState::Full s;
			db.get_State(wlk.m_Sid.m_Row, s);
			s.get_ID(x.m_ID);

			if (msgOut.m_Entries.size() == proto::PerMined::s_EntriesMax)
				break;
		}
	} else
		LOG_WARNING() << "Peer " << m_RemoteAddr << " Unauthorized Mining report request. Returned empty result.";

	Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetProofState&& msg)
{
	if (msg.m_Height < Rules::HeightGenesis)
		ThrowUnexpected();

	proto::ProofStateForDummies msgOut;

	Processor& p = m_This.m_Processor;
	const NodeDB::StateID& sid = p.m_Cursor.m_Sid;
	if (sid.m_Row && (msg.m_Height < sid.m_Height))
	{
		p.get_DB().get_Proof(msgOut.m_Proof, sid, msg.m_Height);

		msgOut.m_Proof.resize(msgOut.m_Proof.size() + 1);
		msgOut.m_Proof.back().first = true;
		p.get_ChainWork(msgOut.m_Proof.back().second, false);

		msgOut.m_Proof.resize(msgOut.m_Proof.size() + 1);
		msgOut.m_Proof.back().first = true;
		p.get_CurrentLive(msgOut.m_Proof.back().second);

		uint64_t rowid = p.FindActiveAtStrict(msg.m_Height);
		p.get_DB().get_State(rowid, msgOut.m_Hdr);
	}
	else
		ZeroObject(msgOut.m_Hdr);

	Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetProofKernel&& msg)
{
	proto::Proof msgOut;

	RadixHashOnlyTree& t = m_This.m_Processor.get_Kernels();

	RadixHashOnlyTree::Cursor cu;
	bool bCreate = false;
	if (t.Find(cu, msg.m_KernelHash, bCreate))
	{
		t.get_Proof(msgOut.m_Proof, cu);
		msgOut.m_Proof.reserve(msgOut.m_Proof.size() + 2);

		msgOut.m_Proof.resize(msgOut.m_Proof.size() + 1);
		msgOut.m_Proof.back().first = false;
		m_This.m_Processor.get_Utxos().get_Hash(msgOut.m_Proof.back().second);

		msgOut.m_Proof.resize(msgOut.m_Proof.size() + 1);
		msgOut.m_Proof.back().first = false;
		m_This.m_Processor.get_CurrentPart2(msgOut.m_Proof.back().second, false);
	}

	Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetProofUtxo&& msg)
{
	struct Traveler :public UtxoTree::ITraveler
	{
		proto::ProofUtxo m_Msg;
		UtxoTree* m_pTree;
		Merkle::Hash m_hvPart2;
		Merkle::Hash m_hvKernels;

		virtual bool OnLeaf(const RadixTree::Leaf& x) override {

			const UtxoTree::MyLeaf& v = (UtxoTree::MyLeaf&) x;
			UtxoTree::Key::Data d;
			d = v.m_Key;

			m_Msg.m_Proofs.resize(m_Msg.m_Proofs.size() + 1);
			Input::Proof& ret = m_Msg.m_Proofs.back();

			ret.m_Count = v.m_Value.m_Count;
			ret.m_Maturity = d.m_Maturity;
			m_pTree->get_Proof(ret.m_Proof, *m_pCu);

			ret.m_Proof.reserve(ret.m_Proof.size() + 2);

			ret.m_Proof.resize(ret.m_Proof.size() + 1);
			ret.m_Proof.back().first = true;
			ret.m_Proof.back().second = m_hvKernels;

			ret.m_Proof.resize(ret.m_Proof.size() + 1);
			ret.m_Proof.back().first = false;
			ret.m_Proof.back().second = m_hvPart2;

			return m_Msg.m_Proofs.size() < Input::Proof::s_EntriesMax;
		}
	} t;

	t.m_pTree = &m_This.m_Processor.get_Utxos();
	m_This.m_Processor.get_Kernels().get_Hash(t.m_hvKernels);
	m_This.m_Processor.get_CurrentPart2(t.m_hvPart2, false);

	UtxoTree::Cursor cu;
	t.m_pCu = &cu;

	// bounds
	UtxoTree::Key kMin, kMax;

	UtxoTree::Key::Data d;
	d.m_Commitment = msg.m_Utxo.m_Commitment;
	d.m_Maturity = msg.m_MaturityMin;
	kMin = d;
	d.m_Maturity = Height(-1);
	kMax = d;

	t.m_pBound[0] = kMin.m_pArr;
	t.m_pBound[1] = kMax.m_pArr;

	t.m_pTree->Traverse(t);

	Send(t.m_Msg);
}

void Node::Peer::OnMsg(proto::PeerInfoSelf&& msg)
{
	m_Port = msg.m_Port;
}

void Node::Peer::OnMsg(proto::PeerInfo&& msg)
{
	if (msg.m_ID != m_This.m_MyPublicID)
		m_This.m_PeerMan.OnPeer(msg.m_ID, msg.m_LastAddr, false);
}

void Node::Peer::OnMsg(proto::GetTime&& msg)
{
	proto::Time msgOut;
	msgOut.m_Value = getTimestamp();
	Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetExternalAddr&& msg)
{
	proto::ExternalAddr msgOut;
	msgOut.m_Value = m_RemoteAddr.ip();
	Send(msgOut);
}

void Node::Peer::OnMsg(proto::BbsMsg&& msg)
{
	Timestamp t = getTimestamp();
	Timestamp t0 = t - m_This.m_Cfg.m_Timeout.m_BbsMessageTimeout_s;
	Timestamp t1 = t + m_This.m_Cfg.m_Timeout.m_BbsMessageMaxAhead_s;

	if ((msg.m_TimePosted <= t0) || (msg.m_TimePosted > t1))
		return;

	NodeDB& db = m_This.m_Processor.get_DB();
	NodeDB::WalkerBbs wlk(db);

	wlk.m_Data.m_Channel = msg.m_Channel;
	wlk.m_Data.m_TimePosted = msg.m_TimePosted;
	wlk.m_Data.m_Message = NodeDB::Blob(msg.m_Message);

	Bbs::CalcMsgKey(wlk.m_Data);

	if (db.BbsFind(wlk))
		return; // already have it

	m_This.m_Bbs.MaybeCleanup();

	db.BbsIns(wlk.m_Data);
	m_This.m_Bbs.m_W.Delete(wlk.m_Data.m_Key);

	// 1. Send to other BBS-es

	proto::BbsHaveMsg msgOut;
	msgOut.m_Key = wlk.m_Data.m_Key;

	for (PeerList::iterator it = m_This.m_lstPeers.begin(); m_This.m_lstPeers.end() != it; it++)
	{
		Peer& peer = *it;
		if (this == &peer)
			continue;
		if (!peer.m_Config.m_Bbs)
			continue;

		peer.Send(msgOut);
	}

	// 2. Send to subscribed
	typedef Bbs::Subscription::BbsSet::iterator It;

	Bbs::Subscription::InBbs key;
	key.m_Channel = msg.m_Channel;

	for (std::pair<It, It> range = m_This.m_Bbs.m_Subscribed.equal_range(key); range.first != range.second; range.first++)
	{
		Bbs::Subscription& s = range.first->get_ParentObj();

		if (this == s.m_pPeer)
			continue;

		s.m_pPeer->SendBbsMsg(wlk.m_Data);
	}
}

void Node::Peer::OnMsg(proto::BbsHaveMsg&& msg)
{
	NodeDB& db = m_This.m_Processor.get_DB();
	NodeDB::WalkerBbs wlk(db);

	wlk.m_Data.m_Key = msg.m_Key;
	if (db.BbsFind(wlk))
		return; // already have it

	if (!m_This.m_Bbs.m_W.Add(msg.m_Key))
		return; // already waiting for it

	proto::BbsGetMsg msgOut;
	msgOut.m_Key = msg.m_Key;
	Send(msgOut);
}

void Node::Peer::OnMsg(proto::BbsGetMsg&& msg)
{
	NodeDB& db = m_This.m_Processor.get_DB();
	NodeDB::WalkerBbs wlk(db);

	wlk.m_Data.m_Key = msg.m_Key;
	if (!db.BbsFind(wlk))
		return; // don't have it

	SendBbsMsg(wlk.m_Data);
}

void Node::Peer::SendBbsMsg(const NodeDB::WalkerBbs::Data& d)
{
	proto::BbsMsg msgOut;
	msgOut.m_Channel = d.m_Channel;
	msgOut.m_TimePosted = d.m_TimePosted;
	d.m_Message.Export(msgOut.m_Message); // TODO: avoid buf allocation

	Send(msgOut);
}

void Node::Peer::OnMsg(proto::BbsSubscribe&& msg)
{
	Bbs::Subscription::InPeer key;
	key.m_Channel = msg.m_Channel;

	Bbs::Subscription::PeerSet::iterator it = m_Subscriptions.find(key);
	if ((m_Subscriptions.end() == it) != msg.m_On)
		return;

	if (msg.m_On)
	{
		Bbs::Subscription* pS = new Bbs::Subscription;
		pS->m_pPeer = this;

		pS->m_Bbs.m_Channel = msg.m_Channel;
		pS->m_Peer.m_Channel = msg.m_Channel;
		m_This.m_Bbs.m_Subscribed.insert(pS->m_Bbs);
		m_Subscriptions.insert(pS->m_Peer);

		NodeDB& db = m_This.m_Processor.get_DB();
		NodeDB::WalkerBbs wlk(db);

		wlk.m_Data.m_Channel = msg.m_Channel;
		wlk.m_Data.m_TimePosted = msg.m_TimeFrom;

		for (db.EnumBbs(wlk); wlk.MoveNext(); )
			SendBbsMsg(wlk.m_Data);
	}
	else
		Unsubscribe(it->get_ParentObj());
}

void Node::Peer::OnMsg(proto::BbsPickChannel&& msg)
{
	proto::BbsPickChannelRes msgOut;
	msgOut.m_Channel = m_This.m_Bbs.m_RecommendedChannel;
	Send(msgOut);
}

void Node::Server::OnAccepted(io::TcpStream::Ptr&& newStream, int errorCode)
{
	if (newStream)
	{
        LOG_DEBUG() << "New peer connected: " << newStream->address();
		Peer* p = get_ParentObj().AllocPeer(newStream->peer_address());
		p->Accept(std::move(newStream));
		p->SecureConnect();
	}
}

void Node::Miner::OnRefresh(uint32_t iIdx)
{
	while (true)
	{
		Task::Ptr pTask;
		Block::SystemState::Full s;

		{
			std::scoped_lock<std::mutex> scope(m_Mutex);
			if (!m_pTask || *m_pTask->m_pStop)
				break;

			pTask = m_pTask;
			s = pTask->m_Hdr; // local copy
		}

		ECC::Hash::Value hv; // pick pseudo-random initial nonce for mining.
		ECC::Hash::Processor()
			<< get_ParentObj().m_Cfg.m_MinerID
			<< get_ParentObj().m_Processor.m_Kdf.m_Secret.V
			<< iIdx
			<< s.m_Height
			>> hv;

		static_assert(sizeof(s.m_PoW.m_Nonce) <= sizeof(hv));
		LOG_INFO() << "OnRefresh: nonce = " << hv;

		memcpy(s.m_PoW.m_Nonce.m_pData, hv.m_pData, sizeof(s.m_PoW.m_Nonce.m_pData));

		Block::PoW::Cancel fnCancel = [this, pTask](bool bRetrying)
		{
			if (*pTask->m_pStop)
				return true;

			if (bRetrying)
			{
				std::scoped_lock<std::mutex> scope(m_Mutex);
				if (pTask != m_pTask)
					return true; // soft restart triggered
			}

			return false;
		};

		if (Rules::get().FakePoW)
		{
			uint32_t timeout_ms = get_ParentObj().m_Cfg.m_TestMode.m_FakePowSolveTime_ms;

			bool bSolved = false;

			for (uint32_t t0_ms = GetTime_ms(); ; )
			{
				if (fnCancel(false))
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(50));

				uint32_t dt_ms = GetTime_ms() - t0_ms;

				if (dt_ms >= timeout_ms)
				{
					bSolved = true;
					break;
				}
			}

			if (!bSolved)
				continue;

			ZeroObject(s.m_PoW.m_Indices); // keep the difficulty intact
		}
		else
		{
			if (!s.GeneratePoW(fnCancel))
				continue;
		}

		std::scoped_lock<std::mutex> scope(m_Mutex);

		if (*pTask->m_pStop)
			continue; // either aborted, or other thread was faster

		pTask->m_Hdr = s; // save the result
		*pTask->m_pStop = true;
		m_pTask = pTask; // In case there was a soft restart we restore the one that we mined.

		m_pEvtMined->post();
		break;
	}
}

void Node::Miner::HardAbortSafe()
{
	std::scoped_lock<std::mutex> scope(m_Mutex);

	if (m_pTask)
	{
		*m_pTask->m_pStop = true;
		m_pTask = NULL;
	}
}

void Node::Miner::SetTimer(uint32_t timeout_ms, bool bHard)
{
	if (!m_pTimer)
		m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());
	else
		if (m_bTimerPending && !bHard)
			return;

	m_pTimer->start(timeout_ms, false, [this]() { OnTimer(); });
	m_bTimerPending = true;
}

void Node::Miner::OnTimer()
{
	m_bTimerPending = false;
	Restart();
}

bool Node::Miner::Restart()
{
	if (m_vThreads.empty())
		return false; //  n/a

	Block::Body* pTreasury = NULL;

	if (get_ParentObj().m_Processor.m_Cursor.m_SubsidyOpen)
	{
		Height dh = get_ParentObj().m_Processor.m_Cursor.m_Sid.m_Height + 1 - Rules::HeightGenesis;
		std::vector<Block::Body>& vTreasury = get_ParentObj().m_Cfg.m_vTreasury;
		if (dh >= vTreasury.size())
			return false;

		pTreasury = &vTreasury[dh];
		pTreasury->m_SubsidyClosing = (dh + 1 == vTreasury.size());
	}

	Task::Ptr pTask(std::make_shared<Task>());

	bool bRes = pTreasury ?
		get_ParentObj().m_Processor.GenerateNewBlock(get_ParentObj().m_TxPool, pTask->m_Hdr, pTask->m_Body, pTask->m_Fees, *pTreasury) :
		get_ParentObj().m_Processor.GenerateNewBlock(get_ParentObj().m_TxPool, pTask->m_Hdr, pTask->m_Body, pTask->m_Fees);

	if (!bRes)
	{
		LOG_WARNING() << "Block generation failed, can't mine!";
		return false;
	}

	Block::SystemState::ID id;
	pTask->m_Hdr.get_ID(id);

	LOG_INFO() << "Block generated: " << id << ", Fee=" << pTask->m_Fees << ", Difficulty=" << pTask->m_Hdr.m_PoW.m_Difficulty << ", Size=" << pTask->m_Body.size();

	// let's mine it.
	std::scoped_lock<std::mutex> scope(m_Mutex);

	if (m_pTask)
	{
		if (*m_pTask->m_pStop)
			return true; // block already mined, probably notification to this thread on its way. Ignore the newly-constructed block
		pTask->m_pStop = m_pTask->m_pStop; // use the same soft-restart indicator
	}
	else
	{
		pTask->m_pStop.reset(new volatile bool);
		*pTask->m_pStop = false;
	}

	m_pTask = pTask;

	for (size_t i = 0; i < m_vThreads.size(); i++)
		m_vThreads[i].m_pEvt->post();

	return true;
}

void Node::Miner::OnMined()
{
	Task::Ptr pTask;
	{
		std::scoped_lock<std::mutex> scope(m_Mutex);
		if (!(m_pTask && *m_pTask->m_pStop))
			return; //?!
		pTask.swap(m_pTask);
	}

	Block::SystemState::ID id;
	pTask->m_Hdr.get_ID(id);

	LOG_INFO() << "New block mined: " << id;

	NodeProcessor::DataStatus::Enum eStatus = get_ParentObj().m_Processor.OnState(pTask->m_Hdr, PeerID());
	switch (eStatus)
	{
	default:
	case NodeProcessor::DataStatus::Invalid:
		// Some bug?
		LOG_WARNING() << "Mined block rejected as invalid!";
		return;

	case NodeProcessor::DataStatus::Rejected:
		// Someone else mined exactly the same block!
		LOG_WARNING() << "Mined block duplicated";
		return;

	case NodeProcessor::DataStatus::Accepted:
		break; // ok
	}
	assert(NodeProcessor::DataStatus::Accepted == eStatus); 

	NodeDB::StateID sid;
	verify(sid.m_Row = get_ParentObj().m_Processor.get_DB().StateFindSafe(id));
	sid.m_Height = id.m_Height;

	get_ParentObj().m_Processor.get_DB().SetMined(sid, pTask->m_Fees); // ding!

	eStatus = get_ParentObj().m_Processor.OnBlock(id, pTask->m_Body, PeerID()); // will likely trigger OnNewState(), and spread this block to the network
	assert(NodeProcessor::DataStatus::Accepted == eStatus);
}

void Node::Compressor::Init()
{
	m_bStop = true;

	OnRolledBack(); // delete potentially ahead-of-time macroblocks
	Cleanup(); // delete exceeding backlog, broken files

	OnNewState();
}

void Node::Compressor::Cleanup()
{
	// delete missing datas, delete exceeding backlog
	Processor& p = get_ParentObj().m_Processor;

	uint32_t nBacklog = get_ParentObj().m_Cfg.m_HistoryCompression.m_MaxBacklog + 1;

	NodeDB::WalkerState ws(p.get_DB());
	for (p.get_DB().EnumMacroblocks(ws); ws.MoveNext(); )
	{
		Block::BodyBase::RW rw;
		FmtPath(rw, ws.m_Sid.m_Height, NULL);

		if (nBacklog && rw.Open(true))
			nBacklog--; // ok
		else
		{
			LOG_WARNING() << "History at height " << ws.m_Sid.m_Height << " not found";
			Delete(ws.m_Sid);
		}
	}
}

void Node::Compressor::OnRolledBack()
{
	Processor& p = get_ParentObj().m_Processor;

	if (m_hrNew.m_Max > p.m_Cursor.m_ID.m_Height)
		StopCurrent();

	NodeDB::WalkerState ws(p.get_DB());
	p.get_DB().EnumMacroblocks(ws);

	while (ws.MoveNext() && (ws.m_Sid.m_Height > p.m_Cursor.m_ID.m_Height))
		Delete(ws.m_Sid);

	// wait for OnNewState callback to realize new task
}

void Node::Compressor::Delete(const NodeDB::StateID& sid)
{
	NodeDB& db = get_ParentObj().m_Processor.get_DB();
	db.MacroblockDel(sid.m_Row);

	Block::BodyBase::RW rw;
	FmtPath(rw, sid.m_Height, NULL);
	rw.Delete();

	LOG_WARNING() << "History at height " << sid.m_Height << " deleted";
}

void Node::Compressor::OnNewState()
{
	if (m_hrNew.m_Max)
		return; // alreaddy in progress

	const Config::HistoryCompression& cfg = get_ParentObj().m_Cfg.m_HistoryCompression;
	Processor& p = get_ParentObj().m_Processor;

	if (p.m_Cursor.m_ID.m_Height - Rules::HeightGenesis + 1 <= cfg.m_Threshold)
		return;

	HeightRange hr;
	hr.m_Max = p.m_Cursor.m_ID.m_Height - cfg.m_Threshold;

	// last macroblock
	NodeDB::WalkerState ws(p.get_DB());
	p.get_DB().EnumMacroblocks(ws);
	hr.m_Min = ws.MoveNext() ? ws.m_Sid.m_Height : 0;

	if (hr.m_Min + cfg.m_MinAggregate > hr.m_Max)
		return;

	LOG_INFO() << "History generation started up to height " << hr.m_Max;

	// Start aggregation
	m_hrNew = hr;
	m_bStop = false;
	m_bSuccess = false;
	ZeroObject(m_hrInplaceRequest);

	m_Link.m_pReactor = io::Reactor::get_Current().shared_from_this();
	m_Link.m_pEvt = io::AsyncEvent::create(m_Link.m_pReactor, [this]() { OnNotify(); });;
	m_Link.m_Thread = std::thread(&Compressor::Proceed, this);
}

void Node::Compressor::FmtPath(Block::BodyBase::RW& rw, Height h, const Height* pH0)
{
	std::stringstream str;
	if (!pH0)
		str << get_ParentObj().m_Cfg.m_HistoryCompression.m_sPathOutput << "mb_";
	else
		str << get_ParentObj().m_Cfg.m_HistoryCompression.m_sPathTmp << "tmp_" << *pH0 << "_";

	str << h;
	rw.m_sPath = str.str();
}

void Node::Compressor::OnNotify()
{
	assert(m_hrNew.m_Max);

	if (m_hrInplaceRequest.m_Max)
	{
		// extract & resume
		try
		{
			Block::Body::RW rw;
			FmtPath(rw, m_hrInplaceRequest.m_Max, &m_hrInplaceRequest.m_Min);
			if (!rw.Open(false))
				std::ThrowIoError();

			get_ParentObj().m_Processor.ExportMacroBlock(rw, m_hrInplaceRequest);
		}
		catch (const std::exception& e) {
			m_bStop = true; // error indication
			LOG_WARNING() << "History add " << e.what();
		}

		{
			// lock is aqcuired by the other thread before it trigger the events. The following line guarantees it won't miss our notification
			std::unique_lock<std::mutex> scope(m_Mutex);
		}

		m_Cond.notify_one();
	}
	else
	{
		Height h = m_hrNew.m_Max;
		StopCurrent();

		if (m_bSuccess)
		{
			std::string pSrc[Block::Body::RW::s_Datas];
			std::string pTrg[Block::Body::RW::s_Datas];

			Block::Body::RW rwSrc, rwTrg;
			FmtPath(rwSrc, h, &Rules::HeightGenesis);
			FmtPath(rwTrg, h, NULL);
			rwSrc.GetPathes(pSrc);
			rwTrg.GetPathes(pTrg);

			for (int i = 0; i < Block::Body::RW::s_Datas; i++)
			{
#ifdef WIN32
				bool bOk = (FALSE != MoveFileExA(pSrc[i].c_str(), pTrg[i].c_str(), MOVEFILE_REPLACE_EXISTING));
#else // WIN32
				bool bOk = !rename(pSrc[i].c_str(), pTrg[i].c_str());
#endif // WIN32

				if (!bOk)
				{
					LOG_WARNING() << "History file move/rename failed";
					m_bSuccess = false;
					break;
				}
			}

			if (!m_bSuccess)
			{
				rwSrc.Delete();
				rwTrg.Delete();
			}
		}

		if (m_bSuccess)
		{
			uint64_t rowid = get_ParentObj().m_Processor.FindActiveAtStrict(h);
			get_ParentObj().m_Processor.get_DB().MacroblockIns(rowid);

			LOG_INFO() << "History generated up to height " << h;

			Cleanup();
		}
		else
			LOG_WARNING() << "History generation failed";

	}
}

void Node::Compressor::StopCurrent()
{
	if (!m_hrNew.m_Max)
		return;

	{
		std::unique_lock<std::mutex> scope(m_Mutex);
		m_bStop = true;
	}

	m_Cond.notify_one();

	if (m_Link.m_Thread.joinable())
		m_Link.m_Thread.join();

	ZeroObject(m_hrNew);
	m_Link.m_pEvt = NULL; // should prevent "spurious" calls
}

void Node::Compressor::Proceed()
{
	try {
		m_bSuccess = ProceedInternal();
	} catch (const std::exception& e) {
		LOG_WARNING() << e.what();
	}

	if (!(m_bSuccess || m_bStop))
		LOG_WARNING() << "History generation failed";

	ZeroObject(m_hrInplaceRequest);
	m_Link.m_pEvt->post();
}

bool Node::Compressor::ProceedInternal()
{
	assert(m_hrNew.m_Max);
	const Config::HistoryCompression& cfg = get_ParentObj().m_Cfg.m_HistoryCompression;

	std::vector<HeightRange> v;

	uint32_t i = 0;
	for (Height hPos = m_hrNew.m_Min; hPos < m_hrNew.m_Max; i++)
	{
		HeightRange hr;
		hr.m_Min = hPos + 1; // convention is boundary-inclusive, whereas m_hrNew excludes min bound
		hr.m_Max = std::min(hPos + cfg.m_Naggling, m_hrNew.m_Max);

		{
			std::unique_lock<std::mutex> scope(m_Mutex);
			m_hrInplaceRequest = hr;

			m_Link.m_pEvt->post();

			m_Cond.wait(scope);

			if (m_bStop)
				return false;
		}

		v.push_back(hr);
		hPos = hr.m_Max;

		for (uint32_t j = i; 1 & j; j >>= 1)
			SquashOnce(v);
	}

	while (v.size() > 1)
		SquashOnce(v);

	if (m_hrNew.m_Min >= Rules::HeightGenesis)
	{
		Block::Body::RW rw, rwSrc0, rwSrc1;

		FmtPath(rw, m_hrNew.m_Max, &Rules::HeightGenesis);
		FmtPath(rwSrc0, m_hrNew.m_Min, NULL);

		Height h0 = m_hrNew.m_Min + 1;
		FmtPath(rwSrc1, m_hrNew.m_Max, &h0);

		rw.m_bAutoDelete = rwSrc1.m_bAutoDelete = true;

		if (!SquashOnce(rw, rwSrc0, rwSrc1))
			return false;

		rw.m_bAutoDelete = false;
	}

	return true;
}

bool Node::Compressor::SquashOnce(std::vector<HeightRange>& v)
{
	assert(v.size() >= 2);

	HeightRange& hr0 = v[v.size() - 2];
	HeightRange& hr1 = v[v.size() - 1];

	Block::Body::RW rw, rwSrc0, rwSrc1;
	FmtPath(rw, hr1.m_Max, &hr0.m_Min);
	FmtPath(rwSrc0, hr0.m_Max, &hr0.m_Min);
	FmtPath(rwSrc1, hr1.m_Max, &hr1.m_Min);

	hr0.m_Max = hr1.m_Max;
	v.pop_back();

	rw.m_bAutoDelete = rwSrc0.m_bAutoDelete = rwSrc1.m_bAutoDelete = true;

	if (!SquashOnce(rw, rwSrc0, rwSrc1))
		return false;

	rw.m_bAutoDelete = false;
	return true;
}

bool Node::Compressor::SquashOnce(Block::BodyBase::RW& rw, Block::BodyBase::RW& rwSrc0, Block::BodyBase::RW& rwSrc1)
{
	if (!rw.Open(false) ||
		!rwSrc0.Open(true) ||
		!rwSrc1.Open(true))
		std::ThrowIoError();

	if (!rw.CombineHdr(std::move(rwSrc0), std::move(rwSrc1), m_bStop))
		return false;

	if (!rw.Combine(std::move(rwSrc0), std::move(rwSrc1), m_bStop))
		return false;

	return true;
}

struct Node::Beacon::OutCtx
{
	int m_Refs;
	OutCtx() :m_Refs(0) {}

	struct UvRequest
		:public uv_udp_send_t
	{
		IMPLEMENT_GET_PARENT_OBJ(OutCtx, m_Request)
	} m_Request;

	uv_buf_t m_BufDescr;

#pragma pack (push, 1)
	struct Message
	{
		Merkle::Hash m_CfgChecksum;
		PeerID m_NodeID;
		uint16_t m_Port; // in network byte order
	};
#pragma pack (pop)

	Message m_Message; // the message broadcasted

	void Release()
	{
		assert(m_Refs > 0);
		if (!--m_Refs)
			delete this;
	}

	static void OnDone(uv_udp_send_t* req, int status);
};

Node::Beacon::Beacon()
{
	m_bShouldClose = false;
	m_bRcv = false;
	m_pOut = NULL;
}

Node::Beacon::~Beacon()
{
	if (m_bRcv)
		uv_udp_recv_stop(&m_Udp);

	if (m_bShouldClose)
		uv_close((uv_handle_t*) &m_Udp, NULL);

	if (m_pOut)
		m_pOut->Release();
}

uint16_t Node::Beacon::get_Port()
{
	uint16_t nPort = get_ParentObj().m_Cfg.m_BeaconPort;
	return nPort ? nPort : get_ParentObj().m_Cfg.m_Listen.port();
}

void Node::Beacon::Start()
{
	assert(!m_bRcv);

	uv_udp_init(&io::Reactor::get_Current().get_UvLoop(), &m_Udp);
	m_Udp.data = this;

	m_BufRcv.resize(sizeof(OutCtx::Message));

	io::Address addr;
	addr.port(get_Port());

	sockaddr_in sa;
	addr.fill_sockaddr_in(sa);

	m_bShouldClose = true;

	if (uv_udp_bind(&m_Udp, (sockaddr*)&sa, UV_UDP_REUSEADDR)) // should allow multiple nodes on the same machine (for testing)
		std::ThrowIoError();

	if (uv_udp_recv_start(&m_Udp, AllocBuf, OnRcv))
		std::ThrowIoError();

	m_bRcv = true;

	if (uv_udp_set_broadcast(&m_Udp, 1))
		std::ThrowIoError();

	m_pTimer = io::Timer::create(io::Reactor::get_Current().shared_from_this());
	m_pTimer->start(get_ParentObj().m_Cfg.m_BeaconPeriod_ms, true, [this]() { OnTimer(); }); // periodic timer
	OnTimer();
}

void Node::Beacon::OnTimer()
{
	if (!m_pOut)
	{
		m_pOut = new OutCtx;
		m_pOut->m_Refs = 1;

		m_pOut->m_Message.m_CfgChecksum = Rules::get().Checksum;
		m_pOut->m_Message.m_NodeID = get_ParentObj().m_MyPublicID;
		m_pOut->m_Message.m_Port = htons(get_ParentObj().m_Cfg.m_Listen.port());

		m_pOut->m_BufDescr.base = (char*) &m_pOut->m_Message;
		m_pOut->m_BufDescr.len = sizeof(m_pOut->m_Message);
	}
	else
		if (m_pOut->m_Refs > 1)
			return; // send still pending

	io::Address addr;
	addr.port(get_Port());
	addr.ip(INADDR_BROADCAST);

	sockaddr_in sa;
	addr.fill_sockaddr_in(sa);

	m_pOut->m_Refs++;

	int nErr = uv_udp_send(&m_pOut->m_Request, &m_Udp, &m_pOut->m_BufDescr, 1, (sockaddr*) &sa, OutCtx::OnDone);
	if (nErr)
		m_pOut->Release();
}

void Node::Beacon::OutCtx::OnDone(uv_udp_send_t* req, int /* status */)
{
	UvRequest* pVal = (UvRequest*)req;
	assert(pVal);

	pVal->get_ParentObj().Release();
}

void Node::Beacon::OnRcv(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* pSa, unsigned flags)
{
	OutCtx::Message msg;
	if (sizeof(msg) != nread)
		return;

	memcpy(&msg, buf->base, sizeof(msg)); // copy it to prevent (potential) datatype misallignment and etc.

	if (msg.m_CfgChecksum != Rules::get().Checksum)
		return;

	Beacon* pThis = (Beacon*)handle->data;

	if (pThis->get_ParentObj().m_MyPublicID == msg.m_NodeID)
		return;

	io::Address addr(*(sockaddr_in*)pSa);
	addr.port(ntohs(msg.m_Port));

	pThis->get_ParentObj().m_PeerMan.OnPeer(msg.m_NodeID, addr, true);
}

void Node::Beacon::AllocBuf(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	Beacon* pThis = (Beacon*)handle->data;
	assert(pThis);

	buf->base = (char*) &pThis->m_BufRcv.at(0);
	buf->len = sizeof(OutCtx::Message);
}

void Node::PeerMan::OnFlush()
{
	NodeDB& db = get_ParentObj().m_Processor.get_DB();
	NodeDB::Transaction t(db);

	db.PeersDel();

	const PeerMan::RawRatingSet& rs = get_Ratings();

	for (PeerMan::RawRatingSet::const_iterator it = rs.begin(); rs.end() != it; it++)
	{
		const PeerMan::PeerInfo& pi = it->get_ParentObj();

		NodeDB::WalkerPeer::Data d;
		d.m_ID = pi.m_ID.m_Key;
		d.m_Rating = pi.m_RawRating.m_Value;
		d.m_Address = pi.m_Addr.m_Value.u64();
		d.m_LastSeen = pi.m_LastSeen;

		db.PeerIns(d);
	}

	t.Commit();
}

void Node::PeerMan::ActivatePeer(PeerInfo& pi)
{
	PeerInfoPlus& pip = (PeerInfoPlus&)pi;
	if (pip.m_pLive)
		return; //?

	Peer* p = get_ParentObj().AllocPeer(pip.m_Addr.m_Value);
	p->m_pInfo = &pip;
	pip.m_pLive = p;

	p->Connect(pip.m_Addr.m_Value);
	p->m_Port = pip.m_Addr.m_Value.port();
}

void Node::PeerMan::DeactivatePeer(PeerInfo& pi)
{
	PeerInfoPlus& pip = (PeerInfoPlus&)pi;
	if (!pip.m_pLive)
		return; //?

	pip.m_pLive->DeleteSelf(false, proto::NodeConnection::ByeReason::Other);
}

proto::PeerManager::PeerInfo* Node::PeerMan::AllocPeer()
{
	PeerInfoPlus* p = new PeerInfoPlus;
	p->m_pLive = NULL;
	return p;
}

void Node::PeerMan::DeletePeer(PeerInfo& pi)
{
	delete (PeerInfoPlus*)&pi;
}

} // namespace beam
