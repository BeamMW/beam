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
#include "../core/block_rw.h"
#include "../core/fly_client.h" // hdr pack decoding moved there

#include "../p2p/protocol.h"
#include "../p2p/connection.h"

#include "../utility/io/tcpserver.h"
#include "../utility/logger.h"
#include "../utility/logger_checkpoints.h"

#include "../bvm/bvm2.h"

#include "pow/external_pow.h"

namespace beam {

bool Node::SyncStatus::operator == (const SyncStatus& x) const
{
	return
		(m_Done == x.m_Done) &&
		(m_Total == x.m_Total);
}

void Node::SyncStatus::ToRelative(Height hDone0)
{
	std::setmin(hDone0, m_Done); // prevent overflow (though should not happen)

	assert(m_Total); // never 0, accounts at least for treasury
	std::setmin(hDone0, m_Total - 1); // prevent "indefinite" situation where sync status is 0/0

	m_Done -= hDone0;
	m_Total -= hDone0;
}

void Node::RefreshCongestions()
{
	for (TaskSet::iterator it = m_setTasks.begin(); m_setTasks.end() != it; ++it)
		it->m_bNeeded = false;

    m_Processor.EnumCongestions();

    for (TaskList::iterator it = m_lstTasksUnassigned.begin(); m_lstTasksUnassigned.end() != it; )
    {
        Task& t = *(it++);
        if (!t.m_bNeeded)
            DeleteUnassignedTask(t);
    }
}

void Node::UpdateSyncStatus()
{
	SyncStatus stat = m_SyncStatus;
	UpdateSyncStatusRaw();

	if (!(m_SyncStatus == stat) && m_UpdatedFromPeers)
	{
		if (!m_PostStartSynced && (m_SyncStatus.m_Done == m_SyncStatus.m_Total) && !m_Processor.IsFastSync())
		{
			m_PostStartSynced = true;

			LOG_INFO() << "Tx replication is ON";

			for (PeerList::iterator it = m_lstPeers.begin(); m_lstPeers.end() != it; ++it)
			{
				Peer& peer = *it;
				if ((Peer::Flags::Connected & peer.m_Flags) && !(Peer::Flags::Probe & peer.m_Flags))
					peer.SendLogin();
			}

		}

		if (m_Cfg.m_Observer)
			m_Cfg.m_Observer->OnSyncProgress();
	}
}

void Node::UpdateSyncStatusRaw()
{
	Height hTotal = m_Processor.m_Cursor.m_ID.m_Height;
	Height hDoneBlocks = hTotal;
	Height hDoneHdrs = hTotal;

	if (m_Processor.IsFastSync())
		hTotal = m_Processor.m_SyncData.m_Target.m_Height;

	for (TaskSet::iterator it = m_setTasks.begin(); m_setTasks.end() != it; ++it)
	{
		const Task& t = *it;
		if (!t.m_bNeeded)
			continue;
		if (m_Processor.m_Cursor.m_ID.m_Height >= t.m_sidTrg.m_Height)
			continue;

		bool bBlock = t.m_Key.second;
		if (bBlock)
		{
			assert(t.m_Key.first.m_Height);
			// all the blocks up to this had been dloaded
			std::setmax(hTotal, t.m_sidTrg.m_Height);
			std::setmax(hDoneHdrs, t.m_sidTrg.m_Height);
			std::setmax(hDoneBlocks, t.m_Key.first.m_Height - 1);
		}
		else
		{
			if (!t.m_pOwner)
				continue; // don't account for unowned

			std::setmax(hTotal, t.m_sidTrg.m_Height);
			if (t.m_sidTrg.m_Height > t.m_Key.first.m_Height)
				std::setmax(hDoneHdrs, m_Processor.m_Cursor.m_ID.m_Height + t.m_sidTrg.m_Height - t.m_Key.first.m_Height);
		}
	}

	// account for treasury
	hTotal++;

	if (m_Processor.IsTreasuryHandled())
	{
		hDoneBlocks++;
		hDoneHdrs++;
	}

	// corrections
	std::setmax(hDoneHdrs, hDoneBlocks);
	std::setmax(hTotal, hDoneHdrs);

	// consider the timestamp of the tip, upon successful sync it should not be too far in the past
	if (m_Processor.m_Cursor.m_ID.m_Height < Rules::HeightGenesis)
		hTotal++;
	else
	{
		Timestamp ts0_s = m_Processor.m_Cursor.m_Full.m_TimeStamp;
		Timestamp ts1_s = getTimestamp();

		const Timestamp tolerance_s = 60 * 60 * 24 * 2; // 2 days tolerance. In case blocks not created for some reason (mining turned off on testnet or etc.) we still don't want to get stuck in sync mode
		ts0_s += tolerance_s;

		if (ts1_s > ts0_s)
		{
			ts1_s -= ts0_s;

			hTotal++;

			const uint32_t& trg_s = Rules::get().DA.Target_s;
			if (trg_s)
				std::setmax(hTotal, m_Processor.m_Cursor.m_ID.m_Height + ts1_s / trg_s);
		}

	}

	m_SyncStatus.m_Total = hTotal * (SyncStatus::s_WeightHdr + SyncStatus::s_WeightBlock);
	m_SyncStatus.m_Done = hDoneHdrs * SyncStatus::s_WeightHdr + hDoneBlocks * SyncStatus::s_WeightBlock;
}

void Node::DeleteUnassignedTask(Task& t)
{
    assert(!t.m_pOwner && !t.m_nCount);
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

    for (PeerList::iterator it = get_ParentObj().m_lstPeers.begin(); get_ParentObj().m_lstPeers.end() != it; ++it)
    {
        Peer& peer = *it;
        if (peer.m_LoginFlags & proto::LoginFlags::SpreadingTransactions)
            peer.Send(msg);
    }
}

void Node::Bbs::CalcMsgKey(NodeDB::WalkerBbs::Data& d)
{
    ECC::Hash::Processor()
        << d.m_Message
        << d.m_Channel
        >> d.m_Key;
}

uint32_t Node::Bbs::WantedMsg::get_Timeout_ms()
{
    return get_ParentObj().get_ParentObj().m_Cfg.m_Timeout.m_GetBbsMsg_ms;
}

void Node::Bbs::WantedMsg::OnExpired(const KeyType& key)
{
    proto::BbsGetMsg msg;
    msg.m_Key = key;

    for (PeerList::iterator it = get_ParentObj().get_ParentObj().m_lstPeers.begin(); get_ParentObj().get_ParentObj().m_lstPeers.end() != it; ++it)
    {
        Peer& peer = *it;
        if (peer.m_LoginFlags & proto::LoginFlags::Bbs)
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
            m_pTimer = io::Timer::create(io::Reactor::get_Current());

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
        {
            SetTimer();
            break;
        }

        OnExpired(n.m_Key); // should not invalidate our structure
        Delete(n); // will also reschedule the timer
    }
}

void Node::TryAssignTask(Task& t)
{
	// Prioritize w.r.t. rating!
	for (PeerMan::LiveSet::iterator it = m_PeerMan.m_LiveSet.begin(); m_PeerMan.m_LiveSet.end() != it; ++it)
	{
		Peer& p = *it->m_p;
		if (TryAssignTask(t, p))
			return;
	}
}

bool Node::TryAssignTask(Task& t, Peer& p)
{
    if (!p.ShouldAssignTasks())
        return false;

    if (p.m_Tip.m_Height < t.m_Key.first.m_Height)
        return false;

    if (p.m_Tip.m_Height == t.m_Key.first.m_Height)
    {
        if (t.m_Key.first.m_Height)
        {
            Merkle::Hash hv;
            p.m_Tip.get_Hash(hv);

            if (hv != t.m_Key.first.m_Hash)
                return false;
        }
        else
        {
            // treasury
            if (!(Peer::Flags::HasTreasury & p.m_Flags))
                return false;
        }
    }

    if (p.m_setRejected.end() != p.m_setRejected.find(t.m_Key))
        return false;

    // check if the peer currently transfers a block
    uint32_t nBlocks = 0;
	for (TaskList::iterator it = p.m_lstTasks.begin(); p.m_lstTasks.end() != it; ++it)
	{
		if (it->m_Key.second)
			nBlocks++;
	}

	// assign
	if (t.m_Key.second)
	{
		if (m_nTasksPackBody >= m_Cfg.m_MaxConcurrentBlocksRequest)
			return false; // too many blocks requested

		Height hCountExtra = t.m_sidTrg.m_Height - t.m_Key.first.m_Height;

		proto::GetBodyPack msg;

		if (t.m_Key.first.m_Height <= m_Processor.m_SyncData.m_Target.m_Height)
		{
			// fast-sync mode, diluted blocks request.
			msg.m_Top.m_Height = m_Processor.m_SyncData.m_Target.m_Height;
			if (m_Processor.IsFastSync())
				m_Processor.get_DB().get_StateHash(m_Processor.m_SyncData.m_Target.m_Row, msg.m_Top.m_Hash);
			else
				msg.m_Top.m_Hash = Zero; // treasury

			msg.m_CountExtra = m_Processor.m_SyncData.m_Target.m_Height - t.m_Key.first.m_Height;
			msg.m_Height0 = m_Processor.m_SyncData.m_h0;
			msg.m_HorizonLo1 = m_Processor.m_SyncData.m_TxoLo;
			msg.m_HorizonHi1 = m_Processor.m_SyncData.m_Target.m_Height;
		}
		else
		{
			// std blocks request
			msg.m_Top.m_Height = t.m_sidTrg.m_Height;
			m_Processor.get_DB().get_StateHash(t.m_sidTrg.m_Row, msg.m_Top.m_Hash);
			msg.m_CountExtra = hCountExtra;
		}

		p.Send(msg);

		t.m_nCount = std::min(static_cast<uint32_t>(msg.m_CountExtra), m_Cfg.m_BandwidthCtl.m_MaxBodyPackCount) + 1; // just an estimate, the actual num of blocks can be smaller
		m_nTasksPackBody += t.m_nCount;

        t.m_h0 = m_Processor.m_SyncData.m_h0;
        t.m_hTxoLo = m_Processor.m_SyncData.m_TxoLo;
	}
	else
	{
        const uint32_t nMaxHdrRequests = proto::g_HdrPackMaxSize * 2;
		if (m_nTasksPackHdr >= nMaxHdrRequests)
			return false; // too many hdrs requested

        if (nBlocks)
            return false; // don't requests headers from the peer that transfers a block

        uint32_t nPackSize = std::min(proto::g_HdrPackMaxSize, nMaxHdrRequests - m_nTasksPackHdr);

		// try to avoid big overlaps when asking for headers, but also try to ask for as many headers as possible
        // The best guess seems to be based on the diff between current height and target header (to either side)
        // For big sync tasks (including deep reorgs) big packs are ok. 
		Height h0 = m_Processor.m_Cursor.m_Full.m_Height;
        Height dh = (t.m_Key.first.m_Height > h0) ? (t.m_Key.first.m_Height - h0) : (h0 - t.m_Key.first.m_Height);

        if (nPackSize > dh)
            nPackSize = dh ? (uint32_t) dh : 1;

        proto::GetHdrPack msg;
        msg.m_Top = t.m_Key.first;
        msg.m_Count = nPackSize;
        p.Send(msg);

        t.m_nCount = nPackSize;
        m_nTasksPackHdr += nPackSize;
    }

    bool bEmpty = p.m_lstTasks.empty();

    assert(!t.m_pOwner);
    t.m_pOwner = &p;

    m_lstTasksUnassigned.erase(TaskList::s_iterator_to(t));
    p.m_lstTasks.push_back(t);

	PeerManager::TimePoint tp;
	m_PeerMan.m_LiveSet.erase(PeerMan::LiveSet::s_iterator_to(Cast::Up<PeerMan::PeerInfoPlus>(p.m_pInfo)->m_Live));
	m_PeerMan.ResetRatingBoost(*p.m_pInfo);
	m_PeerMan.m_LiveSet.insert(Cast::Up<PeerMan::PeerInfoPlus>(p.m_pInfo)->m_Live);

	t.m_TimeAssigned_ms = tp.get();

    if (bEmpty)
        p.SetTimerWrtFirstTask();

    return true;
}

void Node::Peer::SetTimerWrtFirstTask()
{
	if (m_lstTasks.empty())
	{
		assert(m_pTimerRequest);
		m_pTimerRequest->cancel();
	}
	else
	{
		// TODO - timer w.r.t. rating, i.e. should not exceed much the best avail peer rating

		uint32_t timeout_ms = m_lstTasks.front().m_Key.second ?
			m_This.m_Cfg.m_Timeout.m_GetBlock_ms :
			m_This.m_Cfg.m_Timeout.m_GetState_ms;

		if (!m_pTimerRequest)
			m_pTimerRequest = io::Timer::create(io::Reactor::get_Current());

		m_pTimerRequest->start(timeout_ms, false, [this]() { OnRequestTimeout(); });
	}
}

void Node::Processor::RequestData(const Block::SystemState::ID& id, bool bBlock, const NodeDB::StateID& sidTrg)
{
	Node::Task tKey;
    tKey.m_Key.first = id;
    tKey.m_Key.second = bBlock;

    TaskSet::iterator it = get_ParentObj().m_setTasks.find(tKey);
    if (get_ParentObj().m_setTasks.end() == it)
    {
        LOG_INFO() << "Requesting " << (bBlock ? "block" : "header") << " " << id;

		Node::Task* pTask = new Node::Task;
        pTask->m_Key = tKey.m_Key;
        pTask->m_sidTrg = sidTrg;
		pTask->m_bNeeded = true;
        pTask->m_nCount = 0;
        pTask->m_pOwner = NULL;

        get_ParentObj().m_setTasks.insert(*pTask);
        get_ParentObj().m_lstTasksUnassigned.push_back(*pTask);

        get_ParentObj().TryAssignTask(*pTask);

	}
	else
	{
		Node::Task& t = *it;
		t.m_bNeeded = true;

		if (!t.m_pOwner)
		{
			if (t.m_sidTrg.m_Height < sidTrg.m_Height)
				t.m_sidTrg = sidTrg;

			get_ParentObj().TryAssignTask(t);
		}
	}
}

void Node::Processor::OnPeerInsane(const PeerID& peerID)
{
    // Deleting the insane peer in-place is dangerous, because we may be invoked in its context.
    // Use "async-delete mechanism
    if (!m_pAsyncPeerInsane)
    {
        io::AsyncEvent::Callback cb = [this]() { FlushInsanePeers(); };
        m_pAsyncPeerInsane = io::AsyncEvent::create(io::Reactor::get_Current(), std::move(cb));
    }

    m_lstInsanePeers.push_back(peerID);
    m_pAsyncPeerInsane->get_trigger()();
}

void Node::Processor::FlushInsanePeers()
{
    for (; !m_lstInsanePeers.empty(); m_lstInsanePeers.pop_front())
    {
        bool bCreate = false;
        PeerMan::PeerInfoPlus* pInfo = Cast::Up<PeerMan::PeerInfoPlus>(get_ParentObj().m_PeerMan.Find(m_lstInsanePeers.front(), bCreate));

        if (pInfo)
        {
            Peer* pPeer = pInfo->m_Live.m_p;
            if (pPeer)
                pPeer->DeleteSelf(true, proto::NodeConnection::ByeReason::Ban);
            else
                get_ParentObj().m_PeerMan.Ban(*pInfo);

        }
    }
}

void Node::DeleteOutdated()
{
    Height h = m_Cfg.m_RollbackLimit.m_Max;
    std::setmin(h, Rules::get().MaxRollback);

    if (m_Processor.m_Cursor.m_ID.m_Height > h)
    {
        h = m_Processor.m_Cursor.m_ID.m_Height - h;

        while (!m_TxPool.m_lstOutdated.empty())
        {
            TxPool::Fluff::Element& x = m_TxPool.m_lstOutdated.front().get_ParentObj();
            if (x.m_Hist.m_Height > h)
                break;

            m_TxPool.Delete(x);
        }
    }

	for (TxPool::Fluff::ProfitSet::iterator it = m_TxPool.m_setProfit.begin(); m_TxPool.m_setProfit.end() != it; )
	{
		TxPool::Fluff::Element& x = (it++)->get_ParentObj();
		Transaction& tx = *x.m_pValue;

        uint32_t nBvmCharge = 0;
        if (proto::TxStatus::Ok != m_Processor.ValidateTxContextEx(tx, x.m_Profit.m_Stats.m_Hr, true, nBvmCharge, nullptr, nullptr, nullptr))
        {
            x.m_Hist.m_Height = m_Processor.m_Cursor.m_ID.m_Height;
            m_TxPool.SetState(x, TxPool::Fluff::State::Outdated);
        }
	}

    for (TxPool::Stem::TimeSet::iterator it = m_Dandelion.m_setTime.begin(); m_Dandelion.m_setTime.end() != it; )
    {
        TxPool::Stem::Element& x = (it++)->get_ParentObj();

        uint32_t nBvmCharge = 0;
        uint8_t nStatus = m_Processor.ValidateTxContextEx(*x.m_pValue, x.m_Profit.m_Stats.m_Hr, true, nBvmCharge, nullptr, nullptr, nullptr);
        if (proto::TxStatus::Ok != nStatus)
        {
            LogTxStem(*x.m_pValue, "out-1");
            m_Dandelion.Delete(x);
        }
    }

    if (m_Processor.m_Cursor.m_ID.m_Height >= m_Cfg.m_Dandelion.m_dhStemConfirm)
    {
        h = m_Processor.m_Cursor.m_ID.m_Height - m_Cfg.m_Dandelion.m_dhStemConfirm;

        while (!m_TxPool.m_lstWaitFluff.empty())
        {
            auto& c = m_TxPool.m_lstWaitFluff.front();
            if (c.m_Height >= h)
                break;

            auto& x = c.get_ParentObj();

            uint32_t nBvmCharge = 0;
            uint8_t nStatus = m_Processor.ValidateTxContextEx(*x.m_pValue, x.m_Profit.m_Stats.m_Hr, true, nBvmCharge, nullptr, nullptr, nullptr);
            if (proto::TxStatus::Ok == nStatus)
            {
                LogTxStem(*x.m_pValue, "auto-fluffing");

                m_TxPool.SetState(x, TxPool::Fluff::State::Fluffed);
                OnTransactionFluff(x, nullptr);
            }
            else
            {
                LogTxStem(*x.m_pValue, "out-2");
                m_TxPool.Delete(x);
            }
        }
    }
}


void Node::Processor::OnNewState()
{
    m_Cwp.Reset();

	if (!IsTreasuryHandled())
        return;

    LOG_INFO() << "My Tip: " << m_Cursor.m_ID << ", Work = " << Difficulty::ToFloat(m_Cursor.m_Full.m_ChainWork);

	if (IsFastSync())
		return;

    get_ParentObj().m_TxReject.clear();
    get_ParentObj().DeleteOutdated(); // Better to delete all irrelevant txs explicitly, even if the node is supposed to mine
    // because in practice mining could be OFF (for instance, if miner key isn't defined, and owner wallet is offline).
    get_ParentObj().m_TxDependent.Clear();

    if (get_ParentObj().m_Miner.IsEnabled())
    {
        get_ParentObj().m_Miner.HardAbortSafe();
        get_ParentObj().m_Miner.SetTimer(0, true); // async start mining
    }

    proto::NewTip msg;
    msg.m_Description = m_Cursor.m_Full;

    for (PeerList::iterator it = get_ParentObj().m_lstPeers.begin(); get_ParentObj().m_lstPeers.end() != it; ++it)
    {
        Peer& peer = *it;
        if (!(Peer::Flags::Connected & peer.m_Flags))
            continue;

		if (msg.m_Description.m_Height >= Rules::HeightGenesis)
		{
			if (!NodeProcessor::IsRemoteTipNeeded(msg.m_Description, peer.m_Tip))
				continue;
		}
		else
		{
			if (Peer::Flags::HasTreasury & peer.m_Flags)
				continue;
		}

        peer.Send(msg);
    }

    get_ParentObj().RefreshCongestions();

	IObserver* pObserver = get_ParentObj().m_Cfg.m_Observer;
	if (pObserver)
		pObserver->OnStateChanged();

	get_ParentObj().MaybeGenerateRecovery();
}

void Node::Processor::OnFastSyncSucceeded()
{
    if (m_vAccounts.empty())
        return;

    for (auto& acc : m_vAccounts)
    {
        acc.m_hTxoHi = m_Extra.m_TxoHi;
        get_DB().SetAccountTxoHi(acc);
    }

    for (PeerList::iterator it = get_ParentObj().m_lstPeers.begin(); get_ParentObj().m_lstPeers.end() != it; ++it)
    {
        Peer& peer = *it;
        peer.m_Flags &= ~Peer::Flags::SerifSent;
        peer.MaybeSendSerif();
    }
}

void Node::MaybeGenerateRecovery()
{
	if (!m_PostStartSynced || m_Cfg.m_Recovery.m_sPathOutput.empty() || !m_Cfg.m_Recovery.m_Granularity)
		return;

	Height h0 = m_Processor.get_DB().ParamIntGetDef(NodeDB::ParamID::LastRecoveryHeight);
	const Height& h1 = m_Processor.m_Cursor.m_ID.m_Height; // alias
	if (h1 < h0 + m_Cfg.m_Recovery.m_Granularity)
		return;

	LOG_INFO() << "Generating recovery...";

	std::ostringstream os;
	os
		<< m_Cfg.m_Recovery.m_sPathOutput
		<< m_Processor.m_Cursor.m_ID;

	std::string sPath = os.str();

	std::string sTmp = sPath;
	sTmp += ".tmp";

	bool bOk = GenerateRecoveryInfo(sTmp.c_str());
	if (bOk)
	{
#ifdef WIN32
		bOk =
			MoveFileExW(Utf8toUtf16(sTmp.c_str()).c_str(), Utf8toUtf16(sPath.c_str()).c_str(), MOVEFILE_REPLACE_EXISTING) ||
			(GetLastError() == ERROR_FILE_NOT_FOUND);
#else // WIN32
		bOk =
			!rename(sTmp.c_str(), sPath.c_str()) ||
			(ENOENT == errno);
#endif // WIN32
	}

	if (bOk) {
		LOG_INFO() << "Recovery generation done";
		m_Processor.get_DB().ParamIntSet(NodeDB::ParamID::LastRecoveryHeight, h1);
	} else
	{
		LOG_INFO() << "Recovery generation failed";
		beam::DeleteFile(sTmp.c_str());
	}
}

void Node::Processor::OnRolledBack()
{
    LOG_INFO() << "Rolled back to: " << m_Cursor.m_ID;

	TxPool::Fluff& txp = get_ParentObj().m_TxPool;
    while (!txp.m_lstOutdated.empty())
    {
        TxPool::Fluff::Element& x = txp.m_lstOutdated.back().get_ParentObj();
        if (x.m_Hist.m_Height <= m_Cursor.m_ID.m_Height)
            break;

        txp.SetState(x, TxPool::Fluff::State::Fluffed); // may be deferred by the next loop
    }

	// Shielded txs that referenced shielded outputs which were reverted - must be reprocessed
	for (TxPool::Fluff::ProfitSet::iterator it = txp.m_setProfit.begin(); txp.m_setProfit.end() != it; )
	{
		TxPool::Fluff::Element& x = (it++)->get_ParentObj();
        if (!IsShieldedInPool(*x.m_pValue))
        {
            get_ParentObj().OnTransactionDeferred(std::move(x.m_pValue), nullptr, nullptr, true);
            get_ParentObj().m_TxPool.Delete(x);
        }
	}

	TxPool::Stem& txps = get_ParentObj().m_Dandelion;
	for (TxPool::Stem::TimeSet::iterator it = txps.m_setTime.begin(); txps.m_setTime.end() != it; )
	{
		TxPool::Stem::Element& x = (it++)->get_ParentObj();
		if (!IsShieldedInPool(*x.m_pValue))
			txps.Delete(x);
	}

    while (!txp.m_lstWaitFluff.empty())
    {
        TxPool::Fluff::Element& x = txp.m_lstWaitFluff.back().get_ParentObj();
        if (x.m_Hist.m_Height <= m_Cursor.m_ID.m_Height)
            break;

        txp.Delete(x); // never mind
    }

    get_ParentObj().m_TxDependent.Clear();

	IObserver* pObserver = get_ParentObj().m_Cfg.m_Observer;
	if (pObserver)
		pObserver->OnRolledBack(m_Cursor.m_ID);
}

void Node::Processor::MyExecutorMT::RunThread(uint32_t iThread)
{
    MyExecutor::MyContext ctx;
    ctx.m_iThread = iThread;
    ECC::InnerProduct::BatchContext::Scope scope(ctx.m_BatchCtx);

    RunThreadCtx(ctx);
}

void Node::Processor::OnModified()
{
    if (!m_bFlushPending)
    {
        if (!m_pFlushTimer)
            m_pFlushTimer = io::Timer::create(io::Reactor::get_Current());

        m_pFlushTimer->start(50, false, [this]() { OnFlushTimer(); });

        m_bFlushPending = true;
    }
}

void Node::Processor::TryGoUpAsync()
{
	if (!m_bGoUpPending)
	{
		if (!m_pGoUpTimer)
			m_pGoUpTimer = io::Timer::create(io::Reactor::get_Current());

		m_pGoUpTimer->start(0, false, [this]() { OnGoUpTimer(); });

		m_bGoUpPending = true;
	}
}

void Node::Processor::OnGoUpTimer()
{
	m_bGoUpPending = false;
	TryGoUp();
	get_ParentObj().RefreshCongestions();
	get_ParentObj().UpdateSyncStatus();
}

void Node::Processor::Stop()
{
    m_ExecutorMT.Stop();
    m_bGoUpPending = false;
    m_bFlushPending = false;

    if (m_pGoUpTimer)
    {
        m_pGoUpTimer->cancel();
    }

    if (m_pFlushTimer)
    {
        m_pFlushTimer->cancel();
    }
}

Height Node::Processor::get_MaxAutoRollback()
{
    Height h = NodeProcessor::get_MaxAutoRollback();
    if (m_Cursor.m_Full.m_Height >= Rules::HeightGenesis)
    {
        Timestamp ts_s = getTimestamp();
        if (ts_s < m_Cursor.m_Full.m_TimeStamp + get_ParentObj().m_Cfg.m_RollbackLimit.m_TimeoutSinceTip_s)
            std::setmin(h, get_ParentObj().m_Cfg.m_RollbackLimit.m_Max);
    }
    return h;
}

void Node::Processor::OnEvent(Height h, const proto::Event::Base& evt)
{
	if (get_ParentObj().m_Cfg.m_LogEvents)
	{
        std::ostringstream os;
        os << "Event Height=" << h << ", ";
        evt.Dump(os);

        LOG_INFO() << os.str();
	}
}

void Node::Processor::OnDummy(const CoinID& cid, Height)
{
	NodeDB& db = get_DB();
	if (db.GetDummyHeight(cid) != MaxHeight)
		return;

	// recovered
	Height h = get_ParentObj().SampleDummySpentHeight();
	h += get_ParentObj().m_Cfg.m_Dandelion.m_DummyLifetimeHi * 2; // add some factor, to make sure the original creator node will spent it before us (if it's still running)

	db.InsertDummy(h, cid);
}

void Node::Processor::InitializeUtxosProgress(uint64_t done, uint64_t total)
{
    auto& node = get_ParentObj();

    if (node.m_Cfg.m_Observer)
        node.m_Cfg.m_Observer->InitializeUtxosProgress(done, total);   
}

void Node::Processor::OnFlushTimer()
{
    m_bFlushPending = false;
    CommitDB();
}

void Node::Processor::FlushDB()
{
    if (m_bFlushPending)
    {
        assert(m_pFlushTimer);
        m_pFlushTimer->cancel();

        OnFlushTimer();
    }
}

void Node::Processor::OnInvalidBlock(const Block::SystemState::Full& s, const Block::Body& block)
{
    if (!get_ParentObj().m_Miner.IsEnabled())
        return;

    // format path
    std::string sPath = get_ParentObj().m_Cfg.m_sPathLocal;
    {
        for (uint32_t i = (uint32_t) sPath.size(); ; )
        {
            if (!i--)
            {
                sPath.clear();
                break;
            }

            if (('/' == sPath[i]) || ('\\' == sPath[i]))
            {
                sPath.resize(i + 1);
                break;
            }
        }

        Merkle::Hash hv;
        s.get_Hash(hv);

        std::ostringstream os;
        os << s.m_Height << '-' << hv << ".inv_block";
        sPath += os.str();
    }

    std::FStream fs;
    if (fs.Open(sPath.c_str(), false))
    {
        Serializer ser;
        ser & s;
        ser & block;

        fs.write(ser.buffer().first, ser.buffer().second);
    }
}

Node::Peer* Node::AllocPeer(const beam::io::Address& addr)
{
    Peer* pPeer = new Peer(*this);
    m_lstPeers.push_back(*pPeer);

	pPeer->m_UnsentHiMark = m_Cfg.m_BandwidthCtl.m_Drown;
    pPeer->m_pInfo = NULL;
    pPeer->m_Flags = 0;
    pPeer->m_Port = 0;
    ZeroObject(pPeer->m_Tip);
    pPeer->m_RemoteAddr = addr;
	pPeer->m_CursorBbs = std::numeric_limits<int64_t>::max();
	pPeer->m_pCursorTx = nullptr;

    LOG_VERBOSE() << "+Peer " << addr;

    return pPeer;
}

void Node::Keys::InitSingleKey(const ECC::uintBig& seed)
{
    Key::IKdf::Ptr pKdf;
    ECC::HKdf::Create(pKdf, seed);
    SetSingleKey(pKdf);
}

void Node::Keys::SetSingleKey(const Key::IKdf::Ptr& pKdf)
{
    m_nMinerSubIndex = 0;
    m_pMiner = pKdf;
    m_pGeneric = pKdf;
    m_pOwner = pKdf;

    if (CoinID(Zero).get_ChildKdfIndex(m_nMinerSubIndex))
        m_pMiner = MasterKey::get_Child(*pKdf, m_nMinerSubIndex);
}

void Node::Initialize(IExternalPOW* externalPOW)
{
    if (m_Cfg.m_VerificationThreads < 0)
        // use all the cores, don't subtract 'mining threads'. Verification has higher priority
        m_Cfg.m_VerificationThreads = m_Processor.m_ExecutorMT.get_Threads();

    m_Processor.m_ExecutorMT.set_Threads(std::max<uint32_t>(m_Cfg.m_VerificationThreads, 1U));

    m_Processor.m_Horizon = m_Cfg.m_Horizon;
    m_Processor.Initialize(m_Cfg.m_sPathLocal.c_str(), m_Cfg.m_ProcessorParams);

	if (m_Cfg.m_ProcessorParams.m_EraseSelfID)
	{
		m_Processor.get_DB().ParamSet(NodeDB::ParamID::MyID, nullptr, nullptr);
		LOG_INFO() << "Node ID erased";

		io::Reactor::get_Current().stop();
		return;
	}

    InitKeys();
    InitIDs();

    LOG_INFO() << "Node ID=" << m_MyPublicID;
    LOG_INFO() << "Initial Tip: " << m_Processor.m_Cursor.m_ID;
	LOG_INFO() << "Tx replication is OFF";

	if (!m_Cfg.m_Treasury.empty() && !m_Processor.IsTreasuryHandled()) {
		// stupid compiler insists on parentheses here!
		m_Processor.OnTreasury(Blob(m_Cfg.m_Treasury));
	}

    RefreshAccounts();

	ZeroObject(m_SyncStatus);
    RefreshCongestions();

    if (m_Cfg.m_Listen.port())
    {
        m_Server.Listen(m_Cfg.m_Listen);
        if (m_Cfg.m_BeaconPeriod_ms)
            m_Beacon.Start();
    }

    m_PeerMan.Initialize();
    m_Miner.Initialize(externalPOW);
	m_Processor.get_DB().get_BbsTotals(m_Bbs.m_Totals);
    m_Bbs.Cleanup();
	m_Bbs.m_HighestPosted_s = m_Processor.get_DB().get_BbsMaxTime();

    if (m_Cfg.m_TestMode.m_FakePowSolveTime_ms && Rules::get().FakePoW)
        m_PostStartSynced = true;

}

uint32_t Node::get_AcessiblePeerCount() const
{
	return static_cast<uint32_t>(m_PeerMan.get_Addrs().size());
}

const PeerManager::AddrSet& Node::get_AcessiblePeerAddrs() const
{
    return m_PeerMan.get_Addrs();
}

void Node::InitKeys()
{
	if (m_Keys.m_pOwner)
	{
		// Ensure the miner key makes sense.
		if (m_Keys.m_pMiner && m_Keys.m_nMinerSubIndex && m_Keys.m_pOwner->IsSame(*m_Keys.m_pMiner))
		{
			// BB2.1
			CorruptionException exc;
			exc.m_sErr = "Incompatible miner key. Please regenerate with the latest version";
			throw exc;

		}
	}
	else
        m_Keys.m_pMiner = nullptr; // can't mine without owner view key, because it's used for Tagging

    if (!m_Keys.m_pGeneric)
    {
        if (m_Keys.m_pMiner)
            m_Keys.m_pGeneric = m_Keys.m_pMiner;
        else
        {
            // use arbitrary, inited from system random. Needed for misc things, such as secure channel, decoys and etc.
            ECC::NoLeak<ECC::uintBig> seed;
            ECC::GenRandom(seed.V);
            ECC::HKdf::Create(m_Keys.m_pGeneric, seed.V);
        }
    }
}

void Node::InitIDs()
{
    ECC::GenRandom(m_NonceLast.V);

    ECC::NoLeak<ECC::Scalar> s;
    Blob blob(s.V.m_Value);
    bool bNewID =
		m_Cfg.m_ProcessorParams.m_ResetSelfID ||
		!m_Processor.get_DB().ParamGet(NodeDB::ParamID::MyID, NULL, &blob);

    if (bNewID)
    {
        NextNonce(m_MyPrivateID);
        s.V = m_MyPrivateID;
        m_Processor.get_DB().ParamSet(NodeDB::ParamID::MyID, NULL, &blob);
    }
    else
        m_MyPrivateID = s.V;

    m_MyPublicID.FromSk(m_MyPrivateID);
}

struct Node::AccountRefreshCtx
{
    AccountRefreshCtx(Node& n) :m_This(n) {}

    Node& m_This;
    std::map<Merkle::Hash, size_t> m_Map;

    BlobEncoder m_Coder;

    void AddAccount(const Key::IPKdf::Ptr&, Key::IPKdf*);
    void InsertAccount(const NodeProcessor::Account&);

    void get_AccountOwnerID(Merkle::Hash&, const Key::IPKdf::Ptr& pOwner, Key::IPKdf* pMiner);
};

void Node::AccountRefreshCtx::get_AccountOwnerID(Merkle::Hash& hv, const Key::IPKdf::Ptr& pOwner, Key::IPKdf* pMiner)
{
    ECC::Hash::Processor hp;
    hp << uint32_t(4); // change this whenever we change the format of the saved events

    ECC::Hash::Value hv1(Zero);

    ECC::Scalar::Native sk;
    pOwner->DerivePKey(sk, hv1);
    hp << sk;

    if (pMiner)
    {
        // rescan also when miner subkey changes, to recover possible decoys that were rejected earlier
        pMiner->DerivePKey(sk, hv1);
        hp
            << m_This.m_Keys.m_nMinerSubIndex
            << sk;
    }

    hp >> hv;
}


void Node::AccountRefreshCtx::AddAccount(const Key::IPKdf::Ptr& pOwner, Key::IPKdf* pMiner)
{
    auto& accs = m_This.m_Processor.m_vAccounts; // alias

    // check if this account already exists
    if (!m_Map.empty())
    {
        Merkle::Hash hv;
        get_AccountOwnerID(hv, pOwner, pMiner);

        auto it = m_Map.find(hv);
        if (m_Map.end() != it)
        {
            // found!
            auto& acc = accs[it->second];
            m_Map.erase(it);

            acc.m_pOwner = pOwner;

            // save the owner key in DB
            m_This.m_Processor.get_DB().DeleteAccountOnly(acc.m_iAccount);
            InsertAccount(acc);

            return;
        }
    }

    NodeDB::AccountIndex iAccount = 1;

    for (auto& d : accs)
    {
        if (d.m_pOwner && d.m_pOwner->IsSame(*pOwner))
        {
            d.m_pOwner = pOwner; // prefer this instance
            return;
        }

        if (iAccount == d.m_iAccount)
            iAccount = d.m_iAccount + 1;
    }

    // new account
    auto& acc = accs.emplace_back();

    acc.m_iAccount = iAccount;
    acc.m_hTxoHi = m_This.m_Processor.m_Extra.m_TxoHi;
    acc.m_Serif = m_This.NextNonce();

    acc.m_pOwner = pOwner;
    InsertAccount(acc);
}

void Node::AccountRefreshCtx::InsertAccount(const NodeProcessor::Account& acc)
{
    assert(acc.m_pOwner);
    ByteBuffer buf;
    buf.resize(acc.m_pOwner->ExportP(nullptr));

    if (!buf.empty())
    {
        acc.m_pOwner->ExportP(&buf.front());
        m_Coder.Encrypt(buf);
    }


    NodeDB::WalkerAccount::DataPlus d;
    Cast::Down<NodeDB::WalkerAccount::Data>(d) = acc;
    d.m_Owner = buf;

    m_This.m_Processor.get_DB().InsertAccount(d);
}

void Node::RefreshAccounts()
{
    auto& accs = m_Processor.m_vAccounts;
    assert(accs.empty());

    AccountRefreshCtx arc(*this);
    Cast::Reinterpret<ECC::Scalar>(arc.m_Coder.m_hvSecret.V) = m_MyPrivateID;

    // Current accounts
    {
        NodeDB::WalkerAccount wlk;
        for (m_Processor.get_DB().EnumAccounts(wlk); wlk.MoveNext(); )
        {
            auto& acc = accs.emplace_back();
            Cast::Down<NodeDB::WalkerAccount::Data>(acc) = wlk.m_Data;

            static_assert(sizeof(Merkle::Hash) < sizeof(ECC::HKdfPub::Packed), "");

            auto& key = wlk.m_Data.m_Owner; // alias

            if (sizeof(Merkle::Hash) == key.n)
            {
                // older-format account. Would be upgraded on 1st run
                const auto& hv = *reinterpret_cast<const Merkle::Hash*>(key.p);
                arc.m_Map[hv] = accs.size() - 1;
            }
            else
            {
                if (arc.m_Coder.Decrypt(key) && (sizeof(ECC::HKdfPub::Packed) == key.n))
                {
                    const auto& p = *reinterpret_cast<const ECC::HKdfPub::Packed*>(key.p);

                    auto pPKdf = std::make_shared<ECC::HKdfPub>();
                    if (pPKdf->Import(p))
                        acc.m_pOwner = std::move(pPKdf); // loaded ok

                    ECC::SecureErase(Cast::NotConst(key.p), key.n);
                }
            }
        }
    }

    uint32_t nAdd = static_cast<uint32_t>(accs.size());

    if (m_Keys.m_pOwner)
        arc.AddAccount(m_Keys.m_pOwner, m_Keys.m_pMiner.get());

    for (const auto& pExtra : m_Keys.m_Accounts.m_vAdd)
        arc.AddAccount(pExtra, nullptr);

    nAdd = static_cast<uint32_t>(accs.size()) - nAdd; // how many new added

    uint32_t nDel = 0;
    for (auto& acc : accs)
    {
        // filter-out those to be deleted.
        // Never delete the current owner account
        if (acc.m_pOwner && acc.m_pOwner.get() != m_Keys.m_pOwner.get())
        {
            bool bDelete = m_Keys.m_Accounts.m_Del.m_All;
            if (!bDelete && !m_Keys.m_Accounts.m_Del.m_Eps.empty())
            {
                auto it = m_Keys.m_Accounts.m_Del.m_Eps.find(acc.get_Endpoint());
                if (m_Keys.m_Accounts.m_Del.m_Eps.end() != it)
                {
                    m_Keys.m_Accounts.m_Del.m_Eps.erase(it);
                    bDelete = true;
                }
            }

            if (bDelete)
                acc.m_pOwner.reset();
        }

        if (acc.m_pOwner)
            acc.InitFromOwner();
        else
            nDel++;
    }
    // delete unused accounts
    if (nDel)
    {
        assert(nDel <= accs.size());
        LOG_INFO() << "Owned accounts removed: " << nDel;

        size_t iDst = 0;
        for (size_t iSrc = 0; iSrc < accs.size(); iSrc++)
        {
            auto& acc = accs[iSrc];
            if (acc.m_pOwner)
            {
                if (iDst != iSrc)
                    accs[iDst] = std::move(acc);
                iDst++;
            }
            else
                m_Processor.get_DB().DeleteAccountWithEvents(acc.m_iAccount);
        }

        assert(iDst + nDel == accs.size());
        accs.resize(iDst);
    }

    if (nAdd)
    {
        assert(nAdd <= accs.size());
        LOG_INFO() << "Owned accounts added: " << nAdd;

        try
        {
            m_Processor.RescanAccounts(nAdd, m_Cfg.m_Observer ? m_Cfg.m_Observer->GetLongActionHandler() : nullptr);
        }
        catch (const std::runtime_error&)
        {
            m_Processor.RollbackDB();
            throw;
        }
    }

    std::ostringstream os;
    os << "Owned accounts :" << std::endl;

    for (const auto& acc : accs)
        os << '\t' << acc.get_Endpoint() << std::endl;

    LOG_INFO() << os.str();
}

bool Node::Bbs::IsInLimits() const
{
	const NodeDB::BbsTotals& lims = get_ParentObj().m_Cfg.m_Bbs.m_Limit;

	return
		(m_Totals.m_Count <= lims.m_Count) &&
		(m_Totals.m_Size <= lims.m_Size);
}

void Node::Bbs::Cleanup()
{
	NodeDB& db = get_ParentObj().m_Processor.get_DB();
	NodeDB::WalkerBbsTimeLen wlk;

	Timestamp ts = getTimestamp() - get_ParentObj().m_Cfg.m_Bbs.m_MessageTimeout_s;

	for (db.EnumAllBbs(wlk); wlk.MoveNext(); )
	{
		if (IsInLimits() && (wlk.m_Time >= ts))
			break;

		db.BbsDel(wlk.m_ID);
		m_Totals.m_Count--;
		m_Totals.m_Size -= wlk.m_Size;
	}

	m_LastCleanup_ms = GetTime_ms();
}

void Node::Bbs::MaybeCleanup()
{
	if (IsInLimits())
	{
		uint32_t dt_ms = GetTime_ms() - m_LastCleanup_ms;
		if (dt_ms < get_ParentObj().m_Cfg.m_Bbs.m_CleanupPeriod_ms)
			return;
	}
	Cleanup();
}

Node::~Node()
{
    LOG_INFO() << "Node stopping...";

    m_Miner.HardAbortSafe();
	if (m_Miner.m_External.m_pSolver)
		m_Miner.m_External.m_pSolver->stop();

    for (size_t i = 0; i < m_Miner.m_vThreads.size(); i++)
    {
        PerThread& pt = m_Miner.m_vThreads[i];
        if (pt.m_pReactor)
            pt.m_pReactor->stop();

        if (pt.m_Thread.joinable())
            pt.m_Thread.join();
    }
    m_Miner.m_vThreads.clear();

    for (PeerList::iterator it = m_lstPeers.begin(); m_lstPeers.end() != it; ++it)
        it->m_LoginFlags = 0; // prevent re-assigning of tasks in the next loop

    while (!m_lstPeers.empty())
        m_lstPeers.front().DeleteSelf(false, proto::NodeConnection::ByeReason::Stopping);

    while (!m_lstTasksUnassigned.empty())
        DeleteUnassignedTask(m_lstTasksUnassigned.front());

    assert(m_setTasks.empty());

	m_Processor.Stop();

	if (!std::uncaught_exceptions() && m_Processor.get_DB().IsOpen())
		m_PeerMan.OnFlush();

    LOG_INFO() << "Node stopped";
}

void Node::Peer::OnRequestTimeout()
{
	assert(Flags::Connected & m_Flags);
	assert(!m_lstTasks.empty());

	LOG_WARNING() << "Peer " << m_RemoteAddr << " request timeout";

	if (m_pInfo)
		ModifyRatingWrtData(0); // task (request) wasn't handled in time.

    DeleteSelf(false, ByeReason::Timeout);
}

void Node::Peer::OnResendPeers()
{
    PeerMan& pm = m_This.m_PeerMan;
    const PeerMan::RawRatingSet& rs = pm.get_Ratings();
    uint32_t nRemaining = pm.m_Cfg.m_DesiredHighest;

    for (PeerMan::RawRatingSet::const_iterator it = rs.begin(); nRemaining && (rs.end() != it); ++it)
    {
        const PeerMan::PeerInfo& pi = it->get_ParentObj();
        if ((Flags::PiRcvd & m_Flags) && (&pi == m_pInfo))
            continue; // skip

		if (!pi.m_RawRating.m_Value)
			continue; // banned

		if (getTimestamp() - pi.m_LastSeen > pm.m_Cfg.m_TimeoutRecommend_s)
			continue; // not seen for a while

		if (pi.m_Addr.m_Value.empty())
			continue; // address unknown, can't recommend

        proto::PeerInfo msg;
        msg.m_ID = pi.m_ID.m_Key;
        msg.m_LastAddr = pi.m_Addr.m_Value;
        Send(msg);

		nRemaining--;
    }
}

void Node::Peer::GenerateSChannelNonce(ECC::Scalar::Native& nonce)
{
    m_This.NextNonce(nonce);
}

void Node::Peer::OnTrafic(uint8_t msgCode, uint32_t msgSize, bool bOut)
{
    if (m_This.m_Cfg.m_LogTraficUsage)
        std::cout << "** " << (bOut ? "<-" : "->") << " " << m_RemoteAddr << " Size=" << msgSize << ", Msg=" << static_cast<uint32_t>(msgCode) << '\n';
}

void Node::Peer::OnConnectedSecure()
{
    LOG_VERBOSE() << "Peer " << m_RemoteAddr << " Connected";

    m_Flags |= Flags::Connected;

    if (!(Flags::Accepted & m_Flags) && m_This.m_Cfg.m_Listen.port())
    {
        // we've connected to the peer, let it now know our port
        proto::PeerInfoSelf msgPi;
        msgPi.m_Port = m_This.m_Cfg.m_Listen.port();
        Send(msgPi);
    }

    if (Flags::Probe & m_Flags)
        return; // just wait for peer to send its ID

    ProveID(m_This.m_MyPrivateID, proto::IDType::Node);

	SendLogin();

    if (m_This.m_Processor.IsTreasuryHandled() && !m_This.m_Processor.IsFastSync())
    {
        proto::NewTip msg;
        msg.m_Description = m_This.m_Processor.m_Cursor.m_Full;
        Send(msg);
    }
}

void Node::Peer::SetupLogin(proto::Login& msg)
{
	msg.m_Flags |= proto::LoginFlags::SendPeers; // request a another node to periodically send a list of recommended peers

	if (m_This.m_PostStartSynced)
		msg.m_Flags |= proto::LoginFlags::SpreadingTransactions; // indicate ability to receive and broadcast transactions

	if (m_This.m_Cfg.m_Bbs.IsEnabled())
		msg.m_Flags |= proto::LoginFlags::Bbs; // indicate ability to receive and broadcast BBS messages
}

Height Node::Peer::get_MinPeerFork()
{
	return m_This.m_Processor.m_Cursor.m_ID.m_Height + 1;
}

void Node::Peer::OnMsg(proto::Authentication&& msg)
{
    proto::NodeConnection::OnMsg(std::move(msg));
    LOG_VERBOSE() << "Peer " << m_RemoteAddr << " Auth. Type=" << msg.m_IDType << ", ID=" << msg.m_ID;

    if (proto::IDType::Owner == msg.m_IDType)
    {
        bool b = ShouldFinalizeMining();

        for (const auto& acc : m_This.m_Processor.m_vAccounts)
        {
            if (IsKdfObscured(*acc.m_pOwner, msg.m_ID))
            {
                m_Flags |= Flags::Owner | Flags::Viewer;
                m_pAccount = &acc;
                ProvePKdfObscured(*acc.m_pOwner, proto::IDType::Viewer);
                break;
            }
        }

        if (!b && ShouldFinalizeMining())
            m_This.m_Miner.OnFinalizerChanged(this);
    }

	if (proto::IDType::Viewer == msg.m_IDType)
	{
        for (const auto& acc : m_This.m_Processor.m_vAccounts)
        {
            if (IsPKdfObscured(*acc.m_pOwner, msg.m_ID))
            {
                m_Flags |= Flags::Viewer;
                m_pAccount = &acc;
                ProvePKdfObscured(*acc.m_pOwner, proto::IDType::Viewer);
            }
        }
	}

    MaybeSendSerif();

    if (proto::IDType::Node != msg.m_IDType)
        return;

    if ((Flags::PiRcvd & m_Flags) || (msg.m_ID == Zero))
        ThrowUnexpected();

    m_Flags |= Flags::PiRcvd;
    LOG_VERBOSE() << m_RemoteAddr << " received PI";

    PeerMan& pm = m_This.m_PeerMan; // alias
	PeerManager::TimePoint tp;

    Timestamp ts = getTimestamp();

    if (m_pInfo)
    {
        // probably we connected by the address
        if (m_pInfo->m_ID.m_Key == msg.m_ID)
        {
            assert(!(Flags::Accepted & m_Flags));
            m_pInfo->m_LastSeen = ts;

            TakeTasks();
            return; // all settled (already)
        }

        // detach from it
		PeerMan::PeerInfoPlus& pip = *m_pInfo;
		m_pInfo->DetachStrict();

        if (pip.m_ID.m_Key == Zero)
        {
            LOG_INFO() << "deleted anonymous PI";
            pm.Delete(pip); // it's anonymous.
        }
        else
        {
            LOG_INFO() << "PeerID is different";
            pm.OnActive(pip, false);
            pm.RemoveAddr(pip); // turned-out to be wrong
        }
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


    PeerMan::PeerInfoPlus* pPi = Cast::Up<PeerMan::PeerInfoPlus>(pm.OnPeer(msg.m_ID, addr, bAddrValid));
    assert(pPi);

    if (!(Flags::Accepted & m_Flags))
        pPi->m_LastSeen = ts;

    if (Flags::Probe & m_Flags)
    {
        LOG_INFO() << *pPi << " probed ok";
        DeleteSelf(false, ByeReason::Probed);
        return;
    }

    if (pPi->m_Live.m_p)
    {
        LOG_INFO() << "Duplicate connection with the same PI.";
        // Duplicate connection. In this case we have to choose wether to terminate this connection, or the previous. The best is to do it asymmetrically.
        // We decide this based on our Node IDs.
        // In addition, if the older connection isn't completed yet (i.e. it's our connect attempt) - it's prefered for deletion, because such a connection may be impossible (firewalls and friends).
		Peer* pDup = pPi->m_Live.m_p;

        if (!pDup->IsSecureOut() || (m_This.m_MyPublicID > msg.m_ID))
        {
			// detach from that peer
			pPi->DetachStrict();
            pDup->DeleteSelf(false, ByeReason::Duplicate);
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
	pPi->Attach(*this);
    pm.OnActive(*pPi, true);

    LOG_VERBOSE() << *m_pInfo << " connected, info updated";

    if ((Flags::Accepted & m_Flags) && !pPi->m_Addr.m_Value.empty())
    {
        uint32_t nPingThreshold_s = pm.m_Cfg.m_TimeoutRecommend_s / 3;
        if ((ts - pPi->m_LastSeen > nPingThreshold_s) &&
            (ts - pPi->m_LastConnectAttempt > nPingThreshold_s))
        {
            pPi->m_LastConnectAttempt = ts;

            LOG_VERBOSE() << *m_pInfo << " probing connection in opposite direction";


            Peer* p = m_This.AllocPeer(pPi->m_Addr.m_Value);
            p->m_Flags |= Flags::Probe;
            p->Connect(pPi->m_Addr.m_Value);
            p->m_Port = pPi->m_Addr.m_Value.port();
        }
    }

    TakeTasks();
}

bool Node::Peer::ShouldAssignTasks()
{
    // Current design: don't ask anything from non-authenticated peers
    if (!((Peer::Flags::PiRcvd & m_Flags) && m_pInfo))
        return false;

    return true;
}

bool Node::Peer::ShouldFinalizeMining()
{
    return
        (Flags::Owner & m_Flags) &&
        (proto::LoginFlags::MiningFinalization & m_LoginFlags) &&
        (m_pAccount->m_pOwner == m_This.m_Keys.m_pOwner) && // in multi-account node this is important
        m_This.m_Cfg.m_PreferOnlineMining;
}

void Node::Peer::OnMsg(proto::Bye&& msg)
{
    LOG_VERBOSE() << "Peer " << m_RemoteAddr << " Received Bye." << msg.m_Reason;
	NodeConnection::OnMsg(std::move(msg));
}

void Node::Peer::OnDisconnect(const DisconnectReason& dr)
{
    int nLogLevel = LOG_LEVEL_VERBOSE;

    bool bIsErr = true;
    uint8_t nByeReason = 0;

    switch (dr.m_Type)
    {
    default: assert(false);

    case DisconnectReason::Io:
	case DisconnectReason::Drown:
		break;

    case DisconnectReason::Bye:
        bIsErr = false;
        break;

    case DisconnectReason::ProcessingExc:
        switch (dr.m_ExceptionDetails.m_ExceptionType)
        {
        case proto::NodeProcessingException::Type::TimeOutOfSync:
            if (dr.m_ExceptionDetails.m_ExceptionType == proto::NodeProcessingException::Type::TimeOutOfSync && m_This.m_Cfg.m_Observer)
                m_This.m_Cfg.m_Observer->OnSyncError(IObserver::Error::TimeDiffToLarge);
            break;

        case proto::NodeProcessingException::Type::Incompatible:
            break;

        default:
            nLogLevel = LOG_LEVEL_WARNING;
        }

        nByeReason = ByeReason::Ban;
        break;

    case DisconnectReason::Protocol:
        nLogLevel = LOG_LEVEL_WARNING;
        nByeReason = ByeReason::Ban;
        break;
    }

    LOG_MESSAGE(nLogLevel) << m_RemoteAddr << ": " << dr;

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

    if (t.m_nCount)
    {
        uint32_t& nCounter = t.m_Key.second ? m_This.m_nTasksPackBody : m_This.m_nTasksPackHdr;
        assert(nCounter >= t.m_nCount);

        nCounter -= t.m_nCount;
		t.m_nCount = 0;
    }

    m_lstTasks.erase(TaskList::s_iterator_to(t));
    m_This.m_lstTasksUnassigned.push_back(t);

    if (t.m_bNeeded)
        m_This.TryAssignTask(t);
    else
        m_This.DeleteUnassignedTask(t);
}

void Node::Peer::DeleteSelf(bool bIsError, uint8_t nByeReason)
{
    LOG_VERBOSE() << "-Peer " << m_RemoteAddr;

    if (nByeReason && (Flags::Connected & m_Flags))
    {
        proto::Bye msg;
        msg.m_Reason = nByeReason;
        Send(msg);
    }

    if (this == m_This.m_Miner.m_pFinalizer)
    {
        m_Flags &= ~Flags::Owner;
        m_LoginFlags &= proto::LoginFlags::MiningFinalization;

        assert(!ShouldFinalizeMining());
        m_This.m_Miner.OnFinalizerChanged(NULL);
    }

    m_Tip.m_Height = 0; // prevent reassigning the tasks
    m_Flags &= ~Flags::HasTreasury;

    ReleaseTasks();
    Unsubscribe();

    if (m_pInfo)
    {
		PeerMan::PeerInfoPlus& pip = *m_pInfo;
		m_pInfo->DetachStrict();

		PeerManager& pm = m_This.m_PeerMan; // alias

		pm.OnActive(pip, false);

		if (bIsError)
		{
			if (ByeReason::Ban == nByeReason)
				pm.Ban(pip);
			else
			{
				PeerManager::TimePoint tp;
				uint32_t dt_ms = tp.get() - pip.m_LastActivity_ms;
				if (dt_ms < pm.m_Cfg.m_TimeoutDisconnect_ms)
				{
					uint32_t val =
						(pip.m_RawRating.m_Value > PeerManager::Rating::PenaltyNetworkErr) ?
						(pip.m_RawRating.m_Value - PeerManager::Rating::PenaltyNetworkErr) :
						1;
					pm.SetRating(pip, val);
				}
			}
		}

		if (pm.get_Ratings().size() > pm.m_Cfg.m_DesiredTotal)
		{
			bool bDelete =
				!pip.m_LastSeen || // never seen
				((1 == pip.m_RawRating.m_Value) && pm.IsOutdated(pip)); // lowest rating, not seen for a while

			if (bDelete)
			{
				LOG_VERBOSE() << pip << " Deleted";
				pm.Delete(pip);
			}
		}
	}

	SetTxCursor(nullptr);

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
    if (!ShouldAssignTasks())
        return;

    for (TaskList::iterator it = m_This.m_lstTasksUnassigned.begin(); m_This.m_lstTasksUnassigned.end() != it; )
        m_This.TryAssignTask(*it++, *this);
}

void Node::Peer::OnMsg(proto::Pong&&)
{
	if (!(Flags::Chocking & m_Flags))
		ThrowUnexpected();

	m_Flags &= ~Flags::Chocking;

	// not chocking - continue broadcast
	BroadcastTxs();
	BroadcastBbs();

	for (Bbs::Subscription::PeerSet::iterator it = m_Subscriptions.begin(); m_Subscriptions.end() != it; ++it)
		BroadcastBbs(it->get_ParentObj());
}

void Node::Peer::OnMsg(proto::NewTip&& msg)
{
    if (msg.m_Description.m_ChainWork < m_Tip.m_ChainWork)
        ThrowUnexpected();

    m_Tip = msg.m_Description;
    m_setRejected.clear();
    m_Flags |= Flags::HasTreasury;

    Block::SystemState::ID id;
    m_Tip.get_ID(id);

    Processor& p = m_This.m_Processor;
    bool bTipNeeded = NodeProcessor::IsRemoteTipNeeded(m_Tip, p.m_Cursor.m_Full);

    LOG_MESSAGE(bTipNeeded ? LOG_LEVEL_INFO: LOG_LEVEL_VERBOSE) << "Peer " << m_RemoteAddr << " Tip: " << id; 

    if (!m_pInfo)
        return;

    if (bTipNeeded)
    {
        switch (p.OnState(m_Tip, m_pInfo->m_ID.m_Key))
        {
        case NodeProcessor::DataStatus::Invalid:
            m_Tip.m_TimeStamp > getTimestamp() && m_This.m_Cfg.m_Observer ?
                m_This.m_Cfg.m_Observer->OnSyncError(IObserver::Error::TimeDiffToLarge):
                ThrowUnexpected();
            // no break;

        case NodeProcessor::DataStatus::Accepted:
			// don't give explicit reward for this header. Instead - most likely we'll request this block from that peer, and it'll have a chance to boost its rating
            m_This.RefreshCongestions();
            break; // since we made OnPeerInsane handling asynchronous - no need to return rapidly

        default:
            break; // suppress warning
        }
    }

	TakeTasks();

	if (!m_This.m_UpdatedFromPeers)
	{
		m_This.m_UpdatedFromPeers = true; // at least 1 peer reported actual tip

		ZeroObject(m_This.m_SyncStatus);
		m_This.UpdateSyncStatus();
	}
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

	// Refrain from using TakeTasks(), it will only try to assign tasks to this peer
	m_This.RefreshCongestions();
	m_This.m_Processor.TryGoUpAsync();
}

void Node::Peer::ModifyRatingWrtData(size_t nSize)
{
	PeerManager::TimePoint tp;
	uint32_t dt_ms = tp.get() - get_FirstTask().m_TimeAssigned_ms;

	// Calculate the weighted average of the effective bandwidth.
	// We assume the "previous" bandwidth bw0 was calculated within "previous" window t0, and the total download amount was v0 = t0 * bw0.
	// Hence, after accounting for newly-downloaded data, the average bandwidth becomes:
	// <bw> = (v0 + v1) / (t0 + t1) = (bw0 * t0 + v1) / (t0 + t1)
	//
	const uint32_t t0_s = 3;
	const uint32_t t0_ms = t0_s * 1000;
	uint64_t tTotal_ms = static_cast<uint64_t>(t0_ms) + dt_ms;
	assert(tTotal_ms); // can't overflow

	uint32_t bw0 = PeerManager::Rating::ToBps(m_pInfo->m_RawRating.m_Value);
	uint64_t v = static_cast<uint64_t>(bw0) * t0_s + nSize;

	uint32_t bwAvg = static_cast<uint32_t>(v * 1000 / tTotal_ms);

	uint32_t nRatingAvg = PeerManager::Rating::FromBps(bwAvg);

	m_This.m_PeerMan.m_LiveSet.erase(PeerMan::LiveSet::s_iterator_to(Cast::Up<PeerMan::PeerInfoPlus>(m_pInfo)->m_Live));
	m_This.m_PeerMan.SetRating(*m_pInfo, nRatingAvg);
	m_This.m_PeerMan.m_LiveSet.insert(Cast::Up<PeerMan::PeerInfoPlus>(m_pInfo)->m_Live);
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
        Send(proto::DataMissing());
}

void Node::Peer::OnMsg(proto::GetHdrPack&& msg)
{
    NodeDB::StateID sid;
    sid.m_Height = msg.m_Top.m_Height;
    sid.m_Row = m_This.m_Processor.get_DB().StateFindSafe(msg.m_Top);
    SendHdrs(sid, msg.m_Count);
}

void Node::Peer::OnMsg(proto::EnumHdrs&& msg)
{
    NodeDB::StateID sid;
    uint32_t nCount;

    HeightRange hr(Rules::HeightGenesis, m_This.m_Processor.m_Cursor.m_Full.m_Height);
    hr.Intersect(msg.m_Height);
    if (!hr.IsEmpty())
    {
        Height dh = hr.m_Max - hr.m_Min + 1;
        nCount = (dh > proto::g_HdrPackMaxSize) ? proto::g_HdrPackMaxSize : static_cast<uint32_t>(dh);

        sid.m_Height = hr.m_Max;
        sid.m_Row = m_This.m_Processor.FindActiveAtStrict(hr.m_Max);
    }
    else
    {
        nCount = 0;
        sid.m_Row = 0;
    }

    SendHdrs(sid, nCount);
}

void Node::Peer::SendHdrs(NodeDB::StateID& sid, uint32_t nCount)
{
    if (nCount && sid.m_Row)
    {
        // don't throw unexpected if pack size is bigger than max. In case it'll be increased in future versions - just truncate it.
        std::setmin(nCount, proto::g_HdrPackMaxSize);

        proto::HdrPack msgOut;
        msgOut.m_vElements.reserve(nCount);

        NodeDB& db = m_This.m_Processor.get_DB();

        NodeDB::WalkerSystemState wlk;
        for (db.EnumSystemStatesBkwd(wlk, sid); wlk.MoveNext(); )
        {
            msgOut.m_vElements.push_back(wlk.m_State);

            if (msgOut.m_vElements.size() == nCount)
                break;
        }

        if (!msgOut.m_vElements.empty())
        {
            msgOut.m_Prefix = wlk.m_State;
            Send(msgOut);
            return;
        }
    }

    Send(proto::DataMissing());
}

bool Node::DecodeAndCheckHdrs(std::vector<Block::SystemState::Full>& v, const proto::HdrPack& msg)
{
	if (msg.m_vElements.empty() || (msg.m_vElements.size() > proto::g_HdrPackMaxSize))
		return false;

    // PoW verification is heavy for big packs. Do it in parallel
    Executor::Scope scope(m_Processor.m_ExecutorMT);

    proto::FlyClient::Data::DecodedHdrPack ex;
    if (!ex.DecodeAndCheck(msg))
        return false;

    v = std::move(ex.m_vStates);
    return true;
}

void Node::Peer::OnMsg(proto::HdrPack&& msg)
{
    Task& t = get_FirstTask();

	if (t.m_Key.second || !t.m_nCount) {
		// stupid compiler insists on parentheses here!
		ThrowUnexpected();
	}

	std::vector<Block::SystemState::Full> v;
	if (!m_This.DecodeAndCheckHdrs(v, msg))
        ThrowUnexpected();

	// just to be pedantic
	Block::SystemState::ID idLast;
	v.back().get_ID(idLast);
	if (idLast != t.m_Key.first)
		ThrowUnexpected();

    for (size_t i = 0; i < v.size(); i++)
    {
        NodeProcessor::DataStatus::Enum eStatus = m_This.m_Processor.OnStateSilent(v[i], m_pInfo->m_ID.m_Key, idLast, true);
        switch (eStatus)
        {
        case NodeProcessor::DataStatus::Invalid:
			// though PoW was already tested, header can still be invalid. For instance, due to improper Timestamp
			ThrowUnexpected();
			break;

        case NodeProcessor::DataStatus::Accepted:
			// no break;

        default:
            break; // suppress warning
        }
    }

	LOG_INFO() << "Hdr pack received " << msg.m_Prefix.m_Height << "-" << idLast;

	ModifyRatingWrtData(sizeof(msg.m_Prefix) + msg.m_vElements.size() * sizeof(msg.m_vElements.front()));

	OnFirstTaskDone(NodeProcessor::DataStatus::Accepted);
	m_This.UpdateSyncStatus();
}

void Node::Peer::OnMsg(proto::GetBody&& msg)
{
	proto::GetBodyPack msg2;
	msg2.m_Top = msg.m_ID;
	OnMsg(std::move(msg2));
}

void Node::Peer::OnMsg(proto::GetBodyPack&& msg)
{
	Processor& p = m_This.m_Processor; // alias

    if (msg.m_Top.m_Height)
    {
		NodeDB::StateID sid;
		sid.m_Row = p.get_DB().StateFindSafe(msg.m_Top);
		if (sid.m_Row)
		{
			sid.m_Height = msg.m_Top.m_Height;

			if (msg.m_CountExtra)
			{
				if (sid.m_Height - Rules::HeightGenesis < msg.m_CountExtra)
					ThrowUnexpected();

				if (NodeDB::StateFlags::Active & p.get_DB().GetStateFlags(sid.m_Row))
				{
					// functionality only supported for active states
					proto::BodyPack msgBody;
					size_t nSize = 0;

					sid.m_Height -= msg.m_CountExtra;
					Height hMax = std::min(msg.m_Top.m_Height, sid.m_Height + m_This.m_Cfg.m_BandwidthCtl.m_MaxBodyPackCount);

					for (; sid.m_Height <= hMax; sid.m_Height++)
					{
						sid.m_Row = p.FindActiveAtStrict(sid.m_Height);

						proto::BodyBuffers bb;
						if (!GetBlock(bb, sid, msg, true))
							break;

						nSize += bb.m_Eternal.size() + bb.m_Perishable.size();
						msgBody.m_Bodies.push_back(std::move(bb));

						if (nSize >= m_This.m_Cfg.m_BandwidthCtl.m_MaxBodyPackSize)
							break;
					}

					if (msgBody.m_Bodies.size())
					{
						Send(msgBody);
						return;
					}
				}
			}
			else
			{
				proto::Body msgBody;
				if (GetBlock(msgBody.m_Body, sid, msg, false))
				{
					Send(msgBody);
					return;
				}
			}
		}
    }
    else
    {
        if ((msg.m_Top.m_Hash == Zero) && p.IsTreasuryHandled())
        {
            proto::Body msgBody;
            if (p.get_DB().ParamGet(NodeDB::ParamID::Treasury, NULL, NULL, &msgBody.m_Body.m_Eternal))
            {
                Send(msgBody);
                return;
            }
        }
    }

    Send(proto::DataMissing());
}

bool Node::Peer::GetBlock(proto::BodyBuffers& out, const NodeDB::StateID& sid, const proto::GetBodyPack& msg, bool bActive)
{
	ByteBuffer* pP = nullptr;
	ByteBuffer* pE = nullptr;

	switch (msg.m_FlagE)
	{
	case proto::BodyBuffers::Full:
		pE = &out.m_Eternal;
		// no break;
	case proto::BodyBuffers::None:
		break;
	default:
		ThrowUnexpected();
	}

	switch (msg.m_FlagP)
	{
	case proto::BodyBuffers::Recovery1:
	case proto::BodyBuffers::Full:
		pP = &out.m_Perishable;
		// no break;
	case proto::BodyBuffers::None:
		break;
	default:
		ThrowUnexpected();
	}

	if (!m_This.m_Processor.GetBlock(sid, pE, pP, msg.m_Height0, msg.m_HorizonLo1, msg.m_HorizonHi1, bActive))
		return false;

	if (proto::BodyBuffers::Recovery1 == msg.m_FlagP)
	{
		Block::Body block;

		Deserializer der;
		der.reset(out.m_Perishable);
		der & Cast::Down<Block::BodyBase>(block);
		der & Cast::Down<TxVectors::Perishable>(block);

        Serializer ser;
        ser & block.m_vInputs;
        ser & block.m_vOutputs.size();

        for (size_t i = 0; i < block.m_vOutputs.size(); i++)
        {
            const auto& outp = *block.m_vOutputs[i];
            yas::detail::saveRecovery(ser, outp, sid.m_Height);
        }

		ser & Cast::Down<Block::BodyBase>(block);
		ser & Cast::Down<TxVectors::Perishable>(block);

		ser.swap_buf(out.m_Perishable);
	}

	return true;
}

bool Node::Peer::ShouldAcceptBodyPack()
{
    Task& t = get_FirstTask();
    const NodeProcessor::SyncData& d = m_This.m_Processor.m_SyncData; // alias
    return (t.m_h0 == d.m_h0) && (t.m_hTxoLo == d.m_TxoLo);
}

void Node::Peer::OnMsg(proto::Body&& msg)
{
	Task& t = get_FirstTask();

	if (!t.m_Key.second)
		ThrowUnexpected();

	ModifyRatingWrtData(msg.m_Body.m_Eternal.size() + msg.m_Body.m_Perishable.size());

	const Block::SystemState::ID& id = t.m_Key.first;
	Height h = id.m_Height;

	Processor& p = m_This.m_Processor; // alias

	NodeProcessor::DataStatus::Enum eStatus = h ?
        ShouldAcceptBodyPack() ?
		    p.OnBlock(id, msg.m_Body.m_Perishable, msg.m_Body.m_Eternal, m_pInfo->m_ID.m_Key) :
            NodeProcessor::DataStatus::Rejected :
		p.OnTreasury(msg.m_Body.m_Eternal);

	p.TryGoUpAsync();
	OnFirstTaskDone(eStatus);
}

void Node::Peer::OnMsg(proto::BodyPack&& msg)
{
	Task& t = get_FirstTask();

	if (!t.m_Key.second || !t.m_nCount)
		ThrowUnexpected();

	const Block::SystemState::ID& id = t.m_Key.first;
	Processor& p = m_This.m_Processor;

	assert(t.m_sidTrg.m_Height >= id.m_Height);
	Height hCountExtra = t.m_sidTrg.m_Height - id.m_Height;

	if (msg.m_Bodies.size() > hCountExtra + 1)
		ThrowUnexpected();

	size_t nSize = 0;
	for (size_t i = 0; i < msg.m_Bodies.size(); i++)
	{
		nSize +=
			msg.m_Bodies[i].m_Eternal.size() +
			msg.m_Bodies[i].m_Perishable.size();
	}
	ModifyRatingWrtData(nSize);

	NodeProcessor::DataStatus::Enum eStatus = NodeProcessor::DataStatus::Rejected;
	if (!msg.m_Bodies.empty() && ShouldAcceptBodyPack())
	{
		const uint64_t* pPtr = p.get_CachedRows(t.m_sidTrg, hCountExtra);
		if (pPtr)
		{
			LOG_INFO() << id << " Block pack received " << id.m_Height << "-" << (id.m_Height + msg.m_Bodies.size() - 1);

			eStatus = NodeProcessor::DataStatus::Accepted;

			for (Height h = 0; h < msg.m_Bodies.size(); h++)
			{
				NodeDB::StateID sid;
				sid.m_Row = pPtr[hCountExtra - h];
				sid.m_Height = id.m_Height + h;

				const proto::BodyBuffers& bb = msg.m_Bodies[h];

				NodeProcessor::DataStatus::Enum es2 = p.OnBlock(sid, bb.m_Perishable, bb.m_Eternal, m_pInfo->m_ID.m_Key);
				if (NodeProcessor::DataStatus::Invalid == es2)
				{
					p.OnPeerInsane(m_pInfo->m_ID.m_Key);
					break;
				}
			}
		}
	}

	p.TryGoUpAsync();
	OnFirstTaskDone(eStatus);
}

void Node::Peer::OnFirstTaskDone(NodeProcessor::DataStatus::Enum eStatus)
{
    if (NodeProcessor::DataStatus::Invalid == eStatus)
        ThrowUnexpected();

    get_FirstTask().m_bNeeded = false;
    OnFirstTaskDone();
}

void Node::Peer::OnMsg(proto::NewTransaction&& msg)
{
    if (!msg.m_Transaction)
        ThrowUnexpected(); // our deserialization permits NULL Ptrs.
    // However the transaction body must have already been checked for NULLs

    // Change in protocol!
    // Historically the node replied with proto::Status message for transactions in stem phase only, for fluff phase no reply was necessary.
    // Now we reply regardless to phase, but ONLY to non-node client (means - FlyClient).
    // For other cases the tx verification is deferred, until the node is idle.
    bool bReply = !(Flags::PiRcvd & m_Flags);

    const PeerID* pSender = m_pInfo ? &m_pInfo->m_ID.m_Key : nullptr;

    if (bReply)
    {
        std::ostringstream errInfo;

        proto::Status msgOut;
        msgOut.m_Value = m_This.OnTransaction(std::move(msg.m_Transaction), std::move(msg.m_Context), pSender, msg.m_Fluff, &errInfo);

        msgOut.m_ExtraInfo = errInfo.str();
        Send(msgOut);
    }
    else
    {
        m_This.OnTransactionDeferred(std::move(msg.m_Transaction), std::move(msg.m_Context), pSender, msg.m_Fluff);
    }
}

void Node::OnTransactionDeferred(Transaction::Ptr&& pTx, std::unique_ptr<Merkle::Hash>&& pCtx, const PeerID* pSender, bool bFluff)
{
    TxDeferred::Element txd;
    txd.m_pTx = std::move(pTx);
    txd.m_pCtx = std::move(pCtx);
    txd.m_Fluff = bFluff;

    if (pSender)
        txd.m_Sender = *pSender;
    else
        txd.m_Sender = Zero;

    //if (m_Cfg.m_LogTxFluff)
    //{
    //    Transaction::KeyType key;
    //    txd.m_pTx->get_Key(key);

    //    LOG_INFO() << "Tx " << key << " deferred";
    //}

    if (m_TxDeferred.m_lst.empty())
        m_TxDeferred.start();
    else
    {
        while (m_TxDeferred.m_lst.size() > m_Cfg.m_MaxDeferredTransactions)
            m_TxDeferred.m_lst.pop_front();
    }

    m_TxDeferred.m_lst.push_back(std::move(txd));
}

void Node::TxDeferred::OnSchedule()
{
    if (!m_lst.empty())
    {
        TxDeferred::Element& x = m_lst.front();
        get_ParentObj().OnTransaction(std::move(x.m_pTx), std::move(x.m_pCtx), &x.m_Sender, x.m_Fluff, nullptr);
        m_lst.pop_front();
    }

    if (m_lst.empty())
        cancel();

}

uint8_t Node::OnTransaction(Transaction::Ptr&& pTx, std::unique_ptr<Merkle::Hash>&& pCtx, const PeerID* pSender, bool bFluff, std::ostream* pExtraInfo)
{
    return 
        pCtx ?
            OnTransactionDependent(std::move(pTx), *pCtx, pSender, bFluff, pExtraInfo) :
            bFluff ?
                OnTransactionFluff(std::move(pTx), pExtraInfo, pSender, nullptr) :
                OnTransactionStem(std::move(pTx), pExtraInfo);
}

uint8_t Node::ValidateTx(TxPool::Stats& stats, const Transaction& tx, const Transaction::KeyType& keyTx, std::ostream* pExtraInfo, bool& bAlreadyRejected)
{
    auto it = m_TxReject.find(keyTx);
    if (m_TxReject.end() != it)
    {
        bAlreadyRejected = true;
        return it->second;
    }

    Transaction::Context ctx;
    uint32_t nBvmCharge = 0;
    Amount feeReserve = 0;

    uint8_t nRet = ValidateTx2(ctx, tx, nBvmCharge, feeReserve, nullptr, pExtraInfo, nullptr);
    if (proto::TxStatus::Ok == nRet)
    {
        if (AmountBig::get_Hi(ctx.m_Stats.m_Fee))
            nRet = proto::TxStatus::LowFee; // actually it's ridiculously-high fee
        else
        {
            auto nSizeCorrection = (uint32_t)(((uint64_t)nBvmCharge) * Rules::get().MaxBodySize / bvm2::Limits::BlockCharge);
            stats.From(tx, ctx, feeReserve, nSizeCorrection);
        }
    }

    if (proto::TxStatus::Ok != nRet)
    {
        m_TxReject[keyTx] = nRet;
        m_Wtx.Delete(keyTx);
    }

    return nRet;
}

uint8_t Node::ValidateTx2(Transaction::Context& ctx, const Transaction& tx, uint32_t& nBvmCharge, Amount& feeReserve, TxPool::Dependent::Element* pParent, std::ostream* pExtraInfo, Merkle::Hash* pNewCtx)
{
    ctx.m_Height.m_Min = m_Processor.m_Cursor.m_ID.m_Height + 1;

    std::string sErr;
    bool bValid = m_Processor.ValidateAndSummarize(ctx, tx, tx.get_Reader(), sErr);
    if (bValid)
    {
        try {
            ctx.TestValidTransaction();
        } catch (const std::exception& e) {
            bValid = false;
            sErr = e.what();
        }
    }

    if (!bValid)
    {
        if (pExtraInfo)
            *pExtraInfo << "Context-free validation failed: " << sErr;
        return proto::TxStatus::Invalid;
    }

    uint8_t nCode = m_Processor.ValidateTxContextEx(tx, ctx.m_Height, false, nBvmCharge, pParent, pExtraInfo, pNewCtx);
    if (proto::TxStatus::Ok != nCode)
        return nCode;


    if (!CalculateFeeReserve(ctx.m_Stats, ctx.m_Height, ctx.m_Stats.m_Fee, nBvmCharge, feeReserve))
    {
        if (pExtraInfo)
        {
            *pExtraInfo << "Low fee. Min = " << feeReserve << ", provided = " << AmountBig::get_Lo(ctx.m_Stats.m_Fee);
            if (nBvmCharge)
                *pExtraInfo << ", Bvm charge = " << nBvmCharge;
        }
        return proto::TxStatus::LowFee;
    }

	return proto::TxStatus::Ok;
}

bool Node::CalculateFeeReserve(const TxStats& s, const HeightRange& hr, const AmountBig::Type& fees, uint32_t nBvmCharge, Amount& feeReserve)
{
    feeReserve = static_cast<Amount>(-1);

    if (hr.m_Min >= Rules::get().pForks[1].m_Height)
    {
        auto& fs = Transaction::FeeSettings::get(hr.m_Min);
        Amount feesMin = fs.Calculate(s);

        if (nBvmCharge)
            feesMin += fs.CalculateForBvm(s, nBvmCharge);

        AmountBig::Type val(feesMin);

        if (fees < val)
        {
            feeReserve = feesMin;
            return false;
        }

        val.Negate();
        val += fees;

        if (!AmountBig::get_Hi(val))
            feeReserve = AmountBig::get_Lo(val);
    }

    return true;
}

void Node::LogTx(const Transaction& tx, uint8_t nStatus, const Transaction::KeyType& key)
{
	if (!m_Cfg.m_LogTxFluff)
		return;

    std::ostringstream os;

    os << "Tx " << key;

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
    }

    for (size_t i = 0; i < tx.m_vKernels.size(); i++)
    {
        const TxKernel& krn = *tx.m_vKernels[i];

		char sz[Merkle::Hash::nTxtLen + 1];
		krn.m_Internal.m_ID.Print(sz);

        os << "\n\tK: " << sz << " Fee=" << krn.m_Fee;
    }

    os << "\n\tStatus: " << static_cast<uint32_t>(nStatus);
    LOG_INFO() << os.str();
}

void Node::LogTxStem(const Transaction& tx, const char* szTxt)
{
	if (!m_Cfg.m_LogTxStem)
		return;

	std::ostringstream os;
	os << "Stem-Tx " << szTxt;

	for (size_t i = 0; i < tx.m_vKernels.size(); i++)
	{
		char sz[Merkle::Hash::nTxtLen + 1];
		tx.m_vKernels[i]->m_Internal.m_ID.Print(sz);

		os << "\n\tK: " << sz;
	}

	LOG_INFO() << os.str();
}

const ECC::uintBig& Node::NextNonce()
{
    ECC::Scalar::Native sk;
    NextNonce(sk);
    return m_NonceLast.V;
}

void Node::NextNonce(ECC::Scalar::Native& sk)
{
    m_Keys.m_pGeneric->DeriveKey(sk, m_NonceLast.V);
    ECC::Hash::Processor() << sk >> m_NonceLast.V;
}

uint32_t Node::RandomUInt32(uint32_t threshold)
{
    if (threshold)
    {
        typedef uintBigFor<uint32_t>::Type Type;

        Type thr(threshold), val;
        Type::Threshold thrSel(thr);

        do
        {
            val = NextNonce();
        } while (!thrSel.Accept(val));

        val.Export(threshold);
    }
    return threshold;
}

uint8_t Node::OnTransactionStem(Transaction::Ptr&& ptx, std::ostream* pExtraInfo)
{
	TxStats s;
	ptx->get_Reader().AddStats(s);
	if (!s.m_Kernels) {
		// stupid compiler insists on parentheses here!
		return proto::TxStatus::TooSmall;
	}

    if ((s.m_InputsShielded > Rules::get().Shielded.MaxIns) || (s.m_OutputsShielded > Rules::get().Shielded.MaxOuts)) {
        return proto::TxStatus::LimitExceeded;
    }

    Transaction::KeyType keyTx;
    ptx->get_Key(keyTx);

    auto itF = m_TxPool.m_setTxs.find(keyTx, TxPool::Fluff::Element::Tx::Comparator());
    TxPool::Fluff::Element* pF = (m_TxPool.m_setTxs.end() == itF) ? nullptr : &itF->get_ParentObj();

    TxPool::Stats stats;
    bool bTested = false;

    if (pF)
    {
        if (TxPool::Fluff::State::Fluffed == pF->m_State)
        {
            LogTxStem(*ptx, "Already fluffed");
            return proto::TxStatus::Ok;
        }

        if (pF->m_Hist.m_Height == m_Processor.m_Cursor.m_Full.m_Height)
        {
            stats = pF->m_Profit.m_Stats;
            bTested = true;
        }

        ptx = pF->m_pValue; // prefer it, to avoid ambiguity
    }

    LogTxStem(*ptx, pF ? "Already received" : "New");

    if (!bTested)
    {
        bool bAlreadyRejected = false;
        uint8_t nCode = ValidateTx(stats, *ptx, keyTx, pExtraInfo, bAlreadyRejected);
        if (proto::TxStatus::Ok != nCode)
        {
            if (!bAlreadyRejected)
                LogTxStem(*ptx, "invalid");
            return nCode;
        }
    }

    bool bDontAggregate = true;
    if (!pF)
    {
        bDontAggregate =
            (ptx->m_vOutputs.size() >= m_Cfg.m_Dandelion.m_OutputsMax) || // already big enough
            s.m_KernelsNonStd || // contains non-std elements
            !m_Keys.m_pMiner; // can't manage decoys

        // add it to wait-fluff list BEFORE we modify it
        Transaction::Ptr pTxOrig;
        if (bDontAggregate)
            pTxOrig = ptx;
        else
        {
            // clone the tx, since we may modify it in the fure
            pTxOrig = std::make_shared<Transaction>();
            TxVectors::Writer wtx(*pTxOrig, *pTxOrig);

            wtx.Dump(ptx->get_Reader());
            pTxOrig->m_Offset = ptx->m_Offset;
        }

        pF = m_TxPool.AddValidTx(std::move(pTxOrig), stats, keyTx, TxPool::Fluff::State::PreFluffed, m_Processor.m_Cursor.m_Full.m_Height);
        m_Wtx.Delete(keyTx);
    }

    if (bDontAggregate)
        OnTransactionAggregated(std::move(ptx), stats);
    else
    {
        AddDummyInputs(*ptx, stats);

        auto pGuard = std::make_unique<TxPool::Stem::Element>();
        pGuard->m_Time.m_Value = 0;
        pGuard->m_Profit.m_Stats = stats;
        pGuard->m_pValue.swap(ptx);

        m_Dandelion.InsertAggr(*pGuard);
        auto* pElem = pGuard.release();

        PerformAggregation(*pElem);
    }

    return proto::TxStatus::Ok;
}

Node::Peer* Node::SelectRandomPeer(Peer::ISelector& sel)
{
    // must have at least 1 peer to continue the stem phase
    uint32_t nCount = 0;

    for (PeerList::iterator it = m_lstPeers.begin(); m_lstPeers.end() != it; ++it)
        if (sel.IsValid(*it))
            nCount++;

    if (nCount)
    {
        auto thr = uintBigFrom(m_Cfg.m_Dandelion.m_FluffProbability);

        // Compare two bytes of threshold with random nonce 
        if (memcmp(thr.m_pData, NextNonce().m_pData, thr.nBytes) < 0)
        {
            // Choose random peer index between 0 and nStemPeers - 1 
            uint32_t nRandomPeerIdx = RandomUInt32(nCount);

            for (PeerList::iterator it = m_lstPeers.begin(); ; ++it)
                if (sel.IsValid(*it) && !nRandomPeerIdx--)
                    return & *it;
        }
    }

    return nullptr;
}

void Node::OnTransactionAggregated(Transaction::Ptr&& pTx, const TxPool::Stats& stats)
{
    LogTxStem(*pTx, "Aggregation finished");

    Peer::Selector_Stem sel;
    Peer* pNext = SelectRandomPeer(sel);
    if (pNext)
    {
        if (m_Cfg.m_LogTxStem)
        {
            LOG_INFO() << "Stem continues to " << pNext->m_RemoteAddr;
        }

        pNext->SendTx(pTx, false);
    }
    else
    {
        LogTxStem(*pTx, "Going to fluff");
        OnTransactionFluff(std::move(pTx), nullptr, nullptr, &stats);
    }
}

void Node::PerformAggregation(TxPool::Stem::Element& x)
{
    bool bModified = false;
    // Aggregation policiy: first select those with worse profit, than those with better
    TxPool::Stem::ProfitSet::iterator it = TxPool::Stem::ProfitSet::s_iterator_to(x.m_Profit);
    ++it;

    while (x.m_pValue->m_vOutputs.size() <= m_Cfg.m_Dandelion.m_OutputsMax)
    {
        if (m_Dandelion.m_setProfit.end() == it)
            break;

        TxPool::Stem::Element& src = it->get_ParentObj();
        ++it;

        if (m_Dandelion.TryMerge(x, src))
            bModified = true;
    }

    it = TxPool::Stem::ProfitSet::s_iterator_to(x.m_Profit);
    if (m_Dandelion.m_setProfit.begin() != it)
    {
        --it;
        while (x.m_pValue->m_vOutputs.size() <= m_Cfg.m_Dandelion.m_OutputsMax)
        {
            TxPool::Stem::Element& src = it->get_ParentObj();

            bool bEnd = (m_Dandelion.m_setProfit.begin() == it);
            if (!bEnd)
                --it;

            if (m_Dandelion.TryMerge(x, src))
                bModified = true;

            if (bEnd)
                break;
        }
    }

    if (bModified)
    {
        m_Dandelion.DeleteAggr(x);
        m_Dandelion.InsertAggr(x);
    }

	LogTxStem(*x.m_pValue, "Aggregated so far");

    if (x.m_pValue->m_vOutputs.size() >= m_Cfg.m_Dandelion.m_OutputsMin)
    {
        OnTransactionAggregated(std::move(x.m_pValue), x.m_Profit.m_Stats);
        m_Dandelion.Delete(x);
    }
    else
	{
		LogTxStem(*x.m_pValue, "Aggregation pending");
		m_Dandelion.SetTimer(m_Cfg.m_Dandelion.m_AggregationTime_ms, x);
	}
}

void Node::AddDummyInputs(Transaction& tx, TxPool::Stats& stats)
{
	if (!m_Keys.m_pMiner)
		return;

    bool bModified = false;

    while (tx.m_vInputs.size() < m_Cfg.m_Dandelion.m_OutputsMax)
    {
		CoinID cid(Zero);
        Height h = m_Processor.get_DB().GetLowestDummy(cid);
        if (h > m_Processor.m_Cursor.m_ID.m_Height)
            break;

		bModified = true;
        cid.m_Value = 0;

		if (AddDummyInputEx(tx, cid))
		{
			/// in the (unlikely) case the tx will be lost - we'll retry spending this UTXO after the following num of blocks
			m_Processor.get_DB().SetDummyHeight(cid, m_Processor.m_Cursor.m_ID.m_Height + m_Cfg.m_Dandelion.m_DummyLifetimeLo + 1);
		}
		else
		{
			// spent
			m_Processor.get_DB().DeleteDummy(cid);
		}
    }

    if (bModified)
    {
        stats.SetSize(tx);
        m_Processor.FlushDB(); // make sure they're not lost
        tx.Normalize();
    }
}

bool Node::AddDummyInputEx(Transaction& tx, const CoinID& cid)
{
	if (AddDummyInputRaw(tx, cid))
		return true;

	// try workaround
	if (!cid.IsBb21Possible())
		return false;

	CoinID cid2 = cid;
	cid2.set_WorkaroundBb21();
	return AddDummyInputRaw(tx, cid2);

}

bool Node::AddDummyInputRaw(Transaction& tx, const CoinID& cid)
{
	assert(m_Keys.m_pMiner);

	Key::IKdf::Ptr pChild;
	Key::IKdf* pKdf = nullptr;

	if (cid.get_Subkey() == m_Keys.m_nMinerSubIndex)
		pKdf = m_Keys.m_pMiner.get();
	else
	{
		// was created by other miner. If we have the root key - we can recreate its key
		if (m_Keys.m_nMinerSubIndex)
			return false;

		pChild = cid.get_ChildKdf(m_Keys.m_pMiner);
		pKdf = pChild.get();
	}

	ECC::Scalar::Native sk;

	// bounds
	ECC::Point comm;
	CoinID::Worker(cid).Create(sk, comm, *pKdf);

	if (!m_Processor.ValidateInputs(comm))
		return false;

	// unspent
	auto pInp = std::make_unique<Input>();
	pInp->m_Commitment = comm;

	tx.m_vInputs.push_back(std::move(pInp));
	tx.m_Offset = ECC::Scalar::Native(tx.m_Offset) + ECC::Scalar::Native(sk);

	return true;
}

void Node::AddDummyOutputs(Transaction& tx, TxPool::Stats& stats)
{
    if (!m_Cfg.m_Dandelion.m_DummyLifetimeHi || !m_Keys.m_pMiner)
        return;

    // add dummy outputs
    bool bModified = false;
    auto& fs = Transaction::FeeSettings::get(m_Processor.m_Cursor.m_Full.m_Height + 1);
    NodeDB& db = m_Processor.get_DB();

    while (tx.m_vOutputs.size() < m_Cfg.m_Dandelion.m_OutputsMin)
    {
        if (stats.m_FeeReserve < fs.m_Output)
            break;

		CoinID cid(Zero);
		cid.m_Type = Key::Type::Decoy;
        cid.set_Subkey(m_Keys.m_nMinerSubIndex);

		while (true)
		{
			NextNonce().ExportWord<0>(cid.m_Idx);
			if (MaxHeight == db.GetDummyHeight(cid))
				break;
		}

        bModified = true;

        auto pOutput = std::make_unique<Output>();
        ECC::Scalar::Native sk;
        pOutput->Create(m_Processor.m_Cursor.m_ID.m_Height + 1, sk, *m_Keys.m_pMiner, cid, *m_Keys.m_pOwner);

		Height h = SampleDummySpentHeight();
        db.InsertDummy(h, cid);

        tx.m_vOutputs.push_back(std::move(pOutput));

        sk = -sk;
        tx.m_Offset = ECC::Scalar::Native(tx.m_Offset) + sk;

        stats.m_FeeReserve -= fs.m_Output;
    }

    if (bModified)
    {
        stats.SetSize(tx);
        m_Processor.FlushDB();
        tx.Normalize();
    }
}

Height Node::SampleDummySpentHeight()
{
	const Config::Dandelion& d = m_Cfg.m_Dandelion; // alias

	Height h = m_Processor.m_Cursor.m_ID.m_Height + d.m_DummyLifetimeLo + 1;

	if (d.m_DummyLifetimeHi > d.m_DummyLifetimeLo)
		h += RandomUInt32(d.m_DummyLifetimeHi - d.m_DummyLifetimeLo);

	return h;
}

uint8_t Node::OnTransactionFluff(Transaction::Ptr&& ptxArg, std::ostream* pExtraInfo, const PeerID* pSender, const TxPool::Stats* pStats)
{
    Transaction::Ptr ptx;
    ptx.swap(ptxArg);

    Transaction::KeyType keyTx;
    ptx->get_Key(keyTx);

    auto itF = m_TxPool.m_setTxs.find(keyTx, TxPool::Fluff::Element::Tx::Comparator());
    TxPool::Fluff::Element* pF = (m_TxPool.m_setTxs.end() == itF) ? nullptr : &itF->get_ParentObj();

    if (pF && (TxPool::Fluff::State::Fluffed == pF->m_State))
        return proto::TxStatus::Ok; // already fluffed

    TxPool::Stats stats;
    if (!pStats)
    {
        bool bTested = pF && (pF->m_Hist.m_Height == m_Processor.m_Cursor.m_Full.m_Height);
        if (!bTested)
        {
            const auto& pTxToTest = pF ? pF->m_pValue : ptx; // avoid ambiguity

            bool bAlreadyRejected = false;
            uint8_t nCode = ValidateTx(stats, *pTxToTest, keyTx, pExtraInfo, bAlreadyRejected);

            if (!bAlreadyRejected)
                LogTx(*pTxToTest, nCode, keyTx);

            if (proto::TxStatus::Ok != nCode)
                return nCode;
        }
    }

    if (!pF)
    {
        pF = m_TxPool.AddValidTx(std::move(ptx), (pStats ? *pStats : stats), keyTx, TxPool::Fluff::State::Fluffed);
        m_Wtx.Delete(keyTx);
    }

    while (m_TxPool.m_setProfit.size() + m_TxPool.m_lstOutdated.size() > m_Cfg.m_MaxPoolTransactions)
    {
        TxPool::Fluff::Element& txDel = m_TxPool.m_lstOutdated.empty() ?
            m_TxPool.m_setProfit.rbegin()->get_ParentObj() :
            m_TxPool.m_lstOutdated.front().get_ParentObj();

        if (&txDel == pF)
            pF = nullptr; // Anti-spam protection: in case the maximum pool capacity is reached - ensure this tx is any better BEFORE broadcasting ti

        m_TxPool.Delete(txDel);
    }

    if (pF)
        OnTransactionFluff(*pF, pSender);

    return proto::TxStatus::Ok;
}

void Node::OnTransactionFluff(TxPool::Fluff::Element& x, const PeerID* pSender)
{
    m_TxPool.SetState(x, TxPool::Fluff::State::Fluffed);

    proto::HaveTransaction msgOut;
    msgOut.m_ID = x.m_Tx.m_Key;

    for (PeerList::iterator it2 = m_lstPeers.begin(); m_lstPeers.end() != it2; ++it2)
    {
        Peer& peer = *it2;
        if (pSender && peer.m_pInfo && (peer.m_pInfo->m_ID.m_Key == *pSender))
            continue;
        if (!(peer.m_LoginFlags & proto::LoginFlags::SpreadingTransactions) || peer.IsChocking())
            continue;

        peer.Send(msgOut);
        peer.SetTxCursor(x.m_pSend);
    }

    m_Miner.SoftRestart();
}

uint8_t Node::OnTransactionDependent(Transaction::Ptr&& pTx, const Merkle::Hash& hvCtx, const PeerID* pSender, bool bFluff, std::ostream* pExtraInfo)
{
    Transaction::KeyType keyTx;
    pTx->get_Key(keyTx);

    TxPool::Dependent::Element* pElem;
    auto itTx = m_TxDependent.m_setTxs.find(keyTx, TxPool::Dependent::Element::Tx::Comparator());
    if (m_TxDependent.m_setTxs.end() != itTx)
        pElem = &itTx->get_ParentObj();
    else
    {
        TxPool::Dependent::Element* pParent;
        if (m_Processor.m_Cursor.m_Full.m_Prev == hvCtx)
            pParent = nullptr;
        else
        {
            auto itCtx = m_TxDependent.m_setContexts.find(hvCtx, TxPool::Dependent::Element::Context::Comparator());
            if (m_TxDependent.m_setContexts.end() == itCtx)
                return proto::TxStatus::DependentNoParent;

            pParent = &itCtx->get_ParentObj();
        }

        Transaction::Context ctx;
        uint32_t nBvmCharge = 0;
        Amount feeReserve = 0;
        Merkle::Hash hvCtxNew;
        uint8_t nRes = ValidateTx2(ctx, *pTx, nBvmCharge, feeReserve, pParent, pExtraInfo, &hvCtxNew);

        if (proto::TxStatus::Ok != nRes)
            return nRes;

        if (m_TxDependent.m_setContexts.find(hvCtxNew, TxPool::Dependent::Element::Context::Comparator()) != m_TxDependent.m_setContexts.end())
            return proto::TxStatus::DependentNoNewCtx;
        
        pElem = m_TxDependent.AddValidTx(std::move(pTx), ctx, keyTx, hvCtxNew, pParent);
        pElem->m_BvmCharge = nBvmCharge;
        if (pParent)
            pElem->m_BvmCharge += pParent->m_BvmCharge;
    }

    struct MySelector :public Peer::ISelector {
        static bool IsValid_(Peer& p) {
            return Peer::Selector_Stem::IsValid_(p) && (p.get_Ext() >= 9);
        }
        bool IsValid(Peer& p) override {
            return IsValid_(p);
        }
    };

    if (!bFluff)
    {
        MySelector sel;

        Peer* pNext = SelectRandomPeer(sel);
        if (pNext)
            pNext->SendTx(pElem->m_pValue, false, &hvCtx);
        else
            bFluff = true;
    }

    bool bNewBest = (m_TxDependent.m_pBest == pElem);

    if (bFluff && !pElem->m_Fluff)
    {
        pElem->m_Fluff = true;

        for (PeerList::iterator it2 = m_lstPeers.begin(); m_lstPeers.end() != it2; ++it2)
        {
            Peer& peer = *it2;
            if (bNewBest)
                peer.MaybeSendDependent();

            if (pSender && peer.m_pInfo && (peer.m_pInfo->m_ID.m_Key == *pSender))
                continue;
            if (!MySelector::IsValid_(peer)/* || peer.IsChocking()*/)
                continue;

            // save time, send the full tx
            peer.SendTx(pElem->m_pValue, true, &hvCtx);
        }

    }

    if (bNewBest)
        m_Miner.SoftRestart();

    return bNewBest ? proto::TxStatus::Ok : proto::TxStatus::DependentNotBest;
}

void Node::Dandelion::OnTimedOut(Element& x)
{
    get_ParentObj().AddDummyOutputs(*x.m_pValue, x.m_Profit.m_Stats);
    get_ParentObj().LogTxStem(*x.m_pValue, "Aggregation timed-out, dummies added");
    get_ParentObj().OnTransactionAggregated(std::move(x.m_pValue), x.m_Profit.m_Stats);

    get_ParentObj().m_Dandelion.Delete(x);
}

bool Node::Dandelion::ValidateTxContext(const Transaction& tx, const HeightRange& hr, const AmountBig::Type& fees, Amount& feeReserve)
{
    uint32_t nBvmCharge = 0;
    if (proto::TxStatus::Ok != get_ParentObj().m_Processor.ValidateTxContextEx(tx, hr, true, nBvmCharge, nullptr, nullptr, nullptr))
        return false;

    TxStats s;
    tx.get_Reader().AddStats(s);
    CalculateFeeReserve(s, hr, fees, nBvmCharge, feeReserve);

    return true;
}

void Node::Peer::OnLogin(proto::Login&& msg, uint32_t nFlagsPrev)
{
    if (Flags::Probe & m_Flags)
    {
        DeleteSelf(false, ByeReason::Probed);
        return;
    }

    if ((m_LoginFlags ^ nFlagsPrev) & proto::LoginFlags::SendPeers)
    {
        if (m_LoginFlags & proto::LoginFlags::SendPeers)
        {
            if (!m_pTimerPeers)
                m_pTimerPeers = io::Timer::create(io::Reactor::get_Current());

            m_pTimerPeers->start(m_This.m_Cfg.m_Timeout.m_TopPeersUpd_ms, true, [this]() { OnResendPeers(); });

            OnResendPeers();
        }
        else
        {
            if (m_pTimerPeers)
                m_pTimerPeers->cancel();
        }
    }

    bool b;
    {
        TemporarySwap ts(m_LoginFlags, nFlagsPrev);
        b = ShouldFinalizeMining();
    }

	if (m_This.m_Cfg.m_Bbs.IsEnabled() &&
		!(proto::LoginFlags::Bbs & nFlagsPrev) &&
		(proto::LoginFlags::Bbs & m_LoginFlags))
	{
		proto::BbsResetSync msgOut;
		msgOut.m_TimeFrom = std::min(m_This.m_Bbs.m_HighestPosted_s, getTimestamp() - Rules::get().DA.MaxAhead_s);
		Send(msgOut);
	}

    MaybeSendSerif();
    MaybeSendDependent();

	if (b != ShouldFinalizeMining()) {
		// stupid compiler insists on parentheses!
		m_This.m_Miner.OnFinalizerChanged(b ? NULL : this);
	}

	BroadcastTxs();
	BroadcastBbs();
}

bool Node::Peer::IsChocking(size_t nExtra /* = 0 */)
{
	if (Flags::Chocking & m_Flags)
		return true;

	if (get_Unsent() + nExtra  <= m_This.m_Cfg.m_BandwidthCtl.m_Chocking)
		return false;

	OnChocking();
	return true;
}

void Node::Peer::OnChocking()
{
	if (!(Flags::Chocking & m_Flags))
	{
		m_Flags |= Flags::Chocking;
		Send(proto::Ping());
	}
}

void Node::Peer::SetTxCursor(TxPool::Fluff::Element::Send* p)
{
	if (m_pCursorTx)
	{
		assert(m_pCursorTx != p);
		m_This.m_TxPool.Release(*m_pCursorTx);
	}

	m_pCursorTx = p;
	if (m_pCursorTx)
		m_pCursorTx->m_Refs++;
}

void Node::Peer::BroadcastTxs()
{
	if (!(proto::LoginFlags::SpreadingTransactions & m_LoginFlags))
		return;

    // TODO: send dependent txs

	if (IsChocking())
		return;

	for (size_t nExtra = 0; ; )
	{
		TxPool::Fluff::SendQueue::iterator itNext;
		if (m_pCursorTx)
		{
			itNext = TxPool::Fluff::SendQueue::s_iterator_to(*m_pCursorTx);
			++itNext;
		}
		else
			itNext = m_This.m_TxPool.m_SendQueue.begin();

		if (m_This.m_TxPool.m_SendQueue.end() == itNext)
			break; // all sent

		SetTxCursor(&(*itNext));

		if (!m_pCursorTx->m_pThis)
			continue; // already deleted
        auto& x = *m_pCursorTx->m_pThis;

		proto::HaveTransaction msgOut;
		msgOut.m_ID = x.m_Tx.m_Key;
		Send(msgOut);

		nExtra += x.m_Profit.m_Stats.m_Size;
		if (IsChocking(nExtra))
			break;
	}
}
void Node::Peer::BroadcastBbs()
{
	m_This.m_Bbs.MaybeCleanup();

	if (!(proto::LoginFlags::Bbs & m_LoginFlags))
		return;

	if (IsChocking())
		return;

	size_t nExtra = 0;

	NodeDB& db = m_This.m_Processor.get_DB();
	NodeDB::WalkerBbsLite wlk;

	wlk.m_ID = m_CursorBbs;
	for (db.EnumAllBbsSeq(wlk); wlk.MoveNext(); )
	{
		proto::BbsHaveMsg msgOut;
		msgOut.m_Key = wlk.m_Key;
		Send(msgOut);

		nExtra += wlk.m_Size;
		if (IsChocking(nExtra))
			break;
	}

	m_CursorBbs = wlk.m_ID;
}

void Node::Peer::MaybeSendSerif()
{
    if (!(Flags::Viewer & m_Flags) || (Flags::SerifSent & m_Flags))
        return;
    assert(m_pAccount);

    proto::EventsSerif msg;
    msg.m_Height = m_pAccount->m_hTxoHi;
    msg.m_Value = m_pAccount->m_Serif;

    Send(msg);
    m_Flags |= Flags::SerifSent;
}

void Node::Peer::MaybeSendDependent()
{
    auto& vec = m_Dependent.m_vSent; // alias
    if (m_Dependent.m_hSent != m_This.m_Processor.m_Cursor.m_Full.m_Height)
    {
        m_Dependent.m_hSent = m_This.m_Processor.m_Cursor.m_Full.m_Height;
        vec.clear();
    }

    if (!(proto::LoginFlags::WantDependentState & m_LoginFlags))
        return;

    auto* pTop = m_This.m_TxDependent.m_pBest;
    if (!pTop)
        return;

    proto::DependentContextChanged msg;
    for (msg.m_PrefixDepth = std::min(pTop->m_Depth, (uint32_t) vec.size()); msg.m_PrefixDepth; msg.m_PrefixDepth--, pTop = pTop->m_pParent)
        if (vec[msg.m_PrefixDepth - 1] == pTop->m_Context.m_Key)
            break;

    vec.resize(msg.m_PrefixDepth);

    pTop = m_This.m_TxDependent.m_pBest;
    if (pTop->m_Depth == msg.m_PrefixDepth)
        return; // no change

    assert(vec.size() < pTop->m_Depth);
    vec.resize(pTop->m_Depth);
    msg.m_vCtxs.resize(vec.size() - msg.m_PrefixDepth);

    for (uint32_t i = pTop->m_Depth; ; pTop = pTop->m_pParent)
    {
        const auto& hv = pTop->m_Context.m_Key;
        vec[--i] = hv;

        uint32_t i2 = i - msg.m_PrefixDepth;
        msg.m_vCtxs[i2] = hv;

        if (!i2)
            break;
    }

    Send(msg);
}

void Node::Peer::OnMsg(proto::HaveTransaction&& msg)
{
    TxPool::Fluff::Element::Tx key;
    key.m_Key = msg.m_ID;

    TxPool::Fluff::TxSet::iterator it = m_This.m_TxPool.m_setTxs.find(key);
    if (m_This.m_TxPool.m_setTxs.end() != it)
    {
        // already have it
        TxPool::Fluff::Element& x = it->get_ParentObj();
        if (TxPool::Fluff::State::Fluffed != x.m_State)
        {
            const PeerID* pSender = m_pInfo ? &m_pInfo->m_ID.m_Key : nullptr;
            m_This.OnTransactionFluff(x, pSender);
        }

        return;
    }

    if (m_This.m_TxReject.end() != m_This.m_TxReject.find(msg.m_ID))
        return;

    if (!m_This.m_Wtx.Add(key.m_Key))
        return; // already waiting for it

    proto::GetTransaction msgOut;
    msgOut.m_ID = msg.m_ID;
    Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetTransaction&& msg)
{
    TxPool::Fluff::Element::Tx key;
    key.m_Key = msg.m_ID;

    TxPool::Fluff::TxSet::iterator it = m_This.m_TxPool.m_setTxs.find(key);
    if (m_This.m_TxPool.m_setTxs.end() == it)
        return; // don't have it

    SendTx(it->get_ParentObj().m_pValue, true);
}

void Node::Peer::SendTx(Transaction::Ptr& ptx, bool bFluff, const Merkle::Hash* pCtx /* = nullptr */)
{
    struct MyMsg :public proto::NewTransaction {
        ~MyMsg() {
            m_Context.release();
        }
    } msg;
    msg.m_Fluff = bFluff;
    msg.m_Context.reset(Cast::NotConst(pCtx)); // fictive
    TemporarySwap scope(msg.m_Transaction, ptx);

    Send(msg);
}

void Node::Peer::OnMsg(proto::GetCommonState&& msg)
{
    proto::ProofCommonState msgOut;

    Processor& p = m_This.m_Processor; // alias

    for (size_t i = 0; i < msg.m_IDs.size(); i++)
    {
        const Block::SystemState::ID& id = msg.m_IDs[i];
        if (id.m_Height < Rules::HeightGenesis)
            ThrowUnexpected();

        if ((id.m_Height < p.m_Cursor.m_ID.m_Height) && !p.IsFastSync())
        {
            Merkle::Hash hv;
            p.get_DB().get_StateHash(p.FindActiveAtStrict(id.m_Height), hv);

            if ((hv == id.m_Hash) || (i + 1 == msg.m_IDs.size()))
            {
                msgOut.m_ID.m_Height = id.m_Height;
                msgOut.m_ID.m_Hash = hv;
                p.GenerateProofStateStrict(msgOut.m_Proof, id.m_Height);
                break;
            }
        }
    }

    Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetProofState&& msg)
{
    if (msg.m_Height < Rules::HeightGenesis)
        ThrowUnexpected();

    proto::ProofState msgOut;

    Processor& p = m_This.m_Processor;
    const NodeDB::StateID& sid = p.m_Cursor.m_Sid;
    if ((msg.m_Height < sid.m_Height) && !p.IsFastSync())
        p.GenerateProofStateStrict(msgOut.m_Proof, msg.m_Height);

    Send(msgOut);
}

void Node::Processor::GenerateProofStateStrict(Merkle::HardProof& proof, Height h)
{
    assert(h < m_Cursor.m_Sid.m_Height);

    Merkle::ProofBuilderHard bld;

    m_Mmr.m_States.get_Proof(bld, m_Mmr.m_States.H2I(h));

    proof.swap(bld.m_Proof);

    struct MyProofBuilder
        :public ProofBuilderHard
    {
        using ProofBuilderHard::ProofBuilderHard;
        virtual bool get_History(Merkle::Hash&) override { return false; }
    };

    MyProofBuilder pb(*this, proof);
    pb.GenerateProof();
}

void Node::Peer::OnMsg(proto::GetProofKernel&& msg)
{
    proto::ProofKernel msgOut;

    Processor& p = m_This.m_Processor;
	if (!p.IsFastSync())
	{
		Height h = p.get_ProofKernel(msgOut.m_Proof.m_Inner, NULL, msg.m_ID);
		if (h)
		{
			uint64_t rowid = p.FindActiveAtStrict(h);
			p.get_DB().get_State(rowid, msgOut.m_Proof.m_State);

			if (h < p.m_Cursor.m_ID.m_Height)
				p.GenerateProofStateStrict(msgOut.m_Proof.m_Outer, h);
		}
	}
    Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetProofKernel2&& msg)
{
    proto::ProofKernel2 msgOut;

	Processor& p = m_This.m_Processor;
	if (!p.IsFastSync())
		msgOut.m_Height = p.get_ProofKernel(msgOut.m_Proof, msg.m_Fetch ? &msgOut.m_Kernel : NULL, msg.m_ID);
    Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetProofUtxo&& msg)
{
    struct Traveler :public UtxoTree::ITraveler
    {
        proto::ProofUtxo m_Msg;
        NodeProcessor& m_Proc;

        virtual bool OnLeaf(const RadixTree::Leaf& x) override {

            const UtxoTree::MyLeaf& v = Cast::Up<UtxoTree::MyLeaf>(x);
            UtxoTree::Key::Data d;
            d = v.m_Key;

            Input::Proof& ret = m_Msg.m_Proofs.emplace_back();

            ret.m_State.m_Count = v.get_Count();
            ret.m_State.m_Maturity = d.m_Maturity;
            m_Proc.get_Utxos().get_Proof(ret.m_Proof, *m_pCu);

            struct MyProofBuilder
                :public NodeProcessor::ProofBuilder
            {
                using ProofBuilder::ProofBuilder;
                virtual bool get_Utxos(Merkle::Hash&) override { return false; }
            };

            MyProofBuilder pb(m_Proc, ret.m_Proof);
            pb.GenerateProof();

            return m_Msg.m_Proofs.size() < Input::Proof::s_EntriesMax;
        }

        Traveler(NodeProcessor& np) :m_Proc(np) {}
    };

	Processor& p = m_This.m_Processor;
    Traveler t(p);

	if (!p.IsFastSync())
	{
		UtxoTree::Cursor cu;
		t.m_pCu = &cu;

		// bounds
		UtxoTree::Key kMin, kMax;

		UtxoTree::Key::Data d;
		d.m_Commitment = msg.m_Utxo;
		d.m_Maturity = msg.m_MaturityMin;
		kMin = d;
		d.m_Maturity = Height(-1);
		kMax = d;

		t.m_pBound[0] = kMin.V.m_pData;
		t.m_pBound[1] = kMax.V.m_pData;

        p.get_Utxos().Traverse(t);
	}

    Send(t.m_Msg);
}

void Node::Processor::GenerateProofShielded(Merkle::Proof& p, const uintBigFor<TxoID>::Type& mmrIdx)
{
    TxoID nIdx;
    mmrIdx.Export(nIdx);

    m_Mmr.m_Shielded.get_Proof(p, nIdx);

    struct MyProofBuilder
        :public NodeProcessor::ProofBuilder
    {
        using ProofBuilder::ProofBuilder;
        virtual bool get_Shielded(Merkle::Hash&) override { return false; }
    };

    MyProofBuilder pb(*this, p);
    pb.GenerateProof();
}

void Node::Peer::OnMsg(proto::GetProofShieldedOutp&& msg)
{
    if (msg.m_SerialPub.m_Y > 1)
        ThrowUnexpected(); // would not be necessary if/when our serialization will take care of this

    proto::ProofShieldedOutp msgOut;

	Processor& p = m_This.m_Processor;
    if (!p.IsFastSync())
	{
        NodeDB::Recordset rs;
        Blob blob(&msg.m_SerialPub, sizeof(msg.m_SerialPub));
        if (p.get_DB().UniqueFind(blob, rs))
        {
            const NodeProcessor::ShieldedOutpPacked& sop = rs.get_As<NodeProcessor::ShieldedOutpPacked>(0); // Note: will throw CorruptionException if of wrong size

            sop.m_Height.Export(msgOut.m_Height);
            sop.m_TxoID.Export(msgOut.m_ID);
            msgOut.m_Commitment = sop.m_Commitment;

            p.GenerateProofShielded(msgOut.m_Proof, sop.m_MmrIndex);
        }
	}

	Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetProofShieldedInp&& msg)
{
    if (msg.m_SpendPk.m_Y > 1)
        ThrowUnexpected(); // would not be necessary if/when our serialization will take care of this

    proto::ProofShieldedInp msgOut;

    Processor& p = m_This.m_Processor;
    if (!p.IsFastSync())
    {
        NodeDB::Recordset rs;

        msg.m_SpendPk.m_Y |= 2;
        Blob blob(&msg.m_SpendPk, sizeof(msg.m_SpendPk));
        if (p.get_DB().UniqueFind(blob, rs))
        {
            const NodeProcessor::ShieldedInpPacked& sip = rs.get_As<NodeProcessor::ShieldedInpPacked>(0); // Note: will throw CorruptionException if of wrong size

            sip.m_Height.Export(msgOut.m_Height);

            p.GenerateProofShielded(msgOut.m_Proof, sip.m_MmrIndex);
        }
    }

    Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetProofAsset&& msg)
{
    proto::ProofAsset msgOut;
    msgOut.m_Info.m_Deposit = Rules::get().CA.DepositForList2; // for backward compatibility, if asset not found - older deserialization should work

    Processor& p = m_This.m_Processor;
    if (!p.IsFastSync())
    {
        Asset::Full ai;
        ai.m_ID = msg.m_AssetID ?
            msg.m_AssetID :
            p.get_DB().AssetFindByOwner(msg.m_Owner);

        if  (ai.m_ID && p.get_DB().AssetGetSafe(ai))
        {
            msgOut.m_Info = std::move(ai);

            if (get_Ext() < 10)
                msgOut.m_Info.SetCid(nullptr);

            p.m_Mmr.m_Assets.get_Proof(msgOut.m_Proof, msgOut.m_Info.m_ID - 1);

            struct MyProofBuilder
                :public NodeProcessor::ProofBuilder
            {
                using ProofBuilder::ProofBuilder;
                virtual bool get_Assets(Merkle::Hash&) override { return false; }
            };

            MyProofBuilder pb(p, msgOut.m_Proof);
            pb.GenerateProof();
        }
    }

    Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetShieldedList&& msg)
{
	proto::ShieldedList msgOut;

	Processor& p = m_This.m_Processor;
	if ((msg.m_Id0 < p.m_Extra.m_ShieldedOutputs) && msg.m_Count)
	{
        std::setmin(msg.m_Count, Rules::get().Shielded.m_ProofMax.get_N() * 2); // no reason to ask for more

		TxoID n = p.m_Extra.m_ShieldedOutputs - msg.m_Id0;

		if (msg.m_Count > n)
			msg.m_Count = static_cast<uint32_t>(n);

		msgOut.m_Items.resize(msg.m_Count);
		p.get_DB().ShieldedRead(msg.m_Id0, &msgOut.m_Items.front(), msg.m_Count);
        p.get_DB().ShieldedStateRead(msg.m_Id0 + msg.m_Count - 1, &msgOut.m_State1, 1);
    }

    Send(msgOut);
}

bool Node::Processor::BuildCwp()
{
    if (!m_Cwp.IsEmpty())
        return true; // already built

    if (m_Cursor.m_Full.m_Height < Rules::HeightGenesis)
        return false;

    struct Source
        :public Block::ChainWorkProof::ISource
    {
        Processor& m_Proc;

        Source(Processor& proc)
            :m_Proc(proc)
        {}

        virtual void get_StateAt(Block::SystemState::Full& s, const Difficulty::Raw& d) override
        {
            uint64_t rowid = m_Proc.get_DB().FindStateWorkGreater(d);
            m_Proc.get_DB().get_State(rowid, s);
        }

        virtual void get_Proof(Merkle::IProofBuilder& bld, Height h) override
        {
            m_Proc.m_Mmr.m_States.get_Proof(bld, m_Proc.m_Mmr.m_States.H2I(h));
        }
    };

    Source src(*this);

    m_Cwp.Create(src, m_Cursor.m_Full);

    Evaluator ev(*this);
    ev.get_Live(m_Cwp.m_hvRootLive);

    return true;
}

void Node::Peer::OnMsg(proto::GetProofChainWork&& msg)
{
    proto::ProofChainWork msgOut;

    Processor& p = m_This.m_Processor;
    if (!p.IsFastSync() && p.BuildCwp())
    {
        msgOut.m_Proof.m_LowerBound = msg.m_LowerBound;
        BEAM_VERIFY(msgOut.m_Proof.Crop(p.m_Cwp));
    }

    Send(msgOut);
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

void Node::Peer::OnMsg(proto::GetExternalAddr&& msg)
{
    proto::ExternalAddr msgOut;
    msgOut.m_Value = m_RemoteAddr.ip();
    Send(msgOut);
}

void Node::Peer::OnMsg(proto::BbsMsg&& msg)
{
	if ((m_This.m_Processor.m_Cursor.m_ID.m_Height >= Rules::get().pForks[1].m_Height) && !Rules::get().FakePoW)
	{
		// test the hash
		ECC::Hash::Value hv;
		proto::Bbs::get_Hash(hv, msg);

        if (!proto::Bbs::IsHashValid(hv))
			return; // drop
    }

	if (!m_This.m_Cfg.m_Bbs.IsEnabled())
		ThrowUnexpected();

	if (msg.m_Message.size() > proto::Bbs::s_MaxMsgSize)
		ThrowUnexpected("Bbs msg too large"); // will also ban this peer

	Timestamp t = getTimestamp();

    if (msg.m_TimePosted > t + Rules::get().DA.MaxAhead_s)
		return; // too much ahead of time

    if (msg.m_TimePosted + m_This.m_Cfg.m_Bbs.m_MessageTimeout_s < t)
        return; // too old

    if (msg.m_TimePosted + Rules::get().DA.MaxAhead_s < m_This.m_Bbs.m_HighestPosted_s)
        return; // don't allow too much out-of-order messages

    NodeDB& db = m_This.m_Processor.get_DB();
    NodeDB::WalkerBbs wlk;

    wlk.m_Data.m_Channel = msg.m_Channel;
    wlk.m_Data.m_TimePosted = msg.m_TimePosted;
    wlk.m_Data.m_Message = Blob(msg.m_Message);
	msg.m_Nonce.Export(wlk.m_Data.m_Nonce);

    Bbs::CalcMsgKey(wlk.m_Data);

    if (db.BbsFind(wlk.m_Data.m_Key))
        return; // already have it

    m_This.m_Bbs.MaybeCleanup();

    uint64_t id = db.BbsIns(wlk.m_Data);
    m_This.m_Bbs.m_W.Delete(wlk.m_Data.m_Key);

	std::setmax(m_This.m_Bbs.m_HighestPosted_s, msg.m_TimePosted);
	m_This.m_Bbs.m_Totals.m_Count++;
	m_This.m_Bbs.m_Totals.m_Size += wlk.m_Data.m_Message.n;

    // 1. Send to other BBS-es

    proto::BbsHaveMsg msgOut;
    msgOut.m_Key = wlk.m_Data.m_Key;

    for (PeerList::iterator it = m_This.m_lstPeers.begin(); m_This.m_lstPeers.end() != it; ++it)
    {
        Peer& peer = *it;
        if (this == &peer)
            continue;

        if (!(peer.m_LoginFlags & proto::LoginFlags::Bbs) || peer.IsChocking())
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
		assert(s.m_Cursor < id);

        if (s.m_pPeer->IsChocking())
            continue;

        s.m_pPeer->SendBbsMsg(wlk.m_Data);
		s.m_Cursor = id;

		s.m_pPeer->IsChocking(); // in case it's chocking - for faster recovery recheck it ASAP
    }
}

void Node::Peer::OnMsg(proto::BbsHaveMsg&& msg)
{
    if (!m_This.m_Cfg.m_Bbs.IsEnabled())
		ThrowUnexpected();

    NodeDB& db = m_This.m_Processor.get_DB();
	if (db.BbsFind(msg.m_Key)) {
		// stupid compiler insists on parentheses here!
		return; // already have it
	}

	if (!m_This.m_Bbs.m_W.Add(msg.m_Key)) {
		// stupid compiler insists on parentheses here!
		return; // already waiting for it
	}

    proto::BbsGetMsg msgOut;
    msgOut.m_Key = msg.m_Key;
    Send(msgOut);
}

void Node::Peer::OnMsg(proto::BbsGetMsg&& msg)
{
	if (!m_This.m_Cfg.m_Bbs.IsEnabled())
		ThrowUnexpected();

	NodeDB& db = m_This.m_Processor.get_DB();
    NodeDB::WalkerBbs wlk;

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
	d.m_Message.Export(msgOut.m_Message);
	msgOut.m_Nonce = d.m_Nonce;
	Send(msgOut);
}

void Node::Peer::OnMsg(proto::BbsSubscribe&& msg)
{
	if (!m_This.m_Cfg.m_Bbs.IsEnabled())
		ThrowUnexpected();

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

		pS->m_Cursor = m_This.m_Processor.get_DB().BbsFindCursor(msg.m_TimeFrom) - 1;

		BroadcastBbs(*pS);
    }
    else
        Unsubscribe(it->get_ParentObj());
}

void Node::Peer::BroadcastBbs(Bbs::Subscription& s)
{
	if (IsChocking())
		return;

	NodeDB& db = m_This.m_Processor.get_DB();
	NodeDB::WalkerBbs wlk;

	wlk.m_Data.m_Channel = s.m_Peer.m_Channel;
	wlk.m_ID = s.m_Cursor;

	for (db.EnumBbsCSeq(wlk); wlk.MoveNext(); )
	{
		SendBbsMsg(wlk.m_Data);
		if (IsChocking())
			break;
	}

	s.m_Cursor = wlk.m_ID;
}

void Node::Peer::OnMsg(proto::BbsResetSync&& msg)
{
	if (!m_This.m_Cfg.m_Bbs.IsEnabled())
		ThrowUnexpected();

	m_CursorBbs = m_This.m_Processor.get_DB().BbsFindCursor(msg.m_TimeFrom) - 1;
	BroadcastBbs();
}

void Node::Peer::OnMsg(proto::GetEvents&& msg)
{
    proto::Events msgOut;

    if (Flags::Viewer & m_Flags)
    {
        assert(m_pAccount);
		Processor& p = m_This.m_Processor;
		NodeDB& db = p.get_DB();
        NodeDB::WalkerEvent wlk;

        Height hLast = 0;
        uint32_t nCount = 0;

        Serializer ser, serCvt;

        for (db.EnumEvents(wlk, m_pAccount->m_iAccount, msg.m_HeightMin); wlk.MoveNext(); hLast = wlk.m_Height)
        {
            if ((nCount >= proto::Event::s_Max) && (wlk.m_Height != hLast))
                break;

			if (p.IsFastSync() && (wlk.m_Height > p.m_SyncData.m_h0))
				break;

            ser & wlk.m_Height;
            ser.WriteRaw(wlk.m_Body.p, wlk.m_Body.n);

            nCount++;   
		}

        ser.swap_buf(msgOut.m_Events);
    }
    else
        LOG_WARNING() << "Peer " << m_RemoteAddr << " Unauthorized Utxo events request.";

    Send(msgOut);
}

void Node::Peer::OnMsg(proto::BlockFinalization&& msg)
{
    if (!(Flags::Owner & m_Flags) ||
        !(Flags::Finalizing & m_Flags))
        ThrowUnexpected();

    m_Flags &= ~Flags::Finalizing;

    if (!msg.m_Value)
        ThrowUnexpected();
    Transaction& tx = *msg.m_Value;

    if (this != m_This.m_Miner.m_pFinalizer)
        return; // outdated

    if (!m_This.m_Miner.m_pTaskToFinalize)
    {
        m_This.m_Miner.Restart(); // time to restart
        return;
    }

    Miner::Task::Ptr pTask;
    pTask.swap(m_This.m_Miner.m_pTaskToFinalize);
    NodeProcessor::GeneratedBlock& x = *pTask;

    {
        ECC::Mode::Scope scope(ECC::Mode::Fast);

        // verify that all the outputs correspond to our viewer's Kdf (in case our comm was hacked this'd prevent mining for someone else)
        // and do the overall validation
		TxBase::Context ctx;
		ctx.m_Height = m_This.m_Processor.m_Cursor.m_ID.m_Height + 1;
        std::string sErr;
        if (!m_This.m_Processor.ValidateAndSummarize(ctx, *msg.m_Value, msg.m_Value->get_Reader(), sErr))
            ThrowUnexpected(sErr.c_str());

        if (ctx.m_Stats.m_Coinbase != AmountBig::Type(Rules::get_Emission(m_This.m_Processor.m_Cursor.m_ID.m_Height + 1)))
            ThrowUnexpected();

        ctx.m_Sigma = -ctx.m_Sigma;
        ctx.m_Stats.m_Coinbase += AmountBig::Type(x.m_Fees);
        AmountBig::AddTo(ctx.m_Sigma, ctx.m_Stats.m_Coinbase);

        if (!(ctx.m_Sigma == Zero))
            ThrowUnexpected();

        if (!tx.m_vInputs.empty())
            ThrowUnexpected();

        for (size_t i = 0; i < tx.m_vOutputs.size(); i++)
        {
            CoinID cid;
            if (!tx.m_vOutputs[i]->Recover(m_This.m_Processor.m_Cursor.m_ID.m_Height + 1, *m_This.m_Keys.m_pOwner, cid))
                ThrowUnexpected();
        }

        tx.MoveInto(x.m_Block);

        ECC::Scalar::Native offs = x.m_Block.m_Offset;
        offs += ECC::Scalar::Native(tx.m_Offset);
        x.m_Block.m_Offset = offs;

    }

    TxPool::Fluff txpEmpty;

    NodeProcessor::BlockContext bc(txpEmpty, 0, *m_This.m_Keys.m_pGeneric, *m_This.m_Keys.m_pGeneric); // the key isn't used anyway
    bc.m_Mode = NodeProcessor::BlockContext::Mode::Finalize;
    Cast::Down<NodeProcessor::GeneratedBlock>(bc) = std::move(x);

    bool bRes = m_This.m_Processor.GenerateNewBlock(bc);

    if (!bRes)
    {
        LOG_WARNING() << "Block finalization failed";
        return;
    }

    Cast::Down<NodeProcessor::GeneratedBlock>(x) = std::move(bc);

    LOG_INFO() << "Block Finalized by owner";

    m_This.m_Miner.StartMining(std::move(pTask));
}

void Node::Peer::OnMsg(proto::GetStateSummary&& msg)
{
    Processor& p = m_This.m_Processor;

    proto::StateSummary msgOut;
    msgOut.m_TxoLo = p.m_Extra.m_TxoLo;
    msgOut.m_Txos = p.m_Extra.m_Txos - p.m_Cursor.m_ID.m_Height; // by convention after each block there's an artificial gap in Txo counting
    msgOut.m_ShieldedOuts = p.m_Extra.m_ShieldedOutputs;
    msgOut.m_ShieldedIns = p.m_Mmr.m_Shielded.m_Count - p.m_Extra.m_ShieldedOutputs;
    msgOut.m_AssetsMax = static_cast<Asset::ID>(p.m_Mmr.m_Assets.m_Count);
    msgOut.m_AssetsActive = static_cast<Asset::ID>(p.get_DB().ParamIntGetDef(NodeDB::ParamID::AssetsCountUsed));

    Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetShieldedOutputsAt&& msg)
{
    Processor& p = m_This.m_Processor;
    Height h = std::min(msg.m_Height, p.m_Cursor.m_Full.m_Height); // don't throw exc if the height is too high, theoretically height can go down due to reorg (of course the chainwork must be higher anyway)

    proto::ShieldedOutputsAt msgOut;
    msgOut.m_ShieldedOuts = p.get_DB().ShieldedOutpGet(h);
    Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetAssetsListAt&& msg)
{
    proto::AssetsListAt msgOut;

    Asset::Full ai;
    ai.m_ID = msg.m_Aid0 ? (msg.m_Aid0 - 1) : 0;

    bool bCurrent = (msg.m_Height >= m_This.m_Processor.m_Cursor.m_Full.m_Height);
    bool bLegacy = (get_Ext() < 10);

    while (ai.m_ID < Asset::s_MaxCount)
    {
        if (bCurrent)
        {
            if (!m_This.m_Processor.get_DB().AssetGetNext(ai))
                break;
        }
        else
        {
            ++ai.m_ID;
            int ret = m_This.m_Processor.get_AssetAt(ai, msg.m_Height);
            if (!ret)
                break;

            if (ret < 0)
                continue;
        }

        if (msgOut.m_Assets.size() >= 3000)
        {
            msgOut.m_bMore = true;
            break;
        }
        
        if (bLegacy)
            ai.SetCid(nullptr);

        msgOut.m_Assets.push_back(std::move(ai));
    }

    Send(msgOut);
}

void Node::Peer::OnMsg(proto::ContractVarsEnum&& msg)
{
    struct Wrk
        :public NodeProcessor::IWorker
    {
        Peer& m_This;
        proto::ContractVarsEnum& m_In;
        proto::ContractVars m_Out;

        Wrk(Peer& x, proto::ContractVarsEnum& msgIn)
            :m_This(x)
            ,m_In(msgIn)
        {}

        void Do() override
        {
            NodeDB::WalkerContractData wlk;
            m_This.m_This.m_Processor.get_DB().ContractDataEnum(wlk, m_In.m_KeyMin, m_In.m_KeyMax);

            Serializer ser;

            while (true)
            {
                if (!wlk.MoveNext())
                    break;

                if (m_In.m_bSkipMin && (wlk.m_Key == m_In.m_KeyMin))
                    continue; // skip

                ser
                    & wlk.m_Key.n
                    & wlk.m_Val.n;

                ser.WriteRaw(wlk.m_Key.p, wlk.m_Key.n);
                ser.WriteRaw(wlk.m_Val.p, wlk.m_Val.n);

                if (m_This.IsChocking(ser.buffer().second))
                {
                    m_Out.m_bMore = true;
                    break;
                }
            }

            ser.swap_buf(m_Out.m_Result);
        }
    };

    Wrk wrk(*this, msg);

    m_This.m_Processor.ExecInDependentContext(wrk, m_Dependent.m_pQuery.get(), m_This.m_TxDependent);

    Send(wrk.m_Out);
}

void Node::Peer::OnMsg(proto::ContractLogsEnum&& msg)
{
    struct Wrk
        :public NodeProcessor::IWorker
    {
        Peer& m_This;
        proto::ContractLogsEnum& m_In;
        proto::ContractLogs m_Out;

        Wrk(Peer& x, proto::ContractLogsEnum& msgIn)
            :m_This(x)
            , m_In(msgIn)
        {}

        void Do() override
        {
            auto& db = m_This.m_This.m_Processor.get_DB();

            NodeDB::ContractLog::Walker wlk;
            if (m_In.m_KeyMin.empty() && m_In.m_KeyMax.empty())
                db.ContractLogEnum(wlk, m_In.m_PosMin, m_In.m_PosMax);
            else
                db.ContractLogEnum(wlk, m_In.m_KeyMin, m_In.m_KeyMax, m_In.m_PosMin, m_In.m_PosMax);

            Serializer ser;

            while (true)
            {
                if (!wlk.MoveNext())
                    break;

                HeightPos dp;
                dp.m_Height = wlk.m_Entry.m_Pos.m_Height - m_In.m_PosMin.m_Height;
                if (dp.m_Height)
                {
                    m_In.m_PosMin.m_Height = wlk.m_Entry.m_Pos.m_Height;
                    m_In.m_PosMin.m_Pos = 0;
                }

                dp.m_Pos = wlk.m_Entry.m_Pos.m_Pos - m_In.m_PosMin.m_Pos;
                m_In.m_PosMin.m_Pos = wlk.m_Entry.m_Pos.m_Pos;

                ser
                    & dp
                    & wlk.m_Entry.m_Key.n
                    & wlk.m_Entry.m_Val.n;

                ser.WriteRaw(wlk.m_Entry.m_Key.p, wlk.m_Entry.m_Key.n);
                ser.WriteRaw(wlk.m_Entry.m_Val.p, wlk.m_Entry.m_Val.n);

                if (m_This.IsChocking(ser.buffer().second))
                {
                    m_Out.m_bMore = true;
                    break;
                }
            }

            ser.swap_buf(m_Out.m_Result);
        }
    };

    Wrk wrk(*this, msg);

    m_This.m_Processor.ExecInDependentContext(wrk, m_Dependent.m_pQuery.get(), m_This.m_TxDependent);

    Send(wrk.m_Out);
}

void Node::Peer::OnMsg(proto::GetContractVar&& msg)
{
    proto::ContractVar msgOut;

    Blob val;
    NodeDB::Recordset rs;

    NodeProcessor& p = m_This.m_Processor;
    if (p.get_DB().ContractDataFind(msg.m_Key, val, rs))
    {
        val.Export(msgOut.m_Value);

        if (p.IsContractVarStoredInMmr(msg.m_Key))
        {
            Merkle::Hash hv;
            Block::get_HashContractVar(hv, msg.m_Key, val);

            RadixHashOnlyTree& t = p.get_Contracts();
            RadixHashOnlyTree::Cursor cu;
            bool bCreate = false;
            if (!t.Find(cu, hv, bCreate))
                NodeProcessor::OnCorrupted();

            t.get_Proof(msgOut.m_Proof, cu);

            struct MyProofBuilder
                :public NodeProcessor::ProofBuilder
            {
                using ProofBuilder::ProofBuilder;
                virtual bool get_Contracts(Merkle::Hash&) override { return false; }
            };

            MyProofBuilder pb(p, msgOut.m_Proof);
            pb.GenerateProof();
        }
    }

    Send(msgOut);
}

void Node::Peer::OnMsg(proto::GetContractLogProof&& msg)
{
    proto::ContractLogProof msgOut;
    m_This.m_Processor.get_ProofContractLog(msgOut.m_Proof, msg.m_Pos);
    Send(msgOut);
}

void Node::Peer::OnMsg(proto::SetDependentContext&& msg)
{
    m_Dependent.m_pQuery = std::move(msg.m_Context);
}

void Node::Server::OnAccepted(io::TcpStream::Ptr&& newStream, int errorCode)
{
    if (newStream)
    {
        LOG_DEBUG() << "New peer connected: " << newStream->address();
        Peer* p = get_ParentObj().AllocPeer(newStream->peer_address());
        p->Accept(std::move(newStream));
		p->m_Flags |= Peer::Flags::Accepted;
        p->SecureConnect();
    }
}

bool Node::Miner::IsEnabled() const 
{
    if (!get_ParentObj().m_Keys.m_pOwner)
        return false;

    if (!m_External.m_pSolver && m_vThreads.empty())
        return false;

    if (!Rules::get().FakePoW)
        return true;

    if (get_ParentObj().m_Cfg.m_TestMode.m_FakePowSolveTime_ms > 0)
        return true;

    return m_FakeBlocksToGenerate > 0;
}

void Node::Miner::Initialize(IExternalPOW* externalPOW)
{
    const Config& cfg = get_ParentObj().m_Cfg;
    if (!cfg.m_MiningThreads && !externalPOW)
        return;

    m_LastRestart_ms = 0;
    m_pEvtMined = io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { OnMined(); });

    if (cfg.m_MiningThreads) {
        m_vThreads.resize(cfg.m_MiningThreads);
        for (uint32_t i = 0; i < cfg.m_MiningThreads; i++) {
            PerThread &pt = m_vThreads[i];
            pt.m_pReactor = io::Reactor::create();
            pt.m_pEvt = io::AsyncEvent::create(*pt.m_pReactor, [this, i]() { OnRefresh(i); });
            pt.m_Thread = std::thread(&Miner::RunMinerThread, this, pt.m_pReactor, Rules::get());
        }
    }

	m_External.m_pSolver = externalPOW;

    SetTimer(0, true); // async start mining
}

void Node::Miner::RunMinerThread(const io::Reactor::Ptr& pReactor, const Rules& r)
{
    Rules::Scope scopeRules(r);
    pReactor->run();
}

void Node::Miner::OnFinalizerChanged(Peer* p)
{
    // always prefer newer (in case there are several ones)
    m_pFinalizer = p;
    if (!m_pFinalizer)
    {
        // try to find another one
        for (PeerList::iterator it = get_ParentObj().m_lstPeers.begin(); get_ParentObj().m_lstPeers.end() != it; ++it)
            if (it->ShouldFinalizeMining())
            {
                m_pFinalizer = &(*it);
                break;
            }
    }

    Restart();
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
            << pTask->m_hvNonceSeed
            << iIdx
            >> hv;

        static_assert(s.m_PoW.m_Nonce.nBytes <= hv.nBytes);
        s.m_PoW.m_Nonce = hv;

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

            bool bSolved = timeout_ms == 0;

            for (uint32_t t0_ms = GetTime_ms(); !bSolved; )
            {
                if (fnCancel(true))
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
            try
            {
                if (!s.GeneratePoW(fnCancel))
                    continue;
            }
            catch (const std::exception& ex)
            {
                LOG_DEBUG() << ex.what();
                break;
            }
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
    m_pTaskToFinalize.reset();
    m_FeesTrg = 0;

    std::scoped_lock<std::mutex> scope(m_Mutex);

    if (m_pTask)
    {
        *m_pTask->m_pStop = true;
        m_pTask.reset();
    }

	if (m_External.m_pSolver)
	{
		bool bHadTasks = false;

		for (size_t i = 0; i < _countof(m_External.m_ppTask); i++)
		{
			Task::Ptr& pTask = m_External.m_ppTask[i];
			if (!pTask)
				continue;

			assert(*pTask->m_pStop); // should be the same stop inficator
			pTask.reset();
			bHadTasks = true;
		}

		if (bHadTasks)
		    m_External.m_pSolver->stop_current();
	}
}

void Node::Miner::SoftRestart()
{
    if (!IsEnabled() || m_pTaskToFinalize)
        return;

    uint32_t nTimeout_ms = 0;
    if (m_LastRestart_ms)
    {
        uint32_t nMin_ms = get_ParentObj().m_Cfg.m_Timeout.m_MiningSoftRestart_ms;
        uint32_t nElapsed_ms = GetTimeNnz_ms() - m_LastRestart_ms;
        if (nElapsed_ms < nMin_ms)
            nTimeout_ms = nMin_ms - nElapsed_ms;
    }

    SetTimer(nTimeout_ms, false);
}

void Node::Miner::SetTimer(uint32_t timeout_ms, bool bHard)
{
    if (!IsEnabled())
        return;

    if (!m_pTimer)
        m_pTimer = io::Timer::create(io::Reactor::get_Current());
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
    m_LastRestart_ms = GetTimeNnz_ms();

    if (!IsEnabled())
        return false; //  n/a

    if (!get_ParentObj().m_Processor.IsTreasuryHandled() || get_ParentObj().m_Processor.IsFastSync())
        return false;

    m_pTaskToFinalize.reset();

    const Keys& keys = get_ParentObj().m_Keys;

    if (m_pFinalizer)
    {
        if (Peer::Flags::Finalizing & m_pFinalizer->m_Flags)
            return false; // wait until we receive that outdated finalization
    }
    else
        if (!keys.m_pMiner)
            return false; // offline mining is disabled

    NodeProcessor::BlockContext bc(
        get_ParentObj().m_TxPool,
        keys.m_nMinerSubIndex,
        keys.m_pMiner ? *keys.m_pMiner : *keys.m_pGeneric,
        keys.m_pOwner ? *keys.m_pOwner : *keys.m_pGeneric);

    bc.m_pParent = get_ParentObj().m_TxDependent.m_pBest;

    if (m_pFinalizer)
        bc.m_Mode = NodeProcessor::BlockContext::Mode::Assemble;

    bool bRes = get_ParentObj().m_Processor.GenerateNewBlock(bc);

    if (!bRes)
    {
        LOG_WARNING() << "Block generation failed, can't mine!";
        return false;
    }

    if (!IsShouldMine(bc))
        return false;

    Task::Ptr pTask(std::make_shared<Task>());
    Cast::Down<NodeProcessor::GeneratedBlock>(*pTask) = std::move(bc);

    if (m_pFinalizer)
    {
        const NodeProcessor::GeneratedBlock& x = *pTask;
        LOG_INFO() << "Block generated: Height=" << x.m_Hdr.m_Height << ", Fee=" << x.m_Fees << ", Waiting for owner response...";

        proto::GetBlockFinalization msg;
        msg.m_Height = pTask->m_Hdr.m_Height;
        msg.m_Fees = pTask->m_Fees;
        m_pFinalizer->Send(msg);

        assert(!(Peer::Flags::Finalizing & m_pFinalizer->m_Flags));
        m_pFinalizer->m_Flags |= Peer::Flags::Finalizing;

        m_pTaskToFinalize = std::move(pTask);
    }
    else
        StartMining(std::move(pTask));

    return true;
}

bool Node::Miner::IsShouldMine(const NodeProcessor::GeneratedBlock& bc)
{
    if (bc.m_Fees >= m_FeesTrg)
        return true;

    LOG_INFO() << "Block generation no change";
    return false;
}

void Node::Miner::StartMining(Task::Ptr&& pTask)
{
    assert(pTask && !m_pTaskToFinalize);

    const NodeProcessor::GeneratedBlock& x = *pTask;
    if (!IsShouldMine(x))
        return;

    LOG_INFO() << "Block generated: Height=" << x.m_Hdr.m_Height << ", Fee=" << x.m_Fees << ", Difficulty=" << x.m_Hdr.m_PoW.m_Difficulty << ", Size=" << (x.m_BodyP.size() + x.m_BodyE.size());

    m_FeesTrg = x.m_Fees + 1;

    pTask->m_hvNonceSeed = get_ParentObj().NextNonce();

    // let's mine it.
    {
        std::scoped_lock<std::mutex> scope(m_Mutex);

        if (m_pTask)
        {
            if (*m_pTask->m_pStop)
                return; // block already mined, probably notification to this thread on its way. Ignore the newly-constructed block
            pTask->m_pStop = m_pTask->m_pStop; // use the same soft-restart indicator
        }
        else
        {
            pTask->m_pStop.reset(new bool);
            *pTask->m_pStop = false;
        }

        m_pTask = std::move(pTask);

        for (size_t i = 0; i < m_vThreads.size(); i++)
            m_vThreads[i].m_pEvt->post();
    }

    OnRefreshExternal();
}

Node::Miner::Task::Ptr& Node::Miner::External::get_At(uint64_t jobID)
{
	return m_ppTask[static_cast<size_t>(jobID % _countof(m_ppTask))];
}

void Node::Miner::OnRefreshExternal()
{
    if (!m_External.m_pSolver)
		return;

    // NOTE the mutex is locked here
    LOG_INFO() << "New job for external miner";

	uint64_t jobID = ++m_External.m_jobID;
	m_External.get_At(jobID) = m_pTask;

    auto fnCancel = []() { return false; };

    Merkle::Hash hv;
	m_pTask->m_Hdr.get_HashForPoW(hv);

	m_External.m_pSolver->new_job(std::to_string(jobID), hv, m_pTask->m_Hdr.m_PoW, m_pTask->m_Hdr.m_Height , BIND_THIS_MEMFN(OnMinedExternal), fnCancel);
}

IExternalPOW::BlockFoundResult Node::Miner::OnMinedExternal()
{
	std::string jobID_;
	Block::PoW POW;
	Height h;

	assert(m_External.m_pSolver);
	m_External.m_pSolver->get_last_found_block(jobID_, h, POW);

	char* szEnd = nullptr;
	uint64_t jobID = strtoul(jobID_.c_str(), &szEnd, 10);

	std::scoped_lock<std::mutex> scope(m_Mutex);

	bool bReject = (m_External.m_jobID - jobID >= _countof(m_External.m_ppTask));

	LOG_INFO() << "Solution from external miner. jobID=" << jobID << ", Current.jobID=" << m_External.m_jobID << ", Accept=" << static_cast<uint32_t>(!bReject);

    if (bReject)
    {
        LOG_INFO() << "Solution is rejected due it is outdated.";
		return IExternalPOW::solution_expired; // outdated
    }

	Task::Ptr& pTask = m_External.get_At(jobID);

    if (!pTask || *pTask->m_pStop)
    {
        LOG_INFO() << "Solution is rejected due block mining has been canceled.";
		return IExternalPOW::solution_rejected; // already cancelled
    }

	pTask->m_Hdr.m_PoW.m_Nonce = POW.m_Nonce;
	pTask->m_Hdr.m_PoW.m_Indices = POW.m_Indices;

    if (!pTask->m_Hdr.IsValidPoW())
    {
        LOG_INFO() << "invalid solution from external miner";
        return IExternalPOW::solution_rejected;
    }

    IExternalPOW::BlockFoundResult result(IExternalPOW::solution_accepted);
    Merkle::Hash hv;
    pTask->m_Hdr.get_Hash(hv);
    result._blockhash = to_hex(hv.m_pData, hv.nBytes);

	m_pTask = pTask;
    *m_pTask->m_pStop = true;
    m_pEvtMined->post();

    return result;
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

    Processor& p = get_ParentObj().m_Processor; // alias
    NodeProcessor::DataStatus::Enum eStatus = p.OnState(pTask->m_Hdr, get_ParentObj().m_MyPublicID);
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

    eStatus = p.OnBlock(id, pTask->m_BodyP, pTask->m_BodyE, get_ParentObj().m_MyPublicID);
    assert(NodeProcessor::DataStatus::Accepted == eStatus);
    if (m_FakeBlocksToGenerate)
    {
        assert(Rules::get().FakePoW);
        --m_FakeBlocksToGenerate;
    }
    p.FlushDB();
	p.TryGoUpAsync(); // will likely trigger OnNewState(), and spread this block to the network
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
    :m_pUdp(NULL)
    ,m_pOut(NULL)
{
}

Node::Beacon::~Beacon()
{
    if (m_pUdp)
        uv_close((uv_handle_t*) m_pUdp, OnClosed);

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
    assert(!m_pUdp);
    m_pUdp = new uv_udp_t;

    uv_udp_init(&io::Reactor::get_Current().get_UvLoop(), m_pUdp);
    m_pUdp->data = this;

    m_BufRcv.resize(sizeof(OutCtx::Message));

    io::Address addr;
    addr.port(get_Port());

    sockaddr_in sa;
    addr.fill_sockaddr_in(sa);

    if (uv_udp_bind(m_pUdp, (sockaddr*)&sa, UV_UDP_REUSEADDR)) // should allow multiple nodes on the same machine (for testing)
        std::ThrowLastError();

    if (uv_udp_recv_start(m_pUdp, AllocBuf, OnRcv))
        std::ThrowLastError();

    if (uv_udp_set_broadcast(m_pUdp, 1))
        std::ThrowLastError();

    m_pTimer = io::Timer::create(io::Reactor::get_Current());
    m_pTimer->start(get_ParentObj().m_Cfg.m_BeaconPeriod_ms, true, [this]() { OnTimer(); }); // periodic timer
    OnTimer();
}

void Node::Beacon::OnTimer()
{
    if (!m_pOut)
    {
        m_pOut = new OutCtx;
        m_pOut->m_Refs = 1;

        m_pOut->m_Message.m_CfgChecksum = Rules::get().get_LastFork().m_Hash;
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

    int nErr = uv_udp_send(&m_pOut->m_Request, m_pUdp, &m_pOut->m_BufDescr, 1, (sockaddr*) &sa, OutCtx::OnDone);
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

    if (msg.m_CfgChecksum != Rules::get().get_LastFork().m_Hash)
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

void Node::Beacon::OnClosed(uv_handle_t* p)
{
    assert(p);
    delete (uv_udp_t*) p;
}

void Node::PeerMan::Initialize()
{
    const Config& cfg = get_ParentObj().m_Cfg;

    for (uint32_t i = 0; i < cfg.m_Connect.size(); i++)
    {
        PeerID id0(Zero);
        OnPeer(id0, cfg.m_Connect[i], true);
    }

    // peers
    m_pTimerUpd = io::Timer::create(io::Reactor::get_Current());
    m_pTimerUpd->start(cfg.m_Timeout.m_PeersUpdate_ms, true, [this]() { Update(); });

    m_pTimerFlush = io::Timer::create(io::Reactor::get_Current());
    m_pTimerFlush->start(cfg.m_Timeout.m_PeersDbFlush_ms, true, [this]() { OnFlush(); });

    {
        NodeDB::WalkerPeer wlk;
        for (get_ParentObj().m_Processor.get_DB().EnumPeers(wlk); wlk.MoveNext(); )
        {
            if (wlk.m_Data.m_ID == get_ParentObj().m_MyPublicID)
                continue; // could be left from previous run?

            PeerMan::PeerInfo* pPi = OnPeer(wlk.m_Data.m_ID, io::Address::from_u64(wlk.m_Data.m_Address), false);
            if (!pPi)
                continue;

            // set rating
            uint32_t r = wlk.m_Data.m_Rating;
            if (!r)
                Ban(*pPi);
            else
                SetRating(*pPi, r);

            pPi->m_LastSeen = wlk.m_Data.m_LastSeen;
            pPi->m_LastConnectAttempt = pPi->m_LastSeen;
        }
    }
}

void Node::PeerMan::OnFlush()
{
    NodeDB& db = get_ParentObj().m_Processor.get_DB();

    db.PeersDel();

    const PeerMan::RawRatingSet& rs = get_Ratings();

    for (PeerMan::RawRatingSet::const_iterator it = rs.begin(); rs.end() != it; ++it)
    {
        const PeerMan::PeerInfo& pi = it->get_ParentObj();

        NodeDB::WalkerPeer::Data d;
        d.m_ID = pi.m_ID.m_Key;
        d.m_Rating = pi.m_RawRating.m_Value;
        d.m_Address = pi.m_Addr.m_Value.u64();
        d.m_LastSeen = pi.m_LastSeen;

        db.PeerIns(d);
    }
}

void Node::PeerMan::ActivateMorePeers(uint32_t nTime_ms)
{
    const Config& cfg = get_ParentObj().m_Cfg; // alias
    if (!cfg.m_PeersPersistent)
        return;

    for (uint32_t i = 0; i < cfg.m_Connect.size(); i++)
    {
        PeerID id0(Zero);
        PeerInfo* pPi = OnPeer(id0, cfg.m_Connect[i], true);
        if (pPi)
            ActivatePeerSafe(*pPi, nTime_ms);
    }
}

void Node::PeerMan::ActivatePeer(PeerInfo& pi)
{
    PeerInfoPlus& pip = Cast::Up<PeerInfoPlus>(pi);
    if (pip.m_Live.m_p)
        return; //?

    Peer* p = get_ParentObj().AllocPeer(pip.m_Addr.m_Value);
	pip.Attach(*p);

    p->Connect(pip.m_Addr.m_Value);
    p->m_Port = pip.m_Addr.m_Value.port();
}

void Node::PeerMan::DeactivatePeer(PeerInfo& pi)
{
    PeerInfoPlus& pip = (PeerInfoPlus&)pi;
    if (!pip.m_Live.m_p)
        return; //?

    pip.m_Live.m_p->DeleteSelf(false, proto::NodeConnection::ByeReason::Other);
}

PeerManager::PeerInfo* Node::PeerMan::AllocPeer()
{
    PeerInfoPlus* p = new PeerInfoPlus;
    p->m_Live.m_p = nullptr;
    return p;
}

void Node::PeerMan::DeletePeer(PeerInfo& pi)
{
    delete &Cast::Up<PeerInfoPlus>(pi);
}

void Node::PeerMan::PeerInfoPlus::Attach(Peer& p)
{
	assert(!m_Live.m_p && !p.m_pInfo);
	m_Live.m_p = &p;
	p.m_pInfo = this;

	PeerManager::TimePoint tp;
	p.m_This.m_PeerMan.m_LiveSet.insert(m_Live);
}

void Node::PeerMan::PeerInfoPlus::DetachStrict()
{
	assert(m_Live.m_p && (this == m_Live.m_p->m_pInfo));

	m_Live.m_p->m_This.m_PeerMan.m_LiveSet.erase(PeerMan::LiveSet::s_iterator_to(m_Live));

	m_Live.m_p->m_pInfo = nullptr;
	m_Live.m_p = nullptr;
}

bool Node::GenerateRecoveryInfo(const char* szPath)
{
	if (!m_Processor.BuildCwp())
		return false; // no info yet

    typedef yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> MySerializer;

	struct MyTraveler
		:public RadixTree::ITraveler
	{
		RecoveryInfo::Writer m_Writer;
		NodeDB* m_pDB;

		virtual bool OnLeaf(const RadixTree::Leaf& x) override
		{
			const UtxoTree::MyLeaf& n = Cast::Up<UtxoTree::MyLeaf>(x);
			UtxoTree::Key::Data d;
			d = n.m_Key;

			if (n.IsExt())
			{
				for (auto p = n.m_pIDs.get_Strict()->m_pTop.get_Strict(); p; p = p->m_pNext.get())
					OnUtxo(d, p->m_ID);
			}
			else
				OnUtxo(d, n.m_ID);

			return true;
		}

		void OnUtxo(const UtxoTree::Key::Data& d, TxoID id)
		{
			NodeDB::WalkerTxo wlk;
			m_pDB->TxoGetValue(wlk, id);

			Deserializer der;
			der.reset(wlk.m_Value.p, wlk.m_Value.n);

            Output outp;
			der & outp;

            assert(outp.m_Commitment == d.m_Commitment);

			// 2 ways to discover the UTXO create height: either directly by looking its TxoID in States table, or reverse-engineer it from Maturity
			// Since currently maturity delta is independent of current height (not a function of height, not changed in current forks) - we prefer the 2nd method, which is faster.

            //m_Processor.FindHeightByTxoID(val.m_CreateHeight, id);
			//assert(val.m_Output.get_MinMaturity(val.m_CreateHeight) == d.m_Maturity);

			Height hCreateHeight = d.m_Maturity - outp.get_MinMaturity(0);

            MySerializer ser(m_Writer.m_Stream);
            ser & hCreateHeight;

            yas::detail::saveRecovery(ser, outp, hCreateHeight);
		}
	};

	MyTraveler ctx;
	ctx.m_pDB = &m_Processor.get_DB();

	try
	{
		ctx.m_Writer.Open(szPath, m_Processor.m_Cwp);
		m_Processor.get_Utxos().Traverse(ctx);

        const Rules& r = Rules::get();

        if (m_Processor.m_Cursor.m_ID.m_Height >= r.pForks[2].m_Height)
        {
            MySerializer ser(ctx.m_Writer.m_Stream);
            ser & MaxHeight; // terminator

            // shielded in/outs
            struct MyKrnWalker
                :public NodeProcessor::KrnWalkerShielded
            {
                MySerializer& m_Ser;
                MyKrnWalker(MySerializer& ser) :m_Ser(ser) {}

                virtual bool OnKrnEx(const TxKernelShieldedInput& krn) override
                {
                    uint8_t nFlags = 0;

                    m_Ser & m_Height;
                    m_Ser & nFlags;
                    m_Ser & krn.m_SpendProof.m_SpendPk;
                    return true;
                }

                virtual bool OnKrnEx(const TxKernelShieldedOutput& krn) override
                {
                    Asset::Proof::Ptr pAsset;

                    uint8_t nFlags = RecoveryInfo::Flags::Output;
                    if (krn.m_Txo.m_pAsset)
                    {
                        pAsset.swap(Cast::NotConst(krn).m_Txo.m_pAsset);
                        nFlags |= RecoveryInfo::Flags::HadAsset;
                    }

                    m_Ser & m_Height;
                    m_Ser & nFlags;
                    m_Ser & krn.m_Txo;
                    m_Ser & krn.m_Msg;

                    if (pAsset && (m_Height >= Rules::get().pForks[3].m_Height))
                        m_Ser & pAsset->m_hGen;

                    return true;
                }

            } wlk(ser);

            m_Processor.EnumKernels(wlk, HeightRange(r.pForks[2].m_Height, m_Processor.m_Cursor.m_ID.m_Height));

            ser & MaxHeight; // terminator

            // assets
            Asset::Full ai;
            ai.m_ID = 0;

            while (m_Processor.get_DB().AssetGetNext(ai))
            {
                if (m_Processor.m_Cursor.m_ID.m_Height < r.pForks[6].m_Height)
                    ai.SetCid(nullptr);

                ser & ai;
            }

            ser & (Asset::s_MaxCount + 1); // terminator

            if (m_Processor.m_Cursor.m_ID.m_Height >= r.pForks[3].m_Height)
            {
                m_Processor.EnsureCursorKernels();

                Merkle::Hash hv;
                NodeProcessor::Evaluator ev(m_Processor);

                BEAM_VERIFY(ev.get_Contracts(hv));
                ser & hv;

                BEAM_VERIFY(ev.get_KL(hv));
                ser & hv;
            }
        }

	}
	catch (const std::exception& ex)
	{
		LOG_ERROR() << ex.what();
		return false;
	}

	return true;
}

void Node::PrintTxos()
{
    for (const auto& acc : m_Processor.m_vAccounts)
    {
        struct PerAsset {
            Amount m_Avail = 0;
            Amount m_Locked = 0;
        };
        typedef std::map<Asset::ID, PerAsset> AssetMap;

        std::ostringstream os;
        os << "Printing Events for Endpoint=" << acc.get_Endpoint() << std::endl;

        if (m_Processor.IsFastSync())
            os << "Note: Fast-sync is in progress. Data is preliminary and not fully verified yet." << std::endl;

        if (acc.m_hTxoHi >= Rules::HeightGenesis)
            os << "Note: Cut-through up to Height=" << acc.m_hTxoHi << ", Txos spent earlier may be missing. To recover them too please make full sync." << std::endl;

        struct MyParser :public proto::Event::IParser
        {
            std::ostringstream& m_os;
            AssetMap m_Map;
            Height m_Height = 0;

            MyParser(std::ostringstream& os) :m_os(os) {}

            virtual void OnEventBase(proto::Event::Base& x) override
            {
                x.Dump(m_os);
            }

            void OnValue(Amount v, Asset::ID aid, uint8_t nFlags, bool bMature)
            {
                PerAsset& pa = m_Map[aid];

                if (proto::Event::Flags::Add & nFlags)
                    (bMature ? pa.m_Avail : pa.m_Locked) += v;
                else
                    pa.m_Avail -= v;
            }

            virtual void OnEventType(proto::Event::Utxo& x) override
            {
                OnEventBase(x);
                OnValue(x.m_Cid.m_Value, x.m_Cid.m_AssetID, x.m_Flags, x.m_Maturity <= m_Height);
            }

            virtual void OnEventType(proto::Event::Shielded& x) override
            {
                OnEventBase(x);
                OnValue(x.m_CoinID.m_Value, x.m_CoinID.m_AssetID, x.m_Flags, true);
            }

        } p(os);
        p.m_Height = m_Processor.m_Cursor.m_Full.m_Height;

        NodeDB::WalkerEvent wlk;
        for (m_Processor.get_DB().EnumEvents(wlk, acc.m_iAccount, Rules::HeightGenesis - 1); wlk.MoveNext(); )
        {
            os << "\tHeight=" << wlk.m_Height << ", ";
            p.ProceedOnce(wlk.m_Body);
            os << std::endl;
        }

        os << "Totals:" << std::endl;

        for (AssetMap::iterator it = p.m_Map.begin(); p.m_Map.end() != it; ++it)
        {
            const PerAsset& pa = it->second;
            if (pa.m_Avail || pa.m_Locked)
            {
                os << "\tAssetID=" << it->first << ", Avail=" << AmountBig::Printable(pa.m_Avail) << ", Locked=" << AmountBig::Printable(pa.m_Locked) << std::endl;
            }
        }

        LOG_INFO() << os.str();
    }
}

void Node::PrintRollbackStats()
{
    std::ostringstream os;
    os << "Printing recent rollback stats" << std::endl;

    NodeDB& db = m_Processor.get_DB(); // alias

    ByteBuffer bufP, bufE;
    std::vector<NodeDB::StateInput> vIns;

    NodeDB::WalkerState wlk;
    for (db.EnumFunctionalTips(wlk); wlk.MoveNext(); )
    {
        Block::SystemState::Full sTip1;
        db.get_State(wlk.m_Sid.m_Row, sTip1);
        assert(sTip1.m_Height == wlk.m_Sid.m_Height);

        typedef std::map<ECC::Point, Height> InputMap;
        InputMap inpMap;

        typedef std::map<ECC::Hash::Value, Height> KrnMap;
        KrnMap krnMap;

        NodeDB::StateID sidSplit = wlk.m_Sid;

        for (; sidSplit.m_Row; db.get_Prev(sidSplit))
        {
            if (NodeDB::StateFlags::Active & db.GetStateFlags(sidSplit.m_Row))
                break;

            bufP.clear();
            bufE.clear();
            db.GetStateBlock(sidSplit.m_Row, &bufP, &bufE, nullptr);

            Block::Body block;

            Deserializer der;
            der.reset(bufP);
            der & Cast::Down<Block::BodyBase>(block);
            der & Cast::Down<TxVectors::Perishable>(block);

            der.reset(bufE);
            der & Cast::Down<TxVectors::Eternal>(block);

            for (size_t i = 0; i < block.m_vInputs.size(); i++)
                inpMap[block.m_vInputs[i]->m_Commitment] = sidSplit.m_Height;

            for (size_t i = 0; i < block.m_vKernels.size(); i++)
                krnMap[block.m_vKernels[i]->m_Internal.m_ID] = sidSplit.m_Height;
        }

        if (inpMap.empty())
            continue;

        os << "Rollback " << sTip1.m_Height << " -> " << sidSplit.m_Height << std::endl;

        Height h = sidSplit.m_Height;
        while (h < m_Processor.m_Cursor.m_Full.m_Height)
        {
            uint64_t row = db.FindActiveStateStrict(++h);
            Block::SystemState::Full s2;
            db.get_State(row, s2);

            if (s2.m_TimeStamp > sTip1.m_TimeStamp)
            {
                os << "	H=" << h << ", Stopping" << std::endl;
                break;
            }

            vIns.clear();
            db.get_StateInputs(row, vIns);

            if (vIns.empty())
                continue;

            os << "	H=" << h << ", Inputs=" << vIns.size() << std::endl;

            bufE.clear();
            db.GetStateBlock(row, nullptr, &bufE, nullptr);

            TxVectors::Eternal krns;

            Deserializer der;
            der.reset(bufE);
            der & krns;

            typedef std::map<Height, uint32_t> DoubleSpendMap;
            DoubleSpendMap dsm;

            for (size_t i = 0; i < vIns.size(); i++)
            {
                ECC::Point comm;
                vIns[i].Get(comm);

                InputMap::iterator it = inpMap.find(comm);
                if (inpMap.end() != it)
                {
                    Height hOld = it->second;
                    inpMap.erase(it);

                    DoubleSpendMap::iterator it2 = dsm.find(hOld);
                    if (dsm.end() == it2)
                    {
                        // check if there're corresponding kernels
                        uint32_t nCommonKrns = 0;
                        for (size_t j = 0; j < krns.m_vKernels.size(); j++)
                        {
                            if (krnMap.end() != krnMap.find(krns.m_vKernels[j]->m_Internal.m_ID))
                                nCommonKrns++;
                        }

                        it2 = dsm.insert(DoubleSpendMap::value_type(hOld, nCommonKrns ? uint32_t(-1) : 0)).first;
                    }

                    if (it2->second != uint32_t(-1))
                        it2->second++;
                }
            }

            for (DoubleSpendMap::iterator it = dsm.begin(); dsm.end() != it; ++it)
            {
                uint32_t n = it->second;
                if (uint32_t(-1) == n)
                    continue;

                os << "		Double-spend. Old Height=" << it->first << ", Count=" << n << std::endl;
            }

        }

    }

    LOG_INFO() << os.str();
}

void Node::GenerateFakeBlocks(uint32_t n)
{
    assert(Rules::get().FakePoW);
    m_Miner.m_FakeBlocksToGenerate = n;
    m_Miner.SetTimer(0, true);
}

} // namespace beam
