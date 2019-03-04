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

#include "processor.h"
#include "../core/treasury.h"
#include "../core/serialization_adapters.h"
#include "../utility/serialize.h"
#include "../utility/logger.h"
#include "../utility/logger_checkpoints.h"

namespace beam {

void NodeProcessor::OnCorrupted()
{
	CorruptionException exc;
	exc.m_sErr = "node data";
	throw exc;
}

NodeProcessor::Horizon::Horizon()
	:m_Branching(MaxHeight)
	,m_SchwarzschildLo(MaxHeight)
	,m_SchwarzschildHi(MaxHeight)
{
}

void NodeProcessor::Initialize(const char* szPath)
{
	StartParams sp; // defaults
	Initialize(szPath, sp);
}

void NodeProcessor::Initialize(const char* szPath, const StartParams& sp)
{
	m_DB.Open(szPath);
	m_DbTx.Start(m_DB);

	if (sp.m_CheckIntegrityAndVacuum)
	{
		LOG_INFO() << "DB integrity check...";
		m_DB.CheckIntegrity();
		Vacuum();
	}

	Merkle::Hash hv;
	Blob blob(hv);

	ZeroObject(m_Extra);
	m_Extra.m_LoHorizon = m_DB.ParamIntGetDef(NodeDB::ParamID::LoHorizon, Rules::HeightGenesis - 1);
	m_Extra.m_Fossil = m_DB.ParamIntGetDef(NodeDB::ParamID::FossilHeight, Rules::HeightGenesis - 1);
	m_Extra.m_TxoLo = m_DB.ParamIntGetDef(NodeDB::ParamID::HeightTxoLo, Rules::HeightGenesis - 1);
	m_Extra.m_TxoHi = m_DB.ParamIntGetDef(NodeDB::ParamID::HeightTxoHi, Rules::HeightGenesis - 1);

	if (!m_DB.ParamGet(NodeDB::ParamID::CfgChecksum, NULL, &blob))
	{
		blob = Blob(Rules::get().Checksum);
		m_DB.ParamSet(NodeDB::ParamID::CfgChecksum, NULL, &blob);
	}
	else
		if (hv != Rules::get().Checksum)
		{
			std::ostringstream os;
			os << "Data configuration is incompatible: " << hv << ". Current configuration: " << Rules::get().Checksum;
			throw std::runtime_error(os.str());
		}

	ZeroObject(m_SyncData);

	if (sp.m_ResetCursor)
	{
		m_DB.ParamSet(NodeDB::ParamID::SyncTarget, NULL, NULL);
		m_DB.ParamGet(NodeDB::ParamID::SyncData, nullptr, nullptr);
	}
	else
	{
		blob.p = &m_SyncData;
		blob.n = sizeof(m_SyncData);
		m_DB.ParamGet(NodeDB::ParamID::SyncData, nullptr, &blob);

		LogSyncData();
	}

	m_nSizeUtxoComission = 0;

	TxoID nTreasury = 0;

	if (Rules::get().TreasuryChecksum == Zero)
		m_Extra.m_TreasuryHandled = true;
	else
	{
		if (m_DB.ParamGet(NodeDB::ParamID::Treasury, &nTreasury, nullptr, nullptr))
			m_Extra.m_TreasuryHandled = true;
	}

	if (sp.m_ResetCursor)
	{
		m_DB.ResetCursor();

		m_DB.TxoDelFrom(nTreasury);
		m_DB.TxoDelSpentFrom(Rules::HeightGenesis);
	}

	InitCursor();

	InitializeUtxos();
	m_Extra.m_Txos = get_TxosBefore(m_Cursor.m_ID.m_Height + 1);

	OnHorizonChanged();

	if (!sp.m_ResetCursor)
		TryGoUp();
}

void NodeProcessor::LogSyncData()
{
	if (!m_SyncData.m_Target.m_Row)
		return;

	LOG_INFO() << "Fast-sync mode up to height " << m_SyncData.m_Target.m_Height;
}

NodeProcessor::~NodeProcessor()
{
	if (m_DbTx.IsInProgress())
	{
		try {
			m_DbTx.Commit();
		} catch (const CorruptionException& e) {
			LOG_ERROR() << "DB Commit failed: %s" << e.m_sErr;
		}
	}
}

void NodeProcessor::OnHorizonChanged()
{
	m_Horizon.m_SchwarzschildHi = std::max(m_Horizon.m_SchwarzschildHi, m_Horizon.m_Branching);
	m_Horizon.m_SchwarzschildHi = std::max(m_Horizon.m_SchwarzschildHi, (Height) Rules::get().Macroblock.MaxRollback);
	m_Horizon.m_SchwarzschildLo = std::max(m_Horizon.m_SchwarzschildLo, m_Horizon.m_SchwarzschildHi);

	if (PruneOld())
		Vacuum();
}

void NodeProcessor::Vacuum()
{
	if (m_DbTx.IsInProgress())
		m_DbTx.Commit();

	LOG_INFO() << "DB compacting...";
	m_DB.Vacuum();
	LOG_INFO() << "DB compacting completed";

	m_DbTx.Start(m_DB);
}

void NodeProcessor::CommitDB()
{
	if (m_DbTx.IsInProgress())
	{
		m_DbTx.Commit();
		m_DbTx.Start(m_DB);
	}
}

void NodeProcessor::InitCursor()
{
	if (m_DB.get_Cursor(m_Cursor.m_Sid))
	{
		m_DB.get_State(m_Cursor.m_Sid.m_Row, m_Cursor.m_Full);
		m_Cursor.m_Full.get_ID(m_Cursor.m_ID);

		m_DB.get_PredictedStatesHash(m_Cursor.m_HistoryNext, m_Cursor.m_Sid);

		NodeDB::StateID sid = m_Cursor.m_Sid;
		if (m_DB.get_Prev(sid))
			m_DB.get_PredictedStatesHash(m_Cursor.m_History, sid);
		else
			ZeroObject(m_Cursor.m_History);
	}
	else
	{
		ZeroObject(m_Cursor);
		m_Cursor.m_ID.m_Hash = Rules::get().Prehistoric;
	}

	m_Cursor.m_DifficultyNext = get_NextDifficulty();
}

void NodeProcessor::CongestionCache::Clear()
{
	while (!m_lstTips.empty())
		Delete(&m_lstTips.front());
}

void NodeProcessor::CongestionCache::Delete(TipCongestion* pVal)
{
	m_lstTips.erase(TipList::s_iterator_to(*pVal));
	delete pVal;
}

NodeProcessor::CongestionCache::TipCongestion* NodeProcessor::CongestionCache::Find(const NodeDB::StateID& sid)
{
	TipCongestion* pRet = nullptr;

	for (TipList::iterator it = m_lstTips.begin(); m_lstTips.end() != it; it++)
	{
		TipCongestion& x = *it;
		if (!x.IsContained(sid))
			continue;

		// in case of several matches prefer the one with lower height
		if (pRet && (pRet->m_Height <= x.m_Height))
			continue;

		pRet = &x;
	}

	return pRet;
}

bool NodeProcessor::CongestionCache::TipCongestion::IsContained(const NodeDB::StateID& sid)
{
	if (sid.m_Height > m_Height)
		return false;

	Height dh = m_Height - sid.m_Height;
	if (dh >= m_Rows.size())
		return false;

	return (m_Rows.at(dh) == sid.m_Row);
}

NodeProcessor::CongestionCache::TipCongestion* NodeProcessor::EnumCongestionsInternal()
{
	assert(m_Extra.m_TreasuryHandled);

	CongestionCache cc;
	cc.m_lstTips.swap(m_CongestionCache.m_lstTips);

	CongestionCache::TipCongestion* pMaxTarget = nullptr;

	// Find all potentially missing data
	NodeDB::WalkerState ws(m_DB);
	for (m_DB.EnumTips(ws); ws.MoveNext(); )
	{
		NodeDB::StateID& sid = ws.m_Sid; // alias
		if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(sid.m_Row))
			continue;

		Difficulty::Raw wrk;
		m_DB.get_ChainWork(sid.m_Row, wrk);

		if (wrk < m_Cursor.m_Full.m_ChainWork)
			continue; // not interested in tips behind the current cursor

		CongestionCache::TipCongestion* pEntry = nullptr;
		bool bCheckCache = true;
		bool bNeedHdrs = false;

		while (true)
		{
			if (bCheckCache)
			{
				CongestionCache::TipCongestion* p = cc.Find(sid);
				if (p)
				{
					assert(p->m_Height >= sid.m_Height);
					while (p->m_Height > sid.m_Height)
					{
						p->m_Height--;
						p->m_Rows.pop_front();
					}

					if (pEntry)
					{
						for (size_t i = pEntry->m_Rows.size(); i--; p->m_Height++)
							p->m_Rows.push_front(pEntry->m_Rows.at(i));

						m_CongestionCache.Delete(pEntry);
					}

					cc.m_lstTips.erase(CongestionCache::TipList::s_iterator_to(*p));
					m_CongestionCache.m_lstTips.push_back(*p);

					while (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(p->m_Rows.at(p->m_Rows.size() - 1)))
						p->m_Rows.pop_back(); // already retrieved

					assert(p->m_Rows.size());

					sid.m_Row = p->m_Rows.at(p->m_Rows.size() - 1);
					sid.m_Height = p->m_Height - (p->m_Rows.size() - 1);

					pEntry = p;
					bCheckCache = false;
				}
			}

			if (!pEntry)
			{
				pEntry = new CongestionCache::TipCongestion;
				m_CongestionCache.m_lstTips.push_back(*pEntry);

				pEntry->m_Height = sid.m_Height;
			}

			if (bCheckCache)
			{
				CongestionCache::TipCongestion* p = m_CongestionCache.Find(sid);
				if (p)
				{
					assert(p != pEntry);

					// copy the rest
					for (size_t i = p->m_Height - sid.m_Height; i < p->m_Rows.size(); i++)
						pEntry->m_Rows.push_back(p->m_Rows.at(i));

					sid.m_Row = p->m_Rows.at(p->m_Rows.size() - 1);
					sid.m_Height = p->m_Height - (p->m_Rows.size() - 1);

					bCheckCache = false;
				}
			}

			if (pEntry->m_Height >= sid.m_Height + pEntry->m_Rows.size())
				pEntry->m_Rows.push_back(sid.m_Row);

			if (Rules::HeightGenesis == sid.m_Height)
				break;

			if (!m_DB.get_Prev(sid))
			{
				bNeedHdrs = true;
				break;
			}

			if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(sid.m_Row))
				break;
		}

		assert(pEntry && pEntry->m_Rows.size());
		pEntry->m_bNeedHdrs = bNeedHdrs;

		if (!bNeedHdrs && (!pMaxTarget || (pMaxTarget->m_Height < pEntry->m_Height)))
			pMaxTarget = pEntry;
	}

	return pMaxTarget;
}

void NodeProcessor::EnumCongestions(uint32_t nMaxBlocksBacklog)
{
	if (!m_Extra.m_TreasuryHandled)
	{
		Block::SystemState::ID id;
		ZeroObject(id);
		RequestData(id, true, nullptr, 0);
		return;
	}

	CongestionCache::TipCongestion* pMaxTarget = EnumCongestionsInternal();

	// Check the fast-sync status
	if (pMaxTarget)
	{
		bool bFirstTime =
			!m_SyncData.m_Target.m_Row &&
			(pMaxTarget->m_Height > m_Cursor.m_ID.m_Height + m_Horizon.m_SchwarzschildHi + m_Horizon.m_SchwarzschildHi / 2);

		if (bFirstTime)
		{
			// first time target acquisition
			// TODO - verify the headers w.r.t. difficulty and Chainwork
			m_SyncData.m_h0 = pMaxTarget->m_Height - pMaxTarget->m_Rows.size();

			if (pMaxTarget->m_Height > m_Horizon.m_SchwarzschildLo)
				m_SyncData.m_TxoLo = pMaxTarget->m_Height - m_Horizon.m_SchwarzschildLo;

			m_SyncData.m_TxoLo = std::max(m_SyncData.m_TxoLo, m_Extra.m_TxoLo);
		}

		// check if the target should be moved fwd
		bool bTrgChange =
			(m_SyncData.m_Target.m_Row || bFirstTime) &&
			(pMaxTarget->m_Height > m_SyncData.m_Target.m_Height + m_Horizon.m_SchwarzschildHi);

		if (bTrgChange)
		{
			Height hTargetPrev = bFirstTime ? (pMaxTarget->m_Height - pMaxTarget->m_Rows.size()) : m_SyncData.m_Target.m_Height;

			m_SyncData.m_Target.m_Height = pMaxTarget->m_Height - m_Horizon.m_SchwarzschildHi;
			m_SyncData.m_Target.m_Row = pMaxTarget->m_Rows.at(pMaxTarget->m_Height - m_SyncData.m_Target.m_Height);

			if (m_SyncData.m_TxoLo)
				// ensure no old blocks, which could be generated with incorrect TxLo
				DeleteBlocksInRange(m_SyncData.m_Target, hTargetPrev);

			Blob blob(&m_SyncData, sizeof(m_SyncData));
			m_DB.ParamSet(NodeDB::ParamID::SyncData, nullptr, &blob);
		}

		if (bFirstTime)
			LogSyncData();
	}

	// request missing data
	for (CongestionCache::TipList::iterator it = m_CongestionCache.m_lstTips.begin(); m_CongestionCache.m_lstTips.end() != it; it++)
	{
		CongestionCache::TipCongestion& x = *it;

		Block::SystemState::ID id;

		if (!x.m_bNeedHdrs)
		{
			if (m_SyncData.m_Target.m_Row && !x.IsContained(m_SyncData.m_Target))
				continue; // ignore irrelevant branches

			uint32_t nRequested = 0;

			for (size_t i = x.m_Rows.size(); i--; )
			{
				NodeDB::StateID sid;
				sid.m_Height = x.m_Height - i;
				sid.m_Row = x.m_Rows.at(i);

				if (NodeDB::StateFlags::Functional & m_DB.GetStateFlags(sid.m_Row))
					continue;

				m_DB.get_StateID(sid, id);
				RequestDataInternal(id, sid.m_Row, true, x.m_Height);

				if (++nRequested >= nMaxBlocksBacklog)
					break;
			}
		}
		else
		{
			uint64_t rowid = x.m_Rows.at(x.m_Rows.size() - 1);

			Block::SystemState::Full s;
			m_DB.get_State(rowid, s);

			id.m_Height = s.m_Height - 1;
			id.m_Hash = s.m_Prev;

			RequestDataInternal(id, rowid, false, x.m_Height);
		}
	}
}

void NodeProcessor::RequestDataInternal(const Block::SystemState::ID& id, uint64_t row, bool bBlock, Height hTarget)
{
	if (id.m_Height >= m_Extra.m_LoHorizon)
	{
		PeerID peer;
		bool bPeer = m_DB.get_Peer(row, peer);

		RequestData(id, bBlock, bPeer ? &peer : NULL, hTarget);
	}
	else
	{
		LOG_WARNING() << id << " State unreachable!"; // probably will pollute the log, but it's a critical situation anyway
	}
}

void NodeProcessor::TryGoUp()
{
	if (!m_Extra.m_TreasuryHandled)
		return;

	bool bDirty = false;
	uint64_t rowid = m_Cursor.m_Sid.m_Row;

	while (true)
	{
		if (m_SyncData.m_Target.m_Row)
		{
			if (!(NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(m_SyncData.m_Target.m_Row)))
				return;

			bDirty = true;

			RollbackTo(m_SyncData.m_h0);
			GoUpFast();
			continue;
		}

		NodeDB::StateID sidTrg;
		Difficulty::Raw wrkTrg;

		{
			NodeDB::WalkerState ws(m_DB);
			m_DB.EnumFunctionalTips(ws);

			if (!ws.MoveNext())
			{
				assert(!m_Cursor.m_Sid.m_Row);
				break; // nowhere to go
			}

			sidTrg = ws.m_Sid;
			m_DB.get_ChainWork(sidTrg.m_Row, wrkTrg);

			assert(wrkTrg >= m_Cursor.m_Full.m_ChainWork);
			if (wrkTrg == m_Cursor.m_Full.m_ChainWork)
				break; // already at maximum (though maybe at different tip)
		}

		bDirty = true;

		// Calculate the path
		std::vector<uint64_t> vPath;
		while (true)
		{
			vPath.push_back(sidTrg.m_Row);

			if (!m_DB.get_Prev(sidTrg))
			{
				sidTrg.SetNull();
				break;
			}

			if (NodeDB::StateFlags::Active & m_DB.GetStateFlags(sidTrg.m_Row))
				break;
		}

		RollbackTo(sidTrg.m_Height);

		bool bPathOk = true;

		for (size_t i = vPath.size(); i--; )
		{
			if (!GoForward(vPath[i], nullptr))
			{
				bPathOk = false;
				break;
			}
		}

		if (bPathOk)
			break; // at position
	}

	if (bDirty)
	{
		PruneOld();
		if (m_Cursor.m_Sid.m_Row != rowid)
			OnNewState();
	}
}

void NodeProcessor::GoUpFast()
{
	assert(m_SyncData.m_Target.m_Row);
	assert(m_Cursor.m_ID.m_Height == m_SyncData.m_h0);

	bool bRet = GoUpFastInternal();

	if (bRet)
	{
		// raise fossil height, hTxoLo, hTxoHi
		RaiseFossil(m_Cursor.m_ID.m_Height);
		RaiseTxoHi(m_Cursor.m_ID.m_Height);
		RaiseTxoLo(m_SyncData.m_TxoLo);

		m_Extra.m_LoHorizon = m_Cursor.m_ID.m_Height;
		m_DB.ParamSet(NodeDB::ParamID::LoHorizon, &m_Extra.m_LoHorizon, NULL);
	}
	else
	{
		// rapid rollback
		RollbackTo(m_SyncData.m_h0);
		DeleteBlocksInRange(m_SyncData.m_Target, m_SyncData.m_h0);
	}

	ZeroObject(m_SyncData);
	m_DB.ParamSet(NodeDB::ParamID::SyncData, nullptr, nullptr);
}

void NodeProcessor::DeleteBlocksInRange(const NodeDB::StateID& sidTop, Height hStop)
{
	for (NodeDB::StateID sid = sidTop; sid.m_Height > hStop; )
	{
		m_DB.DelStateBlockAll(sid.m_Row);
		m_DB.SetStateNotFunctional(sid.m_Row);

		if (!m_DB.get_Prev(sid))
			sid.SetNull();
	}
}

bool NodeProcessor::GoUpFastInternal()
{
	std::vector<uint64_t> vPath;
	vPath.reserve(m_SyncData.m_Target.m_Height - m_SyncData.m_h0);

	for (NodeDB::StateID sid = m_SyncData.m_Target; sid.m_Height != m_SyncData.m_h0; )
	{
		vPath.push_back(sid.m_Row);
		if (!m_DB.get_Prev(sid))
			sid.SetNull();
	}

	TxBase::Context ctx;
	ctx.m_Height.m_Min = m_SyncData.m_h0 + 1;
	ctx.m_Height.m_Max = m_SyncData.m_Target.m_Height;
	ctx.m_bBlockMode = true;

	Transaction txDummy;
	txDummy.m_Offset = Zero;

	verify(ValidateAndSummarize(ctx, txDummy, txDummy.get_Reader(), true, false));

	for (size_t i = vPath.size(); i--; )
	{
		if (!GoForward(vPath[i], &ctx))
			return false;
	}

	bool bOk =
		ValidateAndSummarize(ctx, txDummy, txDummy.get_Reader(), false, true) && // flush batch context
		ctx.IsValidBlock(); // validate macroblock wrt height range

	if (!bOk)
		return false;

	// Make sure no naked UTXOs are left
	TxoID id0 = get_TxosBefore(m_SyncData.m_h0 + 1);

	NodeDB::WalkerTxo wlk(m_DB);
	for (m_DB.EnumTxos(wlk, id0); wlk.MoveNext(); )
	{
		if (wlk.m_SpendHeight != MaxHeight)
			continue;

		if (TxoIsNaked(wlk.m_Value))
			return false;
	}

	return true;
}

Height NodeProcessor::PruneOld()
{
	if (m_Cursor.m_Sid.m_Height < Rules::HeightGenesis)
		return 0;

	Height hRet = 0;

	if (m_Cursor.m_Sid.m_Height > m_Horizon.m_Branching + Rules::HeightGenesis - 1)
	{
		Height h = m_Cursor.m_Sid.m_Height - m_Horizon.m_Branching;

		while (true)
		{
			uint64_t rowid;
			{
				NodeDB::WalkerState ws(m_DB);
				m_DB.EnumTips(ws);
				if (!ws.MoveNext())
					break;
				if (ws.m_Sid.m_Height >= h)
					break;

				rowid = ws.m_Sid.m_Row;
			}

			do
			{
				if (!m_DB.DeleteState(rowid, rowid))
					break;
				hRet++;

			} while (rowid);
		}
	}

	if (m_Cursor.m_Sid.m_Height - 1 > m_Extra.m_Fossil + Rules::get().Macroblock.MaxRollback)
	{
		Height hTrg = m_Cursor.m_Sid.m_Height - 1 - Rules::get().Macroblock.MaxRollback;
		assert(hTrg > m_Extra.m_Fossil);

		AdjustFossilEnd(hTrg); // should be removed once macroblocks are completely erased
		hRet += RaiseFossil(hTrg);
	}

	if (m_Cursor.m_Sid.m_Height - 1 > m_Extra.m_TxoLo + m_Horizon.m_SchwarzschildLo)
		hRet += RaiseTxoLo(m_Cursor.m_Sid.m_Height - 1 - m_Horizon.m_SchwarzschildLo);

	if (m_Cursor.m_Sid.m_Height - 1 > m_Extra.m_TxoHi + m_Horizon.m_SchwarzschildHi)
		hRet += RaiseTxoHi(m_Cursor.m_Sid.m_Height - 1 - m_Horizon.m_SchwarzschildHi);

	return hRet;
}

Height NodeProcessor::RaiseFossil(Height hTrg)
{
	if (hTrg <= m_Extra.m_Fossil)
		return 0;

	Height hRet = 0;

	while (m_Extra.m_Fossil < hTrg)
	{
		m_Extra.m_Fossil++;

		NodeDB::WalkerState ws(m_DB);
		for (m_DB.EnumStatesAt(ws, m_Extra.m_Fossil); ws.MoveNext(); )
		{
			if (NodeDB::StateFlags::Active & m_DB.GetStateFlags(ws.m_Sid.m_Row))
				m_DB.DelStateBlockPRB(ws.m_Sid.m_Row);
			else
			{
				m_DB.SetStateNotFunctional(ws.m_Sid.m_Row);

				m_DB.DelStateBlockAll(ws.m_Sid.m_Row);
				m_DB.set_Peer(ws.m_Sid.m_Row, NULL);
			}

			hRet++;
		}

	}

	m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &m_Extra.m_Fossil, NULL);
	return hRet;
}

Height NodeProcessor::RaiseTxoLo(Height hTrg)
{
	if (hTrg <= m_Extra.m_TxoLo)
		return 0;

	m_Extra.m_TxoLo = hTrg;
	m_DB.ParamSet(NodeDB::ParamID::HeightTxoLo, &m_Extra.m_TxoLo, NULL);

	return m_DB.DeleteSpentTxos(m_Extra.m_TxoLo);
}

Height NodeProcessor::RaiseTxoHi(Height hTrg)
{
	if (hTrg <= m_Extra.m_TxoHi)
		return 0;

	Height hRet = 0;

	NodeDB::WalkerTxo wlk(m_DB);
	for (m_DB.EnumTxosBySpent(wlk, HeightRange(m_Extra.m_TxoHi + 1, hTrg)); wlk.MoveNext(); )
	{
		assert(wlk.m_SpendHeight <= hTrg);

		if (TxoIsNaked(wlk.m_Value))
			continue;

		uint8_t pNaked[s_TxoNakedMax];
		TxoToNaked(pNaked, wlk.m_Value);

		m_DB.TxoSetValue(wlk.m_ID, wlk.m_Value);
		hRet++;
	}

	m_Extra.m_TxoHi = hTrg;
	m_DB.ParamSet(NodeDB::ParamID::HeightTxoHi, &m_Extra.m_TxoHi, NULL);

	return hRet;
}

void NodeProcessor::TxoToNaked(uint8_t* pBuf, Blob& v)
{
	if (v.n < s_TxoNakedMin)
		OnCorrupted();

	const uint8_t* pSrc = reinterpret_cast<const uint8_t*>(v.p);
	v.p = pBuf;

	if (!(0x30 & pSrc[0]))
	{
		// simple case - just remove some flags and truncate.
		memcpy(pBuf, pSrc, s_TxoNakedMin);
		v.n = s_TxoNakedMin;
		pBuf[0] &= 3;

		return;
	}

	// complex case - the UTXO has either AssetID or Incubation period. Utxo must be re-read
	Deserializer der;
	der.reset(pSrc, v.n);

	Output outp;
	der & outp;

	outp.m_pConfidential.reset();
	outp.m_pPublic.reset();
	outp.m_AssetID = Zero;

	StaticBufferSerializer<s_TxoNakedMax> ser;
	ser & outp;

	SerializeBuffer sb = ser.buffer();
	assert(sb.second <= s_TxoNakedMax);

	memcpy(pBuf, sb.first, sb.second);
	v.n = static_cast<uint32_t>(sb.second);
}

bool NodeProcessor::TxoIsNaked(const Blob& v)
{
	if (v.n < s_TxoNakedMin)
		OnCorrupted();

	const uint8_t* pSrc = reinterpret_cast<const uint8_t*>(v.p);

	return !(pSrc[0] & 0xc);

}

void NodeProcessor::get_Definition(Merkle::Hash& hv, const Merkle::Hash& hvHist)
{
	m_Utxos.get_Hash(hv);
	Merkle::Interpret(hv, hvHist, false);
}

void NodeProcessor::get_Definition(Merkle::Hash& hv, bool bForNextState)
{
	get_Definition(hv, bForNextState ? m_Cursor.m_HistoryNext : m_Cursor.m_History);
}

struct NodeProcessor::RollbackData
{
	// helper structures for rollback
	struct Utxo {
		Height m_Maturity; // the extra info we need to restore an UTXO, in addition to the Input.
		TxoID m_ID;
	};

	ByteBuffer m_Buf;

	void Import(const TxVectors::Perishable& txv)
	{
		if (txv.m_vInputs.empty())
			m_Buf.push_back(0); // make sure it's not empty, even if there were no inputs, this is how we distinguish processed blocks.
		else
		{
			m_Buf.resize(sizeof(Utxo) * txv.m_vInputs.size());

			Utxo* pDst = reinterpret_cast<Utxo*>(&m_Buf.front());

			for (size_t i = 0; i < txv.m_vInputs.size(); i++)
			{
				Utxo& dst = pDst[i];
				const Input& src = *txv.m_vInputs[i];

				dst.m_Maturity = src.m_Maturity;
				dst.m_ID = src.m_ID;
			}
		}
	}

	void Export(TxVectors::Perishable& txv) const
	{
		if (txv.m_vInputs.empty())
			return;

		if (sizeof(Utxo) * txv.m_vInputs.size() != m_Buf.size())
			OnCorrupted();

		const Utxo* pDst = reinterpret_cast<const Utxo*>(&m_Buf.front());

		for (size_t i = 0; i < txv.m_vInputs.size(); i++)
		{
			const Utxo& dst = pDst[i];
			Input& src = *txv.m_vInputs[i];

			src.m_Maturity = dst.m_Maturity;
			src.m_ID = dst.m_ID;
		}
	}
};

uint64_t NodeProcessor::ProcessKrnMmr(Merkle::Mmr& mmr, TxBase::IReader&& r, Height h, const Merkle::Hash& idKrn, TxKernel::Ptr* ppRes)
{
	uint64_t iRet = uint64_t (-1);

	for (uint64_t i = 0; r.m_pKernel && r.m_pKernel->m_Maturity == h; r.NextKernel(), i++)
	{
		Merkle::Hash hv;
		r.m_pKernel->get_ID(hv);
		mmr.Append(hv);

		if (hv == idKrn)
		{
			iRet = i; // found
			if (ppRes)
			{
				ppRes->reset(new TxKernel);
				**ppRes = *r.m_pKernel;
			}
		}
	}

	return iRet;
}

Height NodeProcessor::get_ProofKernel(Merkle::Proof& proof, TxKernel::Ptr* ppRes, const Merkle::Hash& idKrn)
{
	Height h = m_DB.FindKernel(idKrn);
	if (h < Rules::HeightGenesis)
		return h;

	uint64_t rowid = FindActiveAtStrict(h);

	ByteBuffer bbE;
	m_DB.GetStateBlock(rowid, NULL, &bbE, NULL);

	TxVectors::Eternal txve;
	TxVectors::Perishable txvp; // dummy

	Deserializer der;
	der.reset(bbE);
	der & txve;

	TxVectors::Reader r(txvp, txve);
	r.Reset();

	Merkle::FixedMmmr mmr;
	mmr.Reset(txve.m_vKernels.size());
	size_t iTrg = ProcessKrnMmr(mmr, std::move(r), 0, idKrn, ppRes);

	if (uint64_t(-1) == iTrg)
		OnCorrupted();

	mmr.get_Proof(proof, iTrg);
	return h;
}

bool NodeProcessor::HandleTreasury(const Blob& blob)
{
	assert(!m_Extra.m_TreasuryHandled);

	Deserializer der;
	der.reset(blob.p, blob.n);
	Treasury::Data td;

	try {
		der & td;
	} catch (const std::exception&) {
		LOG_WARNING() << "Treasury corrupt";
		return false;
	}

	if (!td.IsValid())
	{
		LOG_WARNING() << "Treasury validation failed";
		return false;
	}

	std::vector<Treasury::Data::Burst> vBursts = td.get_Bursts();

	std::ostringstream os;
	os << "Treasury check. Total bursts=" << vBursts.size();

	for (size_t i = 0; i < vBursts.size(); i++)
	{
		const Treasury::Data::Burst& b = vBursts[i];
		os << "\n\t" << "Height=" << b.m_Height << ", Value=" << b.m_Value;
	}

	LOG_INFO() << os.str();

	for (size_t iG = 0; iG < td.m_vGroups.size(); iG++)
	{
		if (!HandleValidatedTx(td.m_vGroups[iG].m_Data.get_Reader(), 0, true, NULL))
		{
			// undo partial changes
			while (iG--)
			{
				if (!HandleValidatedTx(td.m_vGroups[iG].m_Data.get_Reader(), 0, false, NULL))
					OnCorrupted(); // although should not happen anyway
			}

			LOG_WARNING() << "Treasury invalid";
			return false;
		}
	}

	Serializer ser;
	TxoID id0 = 0;

	for (size_t iG = 0; iG < td.m_vGroups.size(); iG++)
	{
		for (size_t i = 0; i < td.m_vGroups[iG].m_Data.m_vOutputs.size(); i++, id0++)
		{
			ser.reset();
			ser & *td.m_vGroups[iG].m_Data.m_vOutputs[i];

			SerializeBuffer sb = ser.buffer();
			m_DB.TxoAdd(id0, Blob(sb.first, static_cast<uint32_t>(sb.second)));
		}
	}

	m_Extra.m_TreasuryHandled = true;

	return true;
}

bool NodeProcessor::HandleBlock(const NodeDB::StateID& sid, TxBase::Context* pBatch)
{
	ByteBuffer bbP, bbE;
	m_DB.GetStateBlock(sid.m_Row, &bbP, &bbE, nullptr);

	Block::SystemState::Full s;
	m_DB.get_State(sid.m_Row, s); // need it for logging anyway

	Block::SystemState::ID id;
	s.get_ID(id);

	Block::Body block;
	try {
		Deserializer der;
		der.reset(bbP);
		der & Cast::Down<Block::BodyBase>(block);
		der & Cast::Down<TxVectors::Perishable>(block);

		der.reset(bbE);
		der & Cast::Down<TxVectors::Eternal>(block);
	}
	catch (const std::exception&) {
		LOG_WARNING() << id << " Block deserialization failed";
		return false;
	}

	std::vector<Merkle::Hash> vKrnID(block.m_vKernels.size()); // allocate mem for all kernel IDs, we need them for initial verification vs header, and at the end - to add to the kernel index.
	// better to allocate the memory, then to calculate IDs twice
	for (size_t i = 0; i < vKrnID.size(); i++)
		block.m_vKernels[i]->get_ID(vKrnID[i]);

	bool bFirstTime = (m_DB.get_StateTxos(sid.m_Row) == MaxHeight);
	if (bFirstTime)
	{
		Difficulty::Raw wrk = m_Cursor.m_Full.m_ChainWork + s.m_PoW.m_Difficulty;

		if (wrk != s.m_ChainWork)
		{
			LOG_WARNING() << id << " Chainwork expected=" << wrk <<", actual=" << s.m_ChainWork;
			return false;
		}

		if (m_Cursor.m_DifficultyNext.m_Packed != s.m_PoW.m_Difficulty.m_Packed)
		{
			LOG_WARNING() << id << " Difficulty expected=" << m_Cursor.m_DifficultyNext << ", actual=" << s.m_PoW.m_Difficulty;
			return false;
		}

		if (s.m_TimeStamp <= get_MovingMedian())
		{
			LOG_WARNING() << id << " Timestamp inconsistent wrt median";
			return false;
		}

		struct MyFlyMmr :public Merkle::FlyMmr {
			const Merkle::Hash* m_pHashes;
			virtual void LoadElement(Merkle::Hash& hv, uint64_t n) const override {
				hv = m_pHashes[n];
			}
		};

		MyFlyMmr fmmr;
		fmmr.m_Count = vKrnID.size();
		fmmr.m_pHashes = vKrnID.empty() ? NULL : &vKrnID.front();

		Merkle::Hash hv;
		fmmr.get_Hash(hv);

		if (s.m_Kernels != hv)
		{
			LOG_WARNING() << id << " Kernel commitment mismatch";
			return false;
		}

		bool bValid;

		if (pBatch)
		{
			// In batch mode some outputs may be 'naked'. We'll add them as-is
			class ReaderEx
				:public TxVectors::Reader
			{
				void SkipNaked()
				{
					while (m_pUtxoOut && IsNaked(*m_pUtxoOut))
						TxVectors::Reader::NextUtxoOut();
				}
			public:
				ReaderEx(const TxVectors::Perishable& p, const TxVectors::Eternal& e)
					:TxVectors::Reader(p, e) {
				}

				static bool IsNaked(const Output& x)
				{
					return !(x.m_pConfidential || x.m_pPublic);
				}

				void Clone(Ptr& pOut) override {
					pOut.reset(new ReaderEx(m_P, m_E));
				}
				void Reset() override {
					TxVectors::Reader::Reset();
					SkipNaked();
				}
				void NextUtxoOut() override {
					TxVectors::Reader::NextUtxoOut();
					SkipNaked();
				};

			};

			ReaderEx r(block, block);
			bValid = ValidateAndSummarize(*pBatch, block, std::move(r), false, false);

			for (size_t i = 0; i < block.m_vOutputs.size(); i++)
			{
				const Output& x = *block.m_vOutputs[i];
				if (ReaderEx::IsNaked(x))
				{
					// add it as-is
					ECC::Point::Native pt;
					if (pt.Import(x.m_Commitment))
						pBatch->m_Sigma += pt;
					else
						bValid = false;
				}
			}
				
		}
		else
			bValid = VerifyBlock(block, block.get_Reader(), sid.m_Height);

		if (!bValid)
		{
			LOG_WARNING() << id << " context-free verification failed";
			return false;
		}
	}

	bool bOk = HandleValidatedBlock(block.get_Reader(), block, sid.m_Height, true);
	if (!bOk)
		LOG_WARNING() << id << " invalid in its context";

	if (bFirstTime && bOk)
	{
		if (!pBatch || (sid.m_Height >= m_SyncData.m_TxoLo))
		{
			// check the validity of state description.
			Merkle::Hash hvDef;
			get_Definition(hvDef, true);

			if (s.m_Definition != hvDef)
			{
				LOG_WARNING() << id << " Header Definition mismatch";
				bOk = false;
			}
		}

		if (bOk)
		{
			ECC::Scalar offsAcc = block.m_Offset;

			if (sid.m_Height > Rules::HeightGenesis)
			{
				uint64_t row = sid.m_Row;
				if (!m_DB.get_Prev(row))
					OnCorrupted();

				AdjustOffset(offsAcc, row, true);
			}

			m_DB.set_StateExtra(sid.m_Row, &offsAcc);

			m_DB.set_StateTxos(sid.m_Row, &m_Extra.m_Txos);

			if (!pBatch)
			{
				// no need to adjust LoHorizon in batch mode
				assert(m_Extra.m_LoHorizon <= m_Cursor.m_Sid.m_Height);
				if (m_Cursor.m_Sid.m_Height - m_Extra.m_LoHorizon > Rules::get().Macroblock.MaxRollback)
				{
					m_Extra.m_LoHorizon = m_Cursor.m_Sid.m_Height - Rules::get().Macroblock.MaxRollback;
					m_DB.ParamSet(NodeDB::ParamID::LoHorizon, &m_Extra.m_LoHorizon, NULL);
				}
			}

		}
		else
			verify(HandleValidatedBlock(block.get_Reader(), block, sid.m_Height, false));
	}

	if (bOk)
	{
		for (size_t i = 0; i < vKrnID.size(); i++)
			m_DB.InsertKernel(vKrnID[i], sid.m_Height);

		for (size_t i = 0; i < block.m_vInputs.size(); i++)
		{
			const Input& x = *block.m_vInputs[i];
			m_DB.TxoSetSpent(x.m_ID, sid.m_Height);
		}

		assert(m_Extra.m_Txos > block.m_vOutputs.size());
		TxoID id0 = m_Extra.m_Txos - block.m_vOutputs.size() - 1;
		Serializer ser;

		for (size_t i = 0; i < block.m_vOutputs.size(); i++, id0++)
		{
			ser.reset();
			ser & *block.m_vOutputs[i];

			SerializeBuffer sb = ser.buffer();
			m_DB.TxoAdd(id0, Blob(sb.first, static_cast<uint32_t>(sb.second)));
		}

		auto r = block.get_Reader();
		r.Reset();
		RecognizeUtxos(std::move(r), sid.m_Height);

		LOG_INFO() << id << " Block interpreted.";
	}

	return bOk;
}

void NodeProcessor::AdjustOffset(ECC::Scalar& offs, uint64_t rowid, bool bAdd)
{
	ECC::Scalar offsPrev;
	if (!m_DB.get_StateExtra(rowid, offsPrev))
		OnCorrupted();

	ECC::Scalar::Native s(offsPrev);
	if (!bAdd)
		s = -s;

	s += offs;
	offs = s;
}

void NodeProcessor::RecognizeUtxos(TxBase::IReader&& r, Height hMax)
{
	NodeDB::WalkerEvent wlk(m_DB);

	for ( ; r.m_pUtxoIn; r.NextUtxoIn())
	{
		const Input& x = *r.m_pUtxoIn;
		assert(x.m_Maturity); // must've already been validated

		const UtxoEvent::Key& key = x.m_Commitment;

		m_DB.FindEvents(wlk, Blob(&key, sizeof(key))); // raw find (each time from scratch) is suboptimal, because inputs are sorted, should be a way to utilize this
		if (wlk.MoveNext())
		{
			if (wlk.m_Body.n < sizeof(UtxoEvent::Value))
				OnCorrupted();

			UtxoEvent::Value evt = *reinterpret_cast<const UtxoEvent::Value*>(wlk.m_Body.p); // copy
			evt.m_Maturity = x.m_Maturity;
			evt.m_Added = 0;

			// In case of macroblock we can't recover the original input height.
			m_DB.InsertEvent(hMax, Blob(&evt, sizeof(evt)), Blob(&key, sizeof(key)));
			OnUtxoEvent(evt);
		}
	}

	for (; r.m_pUtxoOut; r.NextUtxoOut())
	{
		const Output& x = *r.m_pUtxoOut;

		Key::IDV kidv;
		if (Recover(kidv, x))
		{
			// filter-out dummies
			if (IsDummy(kidv))
			{
				OnDummy(kidv, hMax);
				continue;
			}

			// bingo!
			UtxoEvent::Value evt;
			evt.m_Kidv = kidv;
			evt.m_Added = 1;
			evt.m_AssetID = r.m_pUtxoOut->m_AssetID;

			Height h;
			if (x.m_Maturity)
			{
				evt.m_Maturity = x.m_Maturity;
				// try to reverse-engineer the original block from the maturity
				h = x.m_Maturity - x.get_MinMaturity(0);
			}
			else
			{
				h = hMax;
				evt.m_Maturity = x.get_MinMaturity(h);
			}

			const UtxoEvent::Key& key = x.m_Commitment;
			m_DB.InsertEvent(h, Blob(&evt, sizeof(evt)), Blob(&key, sizeof(key)));
			OnUtxoEvent(evt);
		}
	}
}

void NodeProcessor::RescanOwnedTxos()
{
	LOG_INFO() << "Rescanning owned Txos...";

	m_DB.DeleteEventsFrom(Rules::HeightGenesis - 1);

	struct TxoRecover
		:public ITxoRecover
	{
		uint32_t m_Total = 0;
		uint32_t m_Unspent = 0;

		TxoRecover(NodeProcessor& x) :ITxoRecover(x) {}

		virtual bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp, const Key::IDV& kidv) override
		{
			if (IsDummy(kidv))
			{
				m_This.OnDummy(kidv, hCreate);
				return true;
			}

			UtxoEvent::Value evt;
			evt.m_Kidv = kidv;
			evt.m_Maturity = outp.get_MinMaturity(hCreate);
			evt.m_Added = 1;
			evt.m_AssetID = outp.m_AssetID;

			const UtxoEvent::Key& key = outp.m_Commitment;

			m_This.get_DB().InsertEvent(hCreate, Blob(&evt, sizeof(evt)), Blob(&key, sizeof(key)));
			m_This.OnUtxoEvent(evt);

			m_Total++;

			if (MaxHeight == wlk.m_SpendHeight)
				m_Unspent++;
			else
			{
				evt.m_Added = 0;
				m_This.get_DB().InsertEvent(wlk.m_SpendHeight, Blob(&evt, sizeof(evt)), Blob(&key, sizeof(key)));
				m_This.OnUtxoEvent(evt);
			}

			return true;
		}
	};

	TxoRecover wlk(*this);
	EnumTxos(wlk);

	LOG_INFO() << "Recovered " << wlk.m_Unspent << "/" << wlk.m_Total << " unspent/total Txos";
}

bool NodeProcessor::IsDummy(const Key::IDV&  kidv)
{
	return !kidv.m_Value && (Key::Type::Decoy == kidv.m_Type);
}

bool NodeProcessor::HandleValidatedTx(TxBase::IReader&& r, Height h, bool bFwd, const Height* pHMax)
{
	uint32_t nInp = 0, nOut = 0;
	r.Reset();

	bool bOk = true;
	for (; r.m_pUtxoIn; r.NextUtxoIn(), nInp++)
		if (!HandleBlockElement(*r.m_pUtxoIn, h, pHMax, bFwd))
		{
			bOk = false;
			break;
		}

	if (bOk)
		for (; r.m_pUtxoOut; r.NextUtxoOut(), nOut++)
			if (!HandleBlockElement(*r.m_pUtxoOut, h, pHMax, bFwd))
			{
				bOk = false;
				break;
			}

	if (bOk)
		return true;

	if (!bFwd)
		OnCorrupted();

	// Rollback all the changes. Must succeed!
	r.Reset();

	for (; nOut--; r.NextUtxoOut())
		HandleBlockElement(*r.m_pUtxoOut, h, pHMax, false);

	for (; nInp--; r.NextUtxoIn())
		HandleBlockElement(*r.m_pUtxoIn, h, pHMax, false);

	return false;
}

bool NodeProcessor::HandleValidatedBlock(TxBase::IReader&& r, const Block::BodyBase& body, Height h, bool bFwd, const Height* pHMax)
{
	// make sure we adjust txo count, to prevent the same Txos for consecutive blocks after cut-through
	if (!bFwd)
	{
		assert(m_Extra.m_Txos);
		m_Extra.m_Txos--;
	}

	if (!HandleValidatedTx(std::move(r), h, bFwd, pHMax))
		return false;

	// currently there's no extra info in the block that's needed

	if (bFwd)
		m_Extra.m_Txos++;

	return true;
}

bool NodeProcessor::HandleBlockElement(const Input& v, Height h, const Height* pHMax, bool bFwd)
{
	UtxoTree::Cursor cu;
	UtxoTree::MyLeaf* p;
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;

	if (bFwd)
	{
		struct Traveler :public UtxoTree::ITraveler {
			virtual bool OnLeaf(const RadixTree::Leaf& x) override {
				return false; // stop iteration
			}
		} t;


		UtxoTree::Key kMin, kMax;

		if (!pHMax)
		{
			d.m_Maturity = 0;
			kMin = d;
			d.m_Maturity = h - 1;
			kMax = d;
		}
		else
		{
			if (v.m_Maturity >= *pHMax)
				return false;

			d.m_Maturity = v.m_Maturity;
			kMin = d;
			kMax = kMin;
		}

		t.m_pCu = &cu;
		t.m_pBound[0] = kMin.m_pArr;
		t.m_pBound[1] = kMax.m_pArr;

		if (m_Utxos.Traverse(t))
			return false;

		p = &Cast::Up<UtxoTree::MyLeaf>(cu.get_Leaf());

		d = p->m_Key;
		assert(d.m_Commitment == v.m_Commitment);
		assert(d.m_Maturity <= (pHMax ? *pHMax : h));

		TxoID nID = p->m_ID;

		if (!p->IsExt())
			m_Utxos.Delete(cu);
		else
		{
			nID = p->PopID();
			cu.InvalidateElement();
		}

		if (!pHMax)
		{
			Cast::NotConst(v).m_Maturity = d.m_Maturity;
			Cast::NotConst(v).m_ID = nID;
		}
	} else
	{
		d.m_Maturity = v.m_Maturity;

		bool bCreate = true;
		UtxoTree::Key key;
		key = d;

		p = m_Utxos.Find(cu, key, bCreate);

		if (bCreate)
			p->m_ID = v.m_ID;
		else
		{
			p->PushID(v.m_ID);
			cu.InvalidateElement();
		}
	}

	return true;
}

bool NodeProcessor::HandleBlockElement(const Output& v, Height h, const Height* pHMax, bool bFwd)
{
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;
	d.m_Maturity = v.get_MinMaturity(h);

	if (pHMax)
	{
		if (v.m_Maturity < d.m_Maturity)
			return false; // decrease not allowed

		d.m_Maturity = v.m_Maturity;
	}

	UtxoTree::Key key;
	key = d;

	UtxoTree::Cursor cu;
	bool bCreate = true;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, key, bCreate);

	cu.InvalidateElement();

	if (bFwd)
	{
		TxoID nID = m_Extra.m_Txos;

		if (bCreate)
			p->m_ID = nID;
		else
		{
			// protect again overflow attacks, though it's highly unlikely (Input::Count is currently limited to 32 bits, it'd take millions of blocks)
			Input::Count nCountInc = p->get_Count() + 1;
			if (!nCountInc)
				return false;

			p->PushID(nID);
		}

		m_Extra.m_Txos++;

	} else
	{
		assert(m_Extra.m_Txos);
		m_Extra.m_Txos--;

		if (!p->IsExt())
			m_Utxos.Delete(cu);
		else
			p->PopID();
	}

	return true;
}

bool NodeProcessor::GoForward(uint64_t row, TxBase::Context* pBatch)
{
	NodeDB::StateID sid;
	sid.m_Height = m_Cursor.m_Sid.m_Height + 1;
	sid.m_Row = row;

	if (HandleBlock(sid, pBatch))
	{
		m_DB.MoveFwd(sid);
		InitCursor();
		return true;
	}

	if (!pBatch)
	{
		m_DB.DelStateBlockAll(row);
		m_DB.SetStateNotFunctional(row);

		PeerID peer;
		if (m_DB.get_Peer(row, peer))
		{
			m_DB.set_Peer(row, NULL);
			OnPeerInsane(peer);
		}
	}

	return false;
}

void NodeProcessor::RollbackTo(Height h)
{
	assert(h <= m_Cursor.m_Sid.m_Height);
	if (h == m_Cursor.m_Sid.m_Height)
		return;

	TxoID id0 = get_TxosBefore(h + 1);

	// undo inputs
	NodeDB::WalkerTxo wlk(m_DB);
	for (m_DB.EnumTxosBySpent(wlk, HeightRange(h + 1, m_Cursor.m_Sid.m_Height)); wlk.MoveNext(); )
	{
		if (wlk.m_ID >= id0)
			continue; // created and spent within this range - skip it

		uint8_t pNaked[s_TxoNakedMax];
		Blob val = wlk.m_Value;
		TxoToNaked(pNaked, val);

		Deserializer der;
		der.reset(val.p, val.n);

		Output outp;
		der & outp;

		Input inp;
		inp.m_Commitment = outp.m_Commitment;
		inp.m_ID = wlk.m_ID;

		NodeDB::StateID sidPrev;
		m_DB.FindStateByTxoID(sidPrev, wlk.m_ID); // relatively heavy operation: search for the original txo height
		assert(sidPrev.m_Height <= h);

		inp.m_Maturity = outp.get_MinMaturity(sidPrev.m_Height);

		if (!HandleBlockElement(inp, 0, nullptr, false))
			OnCorrupted();
	}

	// undo outputs
	struct MyWalker
		:public ITxoWalker_UnspentNaked
	{
		NodeProcessor* m_pThis;

		virtual bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp) override
		{
			if (!m_pThis->HandleBlockElement(outp, hCreate, nullptr, false))
				OnCorrupted();
			return true;
		}
	};

	MyWalker wlk2;
	wlk2.m_pThis = this;
	EnumTxos(wlk2, HeightRange(h + 1, m_Cursor.m_Sid.m_Height));

	m_DB.TxoDelFrom(id0);
	m_DB.TxoDelSpentFrom(h + 1);
	m_DB.DeleteEventsFrom(h + 1);


	// Kernels and cursor
	ByteBuffer bbE;
	TxVectors::Eternal txve;

	for (; m_Cursor.m_Sid.m_Height > h; m_DB.MoveBack(m_Cursor.m_Sid))
	{
		txve.m_vKernels.clear();
		bbE.clear();
		m_DB.GetStateBlock(m_Cursor.m_Sid.m_Row, nullptr, &bbE, nullptr);

		Deserializer der;
		der.reset(bbE);
		der & Cast::Down<TxVectors::Eternal>(txve);

		for (size_t i = 0; i < txve.m_vKernels.size(); i++)
		{
			Merkle::Hash hv;
			txve.m_vKernels[i]->get_ID(hv);

			m_DB.DeleteKernel(hv, m_Cursor.m_Sid.m_Height);
		}
	}

	InitCursor();
	OnRolledBack();

	m_Extra.m_Txos = id0;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnStateInternal(const Block::SystemState::Full& s, Block::SystemState::ID& id)
{
	s.get_ID(id);

	if (!s.IsValid())
	{
		LOG_WARNING() << id << " header invalid!";
		return DataStatus::Invalid;
	}

	Timestamp ts = getTimestamp();
	if (s.m_TimeStamp > ts)
	{
		ts = s.m_TimeStamp - ts; // dt
		if (ts > Rules::get().DA.MaxAhead_s)
		{
			LOG_WARNING() << id << " Timestamp ahead by " << ts;
			return DataStatus::Invalid;
		}
	}

	if (s.m_Height < m_Extra.m_LoHorizon)
		return DataStatus::Unreachable;

	if (m_DB.StateFindSafe(id))
		return DataStatus::Rejected;

	return DataStatus::Accepted;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnState(const Block::SystemState::Full& s, const PeerID& peer)
{
	Block::SystemState::ID id;

	DataStatus::Enum ret = OnStateInternal(s, id);
	if (DataStatus::Accepted == ret)
	{
		uint64_t rowid = m_DB.InsertState(s);
		m_DB.set_Peer(rowid, &peer);

		LOG_INFO() << id << " Header accepted";
	}
	
	return ret;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnBlock(const Block::SystemState::ID& id, const Blob& bbP, const Blob& bbE, const PeerID& peer)
{
	size_t nSize = size_t(bbP.n) + size_t(bbE.n);
	if (nSize > Rules::get().MaxBodySize)
	{
		LOG_WARNING() << id << " Block too large: " << nSize;
		return DataStatus::Invalid;
	}

	uint64_t rowid = m_DB.StateFindSafe(id);
	if (!rowid)
	{
		LOG_WARNING() << id << " Block unexpected";
		return DataStatus::Rejected;
	}

	if (NodeDB::StateFlags::Functional & m_DB.GetStateFlags(rowid))
	{
		LOG_WARNING() << id << " Block already received";
		return DataStatus::Rejected;
	}

	if (id.m_Height < m_Extra.m_LoHorizon)
		return DataStatus::Unreachable;

	LOG_INFO() << id << " Block received";

	m_DB.SetStateBlock(rowid, bbP, bbE);
	m_DB.SetStateFunctional(rowid);
	m_DB.set_Peer(rowid, &peer);

	if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(rowid))
		TryGoUp();

	return DataStatus::Accepted;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnTreasury(const Blob& blob)
{
	if (Rules::get().TreasuryChecksum == Zero)
		return DataStatus::Invalid; // should be no treasury

	ECC::Hash::Value hv;
	ECC::Hash::Processor()
		<< blob
		>> hv;

	if (Rules::get().TreasuryChecksum != hv)
		return DataStatus::Invalid;

	if (m_Extra.m_TreasuryHandled)
		return DataStatus::Rejected;

	if (!HandleTreasury(blob))
		return DataStatus::Invalid;

	assert(m_Extra.m_TreasuryHandled);
	m_Extra.m_Txos++;
	m_DB.ParamSet(NodeDB::ParamID::Treasury, &m_Extra.m_Txos, &blob);

	LOG_INFO() << "Treasury verified";

	RescanOwnedTxos();

	OnNewState();
	TryGoUp();

	return DataStatus::Accepted;
}

bool NodeProcessor::IsRemoteTipNeeded(const Block::SystemState::Full& sTipRemote, const Block::SystemState::Full& sTipMy)
{
	int n = sTipMy.m_ChainWork.cmp(sTipRemote.m_ChainWork);
	if (n > 0)
		return false;
	if (n < 0)
		return true;

	return sTipMy != sTipRemote;
}

uint64_t NodeProcessor::FindActiveAtStrict(Height h)
{
	NodeDB::WalkerState ws(m_DB);
	m_DB.EnumStatesAt(ws, h);
	while (true)
	{
		if (!ws.MoveNext())
			OnCorrupted();

		if (NodeDB::StateFlags::Active & m_DB.GetStateFlags(ws.m_Sid.m_Row))
			return ws.m_Sid.m_Row;
	}
}

/////////////////////////////
// Block generation
Difficulty NodeProcessor::get_NextDifficulty()
{
	const Rules& r = Rules::get(); // alias

	if (!m_Cursor.m_Sid.m_Row)
		return r.DA.Difficulty0; // 1st block

	//if (m_Cursor.m_Full.m_Height - Rules::HeightGenesis < r.DA.WindowWork)
	//	return r.DA.Difficulty0; // 1st block difficulty 0

	THW thw0, thw1;

	get_MovingMedianEx(m_Cursor.m_Sid.m_Row, r.DA.WindowMedian1, thw1);

	if (m_Cursor.m_Full.m_Height - Rules::HeightGenesis >= r.DA.WindowWork)
	{
		uint64_t row0 = FindActiveAtStrict(m_Cursor.m_Full.m_Height - r.DA.WindowWork);
		get_MovingMedianEx(row0, r.DA.WindowMedian1, thw0);
	}
	else
	{
		uint64_t row0 = FindActiveAtStrict(Rules::HeightGenesis);
		get_MovingMedianEx(row0, r.DA.WindowMedian1, thw0); // awkward to look for median, since they're immaginary. But makes sure we stick to the same median search and rounding (in case window is even).

		// how many immaginary prehistoric blocks should be offset
		uint32_t nDelta = r.DA.WindowWork - static_cast<uint32_t>(m_Cursor.m_Full.m_Height - Rules::HeightGenesis);

		thw0.first -= r.DA.Target_s * nDelta;
		thw0.second.first -= nDelta;

		Difficulty::Raw wrk, wrk2;
		r.DA.Difficulty0.Unpack(wrk);
		wrk2.AssignMul(wrk, uintBigFrom(nDelta));
		wrk2.Negate();
		thw0.second.second += wrk2;
	}

	assert(r.DA.WindowWork > r.DA.WindowMedian1); // when getting median - the target height can be shifted by some value, ensure it's smaller than the window
	// means, the height diff should always be positive
	assert(thw1.second.first > thw0.second.first);

	uint32_t dh = static_cast<uint32_t>(thw1.second.first - thw0.second.first);

	uint32_t dtTrg_s = r.DA.Target_s * dh;
	uint32_t dtSrc_s =
		(thw1.first >= thw0.first + dtTrg_s * 2) ? (dtTrg_s * 2) :
		(thw1.first <= thw0.first + dtTrg_s / 2) ? (dtTrg_s / 2) :
		static_cast<uint32_t>(thw1.first - thw0.first);

	Difficulty::Raw& dWrk = thw0.second.second;
	dWrk.Negate();
	dWrk += thw1.second.second;

	Difficulty res;
	res.Calculate(dWrk, dh, dtTrg_s, dtSrc_s);

	return res;
}

void NodeProcessor::get_MovingMedianEx(uint64_t rowLast, uint32_t nWindow, THW& res)
{
	std::vector<THW> v;
	v.reserve(nWindow);

	assert(rowLast);
	while (v.size() < nWindow)
	{
		v.emplace_back();
		THW& thw = v.back();

		if (rowLast)
		{
			Block::SystemState::Full s;
			m_DB.get_State(rowLast, s);

			thw.first = s.m_TimeStamp;
			thw.second.first = s.m_Height;
			thw.second.second = s.m_ChainWork;

			if (!m_DB.get_Prev(rowLast))
				rowLast = 0;
		}
		else
		{
			// append "prehistoric" blocks of starting difficulty and perfect timing
			const THW& thwSrc = v[v.size() - 2];

			thw.first = thwSrc.first - Rules::get().DA.Target_s;
			thw.second.first = thwSrc.second.first - 1;
			thw.second.second = thwSrc.second.second - Rules::get().DA.Difficulty0; // don't care about overflow
		}
	}

	std::sort(v.begin(), v.end()); // there's a better algorithm to find a median (or whatever order), however our array isn't too big, so it's ok.
	// In case there are multiple blocks with exactly the same Timestamp - the ambiguity is resolved w.r.t. Height.

	res = v[nWindow >> 1];
}

Timestamp NodeProcessor::get_MovingMedian()
{
	if (!m_Cursor.m_Sid.m_Row)
		return 0;

	THW thw;
	get_MovingMedianEx(m_Cursor.m_Sid.m_Row, Rules::get().DA.WindowMedian0, thw);
	return thw.first;
}

bool NodeProcessor::ValidateTxWrtHeight(const Transaction& tx) const
{
	Height h = m_Cursor.m_Sid.m_Height + 1;

	for (size_t i = 0; i < tx.m_vKernels.size(); i++)
		if (!tx.m_vKernels[i]->m_Height.IsInRange(h))
			return false;

	return true;
}

bool NodeProcessor::ValidateTxContext(const Transaction& tx)
{
	if (!ValidateTxWrtHeight(tx))
		return false;

	Height h = m_Cursor.m_Sid.m_Height;

	// Cheap tx verification. No need to update the internal structure, recalculate definition, or etc.
	// Ensure input UTXOs are present
	for (size_t i = 0; i < tx.m_vInputs.size(); i++)
	{
		struct Traveler :public UtxoTree::ITraveler
		{
			uint32_t m_Count;
			virtual bool OnLeaf(const RadixTree::Leaf& x) override
			{
				const UtxoTree::MyLeaf& n = Cast::Up<UtxoTree::MyLeaf>(x);
				Input::Count nCount = n.get_Count();
				assert(m_Count && nCount);
				if (m_Count <= nCount)
					return false; // stop iteration

				m_Count -= nCount;
				return true;
			}
		} t;
		t.m_Count = 1;
		const Input& v = *tx.m_vInputs[i];

		for (; i + 1 < tx.m_vInputs.size(); i++, t.m_Count++)
			if (tx.m_vInputs[i + 1]->m_Commitment != v.m_Commitment)
				break;

		UtxoTree::Key kMin, kMax;

		UtxoTree::Key::Data d;
		d.m_Commitment = v.m_Commitment;
		d.m_Maturity = 0;
		kMin = d;
		d.m_Maturity = h;
		kMax = d;

		UtxoTree::Cursor cu;
		t.m_pCu = &cu;
		t.m_pBound[0] = kMin.m_pArr;
		t.m_pBound[1] = kMax.m_pArr;

		if (m_Utxos.Traverse(t))
			return false; // some input UTXOs are missing
	}

	return true;
}

void NodeProcessor::DeleteOutdated(TxPool::Fluff& txp)
{
	for (TxPool::Fluff::ProfitSet::iterator it = txp.m_setProfit.begin(); txp.m_setProfit.end() != it; )
	{
		TxPool::Fluff::Element& x = (it++)->get_ParentObj();
		Transaction& tx = *x.m_pValue;

		if (!ValidateTxContext(tx))
			txp.Delete(x);
	}
}

size_t NodeProcessor::GenerateNewBlockInternal(BlockContext& bc)
{
	Height h = m_Cursor.m_Sid.m_Height + 1;

	// Generate the block up to the allowed size.
	// All block elements are serialized independently, their binary size can just be added to the size of the "empty" block.

	SerializerSizeCounter ssc;
	ssc & bc.m_Block;

	Block::Builder bb(bc.m_SubIdx, bc.m_Coin, bc.m_Tag, h);

	Output::Ptr pOutp;
	TxKernel::Ptr pKrn;

	bb.AddCoinbaseAndKrn(pOutp, pKrn);
	if (pOutp)
		ssc & *pOutp;
	ssc & *pKrn;

	ECC::Scalar::Native offset = bc.m_Block.m_Offset;

	if (BlockContext::Mode::Assemble != bc.m_Mode)
	{
		if (pOutp)
		{
			if (!HandleBlockElement(*pOutp, h, NULL, true))
				return 0;

			bc.m_Block.m_vOutputs.push_back(std::move(pOutp));
		}
		bc.m_Block.m_vKernels.push_back(std::move(pKrn));
	}

	// estimate the size of the fees UTXO
	if (!m_nSizeUtxoComission)
	{
		Output outp;
		outp.m_pConfidential.reset(new ECC::RangeProof::Confidential);
		ZeroObject(*outp.m_pConfidential);

		SerializerSizeCounter ssc2;
		ssc2 & outp;
		m_nSizeUtxoComission = ssc2.m_Counter.m_Value;
	}

	if (bc.m_Fees)
		ssc.m_Counter.m_Value += m_nSizeUtxoComission;

	const size_t nSizeMax = Rules::get().MaxBodySize;
	if (ssc.m_Counter.m_Value > nSizeMax)
	{
		// the block may be non-empty (i.e. contain treasury)
		LOG_WARNING() << "Block too large.";
		return 0; //
	}

	size_t nTxNum = 0;

	for (TxPool::Fluff::ProfitSet::iterator it = bc.m_TxPool.m_setProfit.begin(); bc.m_TxPool.m_setProfit.end() != it; )
	{
		TxPool::Fluff::Element& x = (it++)->get_ParentObj();

		if (AmountBig::get_Hi(x.m_Profit.m_Fee))
		{
			// huge fees are unsupported
			bc.m_TxPool.Delete(x);
			continue;
		}

		Amount feesNext = bc.m_Fees + AmountBig::get_Lo(x.m_Profit.m_Fee);
		if (feesNext < bc.m_Fees)
			continue; // huge fees are unsupported

		size_t nSizeNext = ssc.m_Counter.m_Value + x.m_Profit.m_nSize;
		if (!bc.m_Fees && feesNext)
			nSizeNext += m_nSizeUtxoComission;

		if (nSizeNext > nSizeMax)
		{
			if (bc.m_Block.m_vInputs.empty() &&
				(bc.m_Block.m_vOutputs.size() == 1) &&
				(bc.m_Block.m_vKernels.size() == 1))
			{
				// won't fit in empty block
				LOG_INFO() << "Tx is too big.";
				bc.m_TxPool.Delete(x);
			}
			continue;
		}

		Transaction& tx = *x.m_pValue;

		if (ValidateTxWrtHeight(tx) && HandleValidatedTx(tx.get_Reader(), h, true))
		{
			TxVectors::Writer(bc.m_Block, bc.m_Block).Dump(tx.get_Reader());

			bc.m_Fees = feesNext;
			ssc.m_Counter.m_Value = nSizeNext;
			offset += ECC::Scalar::Native(tx.m_Offset);
			++nTxNum;
		}
		else
			bc.m_TxPool.Delete(x); // isn't available in this context
	}

	LOG_INFO() << "GenerateNewBlock: size of block = " << ssc.m_Counter.m_Value << "; amount of tx = " << nTxNum;

	if (BlockContext::Mode::Assemble != bc.m_Mode)
	{
		if (bc.m_Fees)
		{
			bb.AddFees(bc.m_Fees, pOutp);
			if (!HandleBlockElement(*pOutp, h, NULL, true))
				return 0;

			bc.m_Block.m_vOutputs.push_back(std::move(pOutp));
		}

		bb.m_Offset = -bb.m_Offset;
		offset += bb.m_Offset;
	}

	bc.m_Block.m_Offset = offset;

	return ssc.m_Counter.m_Value;
}

void NodeProcessor::GenerateNewHdr(BlockContext& bc)
{
	bc.m_Hdr.m_Prev = m_Cursor.m_ID.m_Hash;

	get_Definition(bc.m_Hdr.m_Definition, true);

#ifndef NDEBUG
	// kernels must be sorted already
	for (size_t i = 1; i < bc.m_Block.m_vKernels.size(); i++)
	{
		const TxKernel& krn0 = *bc.m_Block.m_vKernels[i - 1];
		const TxKernel& krn1 = *bc.m_Block.m_vKernels[i];
		assert(krn0 <= krn1);
	}
#endif // NDEBUG

	struct MyFlyMmr :public Merkle::FlyMmr {
		const TxKernel::Ptr* m_ppKrn;
		virtual void LoadElement(Merkle::Hash& hv, uint64_t n) const override {
			m_ppKrn[n]->get_ID(hv);
		}
	};

	MyFlyMmr fmmr;
	fmmr.m_Count = bc.m_Block.m_vKernels.size();
	fmmr.m_ppKrn = bc.m_Block.m_vKernels.empty() ? NULL : &bc.m_Block.m_vKernels.front();
	fmmr.get_Hash(bc.m_Hdr.m_Kernels);

	bc.m_Hdr.m_PoW.m_Difficulty = m_Cursor.m_DifficultyNext;
	bc.m_Hdr.m_TimeStamp = getTimestamp();

	bc.m_Hdr.m_ChainWork = m_Cursor.m_Full.m_ChainWork + bc.m_Hdr.m_PoW.m_Difficulty;

	// Adjust the timestamp to be no less than the moving median (otherwise the block'll be invalid)
	Timestamp tm = get_MovingMedian() + 1;
	bc.m_Hdr.m_TimeStamp = std::max(bc.m_Hdr.m_TimeStamp, tm);
}

NodeProcessor::BlockContext::BlockContext(TxPool::Fluff& txp, Key::Index nSubKey, Key::IKdf& coin, Key::IPKdf& tag)
	:m_TxPool(txp)
	,m_SubIdx(nSubKey)
	,m_Coin(coin)
	,m_Tag(tag)
{
	m_Fees = 0;
	m_Block.ZeroInit();
}

bool NodeProcessor::GenerateNewBlock(BlockContext& bc)
{
	Height h = m_Cursor.m_Sid.m_Height + 1;

	bool bEmpty =
		bc.m_Block.m_vInputs.empty() &&
		bc.m_Block.m_vOutputs.empty() &&
		bc.m_Block.m_vKernels.empty();

	if (!bEmpty)
	{
		if ((BlockContext::Mode::Finalize != bc.m_Mode) && !VerifyBlock(bc.m_Block, bc.m_Block.get_Reader(), h))
			return false;

		if (!HandleValidatedTx(bc.m_Block.get_Reader(), h, true))
			return false;
	}

	size_t nSizeEstimated = 1;

	if (BlockContext::Mode::Finalize != bc.m_Mode)
		nSizeEstimated = GenerateNewBlockInternal(bc);

	if (nSizeEstimated)
		bc.m_Hdr.m_Height = h;

	verify(HandleValidatedTx(bc.m_Block.get_Reader(), h, false)); // undo changes

	// reset input maturities
	for (size_t i = 0; i < bc.m_Block.m_vInputs.size(); i++)
		bc.m_Block.m_vInputs[i]->m_Maturity = 0;

	if (!nSizeEstimated)
		return false;

	if (BlockContext::Mode::Assemble == bc.m_Mode)
		return true;

	size_t nCutThrough = bc.m_Block.Normalize(); // right before serialization
	nCutThrough; // remove "unused var" warning

	// The effect of the cut-through block may be different than it was during block construction, because the consumed and created UTXOs (removed by cut-through) could have different maturities.
	// Hence - we need to re-apply the block after the cut-throught, evaluate the definition, and undo the changes (once again).
	if (!HandleValidatedTx(bc.m_Block.get_Reader(), h, true))
	{
		LOG_WARNING() << "couldn't apply block after cut-through!";
		return false; // ?!
	}
	GenerateNewHdr(bc);
	verify(HandleValidatedTx(bc.m_Block.get_Reader(), h, false)); // undo changes


	Serializer ser;

	ser.reset();
	ser & Cast::Down<Block::BodyBase>(bc.m_Block);
	ser & Cast::Down<TxVectors::Perishable>(bc.m_Block);
	ser.swap_buf(bc.m_BodyP);

	ser.reset();
	ser & Cast::Down<TxVectors::Eternal>(bc.m_Block);
	ser.swap_buf(bc.m_BodyE);

	size_t nSize = bc.m_BodyP.size() + bc.m_BodyE.size();

	if (BlockContext::Mode::SinglePass == bc.m_Mode)
		assert(nCutThrough ?
			(nSize < nSizeEstimated) :
			(nSize == nSizeEstimated));

	return nSize <= Rules::get().MaxBodySize;
}

bool NodeProcessor::ValidateAndSummarize(TxBase::Context& ctx, const TxBase& txb, TxBase::IReader&& r, bool bBatchReset, bool bBatchFinalize)
{
	return ctx.ValidateAndSummarize(txb, std::move(r));
}

bool NodeProcessor::VerifyBlock(const Block::BodyBase& block, TxBase::IReader&& r, const HeightRange& hr)
{
	if ((hr.m_Min < Rules::HeightGenesis) || hr.IsEmpty())
		return false;

	TxBase::Context ctx;
	ctx.m_Height = hr;
	ctx.m_bBlockMode = true;

	return
		ValidateAndSummarize(ctx, block, std::move(r), true, true) &&
		ctx.IsValidBlock();
}

void NodeProcessor::ExtractBlockWithExtra(Block::Body& block, const NodeDB::StateID& sid)
{
	ByteBuffer bbE;
	m_DB.GetStateBlock(sid.m_Row, nullptr, &bbE, nullptr);

	Deserializer der;
	der.reset(bbE);
	der & Cast::Down<TxVectors::Eternal>(block);

	for (size_t i = 0; i < block.m_vKernels.size(); i++)
		block.m_vKernels[i]->m_Maturity = sid.m_Height;

	TxoID id0;
	TxoID id1 = m_DB.get_StateTxos(sid.m_Row);

	if (!m_DB.get_StateExtra(sid.m_Row, block.m_Offset))
		OnCorrupted();

	uint64_t rowid = sid.m_Row;
	if (m_DB.get_Prev(rowid))
	{
		AdjustOffset(block.m_Offset, rowid, false);
		id0 = m_DB.get_StateTxos(rowid);
	}
	else
		id0 = get_TxosBefore(Rules::HeightGenesis);

	// inputs
	NodeDB::WalkerTxo wlk(m_DB);
	for (m_DB.EnumTxosBySpent(wlk, sid.m_Height); wlk.MoveNext(); )
	{
		assert(wlk.m_SpendHeight == sid.m_Height);

		uint8_t pNaked[s_TxoNakedMax];
		TxoToNaked(pNaked, wlk.m_Value);

		der.reset(wlk.m_Value.p, wlk.m_Value.n);

		Output outp;
		der & outp;

		NodeDB::StateID sidPrev;
		m_DB.FindStateByTxoID(sidPrev, wlk.m_ID); // relatively heavy operation: search for the original txo height


		block.m_vInputs.emplace_back();
		Input::Ptr& pInp = block.m_vInputs.back();
		pInp.reset(new Input);

		pInp->m_Commitment = outp.m_Commitment;
		pInp->m_Maturity = outp.get_MinMaturity(sidPrev.m_Height);
	}

	// outputs
	for (m_DB.EnumTxos(wlk, id0); wlk.MoveNext(); )
	{
		if (wlk.m_ID >= id1)
			break;

		block.m_vOutputs.emplace_back();
		Output::Ptr& pOutp = block.m_vOutputs.back();
		pOutp.reset(new Output);

		der.reset(wlk.m_Value.p, wlk.m_Value.n);
		der & *pOutp;

		pOutp->m_Maturity = pOutp->get_MinMaturity(sid.m_Height);
	}

	block.NormalizeP();
}

void NodeProcessor::SquashOnce(std::vector<Block::Body>& v)
{
	assert(v.size() >= 2);

	Block::Body& trg = v[v.size() - 2];
	const Block::Body& src0 = v.back();
	Block::Body src1 = std::move(trg);

	trg.Merge(src0);

	bool bStop = false;
	TxVectors::Writer(trg, trg).Combine(src0.get_Reader(), src1.get_Reader(), bStop);

	v.pop_back();
}

void NodeProcessor::ExportMacroBlock(Block::BodyBase::IMacroWriter& w, const HeightRange& hr)
{
	assert(hr.m_Min <= hr.m_Max);
	NodeDB::StateID sid;
	sid.m_Row = FindActiveAtStrict(hr.m_Max);
	sid.m_Height = hr.m_Max;

	std::vector<Block::Body> vBlocks;

	for (uint32_t i = 0; ; i++)
	{
		vBlocks.resize(vBlocks.size() + 1);
		ExtractBlockWithExtra(vBlocks.back(), sid);

		if (hr.m_Min == sid.m_Height)
			break;

		if (!m_DB.get_Prev(sid))
			OnCorrupted();

		for (uint32_t j = i; 1 & j; j >>= 1)
			SquashOnce(vBlocks);
	}

	while (vBlocks.size() > 1)
		SquashOnce(vBlocks);

	std::vector<Block::SystemState::Sequence::Element> vElem;
	Block::SystemState::Sequence::Prefix prefix;
	ExportHdrRange(hr, prefix, vElem);

	w.put_Start(vBlocks[0], prefix);

	for (size_t i = 0; i < vElem.size(); i++)
		w.put_NextHdr(vElem[i]);

	w.Dump(vBlocks[0].get_Reader());
}

void NodeProcessor::ExportHdrRange(const HeightRange& hr, Block::SystemState::Sequence::Prefix& prefix, std::vector<Block::SystemState::Sequence::Element>& v)
{
	if (hr.m_Min > hr.m_Max) // can happen for empty range
		ZeroObject(prefix);
	else
	{
		v.resize(hr.m_Max - hr.m_Min + 1);

		NodeDB::StateID sid;
		sid.m_Row = FindActiveAtStrict(hr.m_Max);
		sid.m_Height = hr.m_Max;

		while (true)
		{
			Block::SystemState::Full s;
			m_DB.get_State(sid.m_Row, s);

			v[sid.m_Height - hr.m_Min] = s;

			if (sid.m_Height == hr.m_Min)
			{
				prefix = s;
				break;
			}

			if (!m_DB.get_Prev(sid))
				OnCorrupted();
		}
	}
}

bool NodeProcessor::ImportMacroBlock(Block::BodyBase::IMacroReader& r)
{
	if (!ImportMacroBlockInternal(r))
		return false;

	TryGoUp();
	return true;
}

bool NodeProcessor::ImportMacroBlockInternal(Block::BodyBase::IMacroReader& r)
{
	Block::BodyBase body;
	Block::SystemState::Full s;
	Block::SystemState::ID id;

	r.Reset();
	r.get_Start(body, s);

	id.m_Height = s.m_Height - 1;
	id.m_Hash = s.m_Prev;

	if ((m_Cursor.m_ID.m_Height + 1 != s.m_Height) || (m_Cursor.m_ID.m_Hash != s.m_Prev))
	{
		LOG_WARNING() << "Incompatible state for import. My Tip: " << m_Cursor.m_ID << ", Macroblock starts at " << id;
		return false; // incompatible beginning state
	}

	if (r.m_pKernel && r.m_pKernel->m_Maturity < s.m_Height)
	{
		LOG_WARNING() << "Kernel maturity OOB";
		return false; // incompatible beginning state
	}

	Merkle::CompactMmr cmmr, cmmrKrn;
	if (m_Cursor.m_ID.m_Height > Rules::HeightGenesis)
	{
		Merkle::ProofBuilderHard bld;
		m_DB.get_Proof(bld, m_Cursor.m_Sid, m_Cursor.m_Sid.m_Height - 1);

		cmmr.m_vNodes.swap(bld.m_Proof);
		std::reverse(cmmr.m_vNodes.begin(), cmmr.m_vNodes.end());
		cmmr.m_Count = m_Cursor.m_Sid.m_Height - 1 - Rules::HeightGenesis;

		cmmr.Append(m_Cursor.m_Full.m_Prev);
	}

	LOG_INFO() << "Verifying headers...";

	for (bool bFirstTime = true ; r.get_NextHdr(s); s.NextPrefix())
	{
		// Difficulty check?!

		if (bFirstTime)
		{
			bFirstTime = false;

			Difficulty::Raw wrk = m_Cursor.m_Full.m_ChainWork + s.m_PoW.m_Difficulty;

			if (wrk != s.m_ChainWork)
			{
				LOG_WARNING() << id << " Chainwork expected=" << wrk << ", actual=" << s.m_ChainWork;
				return false;
			}
		}
		else
			s.m_ChainWork += s.m_PoW.m_Difficulty;

		if (id.m_Height >= Rules::HeightGenesis)
			cmmr.Append(id.m_Hash);

		switch (OnStateInternal(s, id))
		{
		case DataStatus::Invalid:
		{
			LOG_WARNING() << "Invald header encountered: " << id;
			return false;
		}

		case DataStatus::Accepted:
			m_DB.InsertState(s);

		default: // suppress the warning of not handling all the enum values
			break;
		}

		// verify kernel commitment
		cmmrKrn.m_Count = 0;
		cmmrKrn.m_vNodes.clear();

		// don't care if kernels are out-of-order, this will be handled during the context-free validation.
		for (; r.m_pKernel && (r.m_pKernel->m_Maturity == s.m_Height); r.NextKernel())
		{
			Merkle::Hash hv;
			r.m_pKernel->get_ID(hv);
			cmmrKrn.Append(hv);
		}

		Merkle::Hash hv;
		cmmrKrn.get_Hash(hv);

		if (s.m_Kernels != hv)
		{
			LOG_WARNING() << id << " Kernel commitment mismatch";
			return false;
		}
	}

	if (r.m_pKernel)
	{
		LOG_WARNING() << "Kernel maturity OOB";
		return false;
	}

	LOG_INFO() << "Context-free validation...";

	if (!VerifyBlock(body, std::move(r), HeightRange(m_Cursor.m_ID.m_Height + 1, id.m_Height)))
	{
		LOG_WARNING() << "Context-free verification failed";
		return false;
	}

	LOG_INFO() << "Applying macroblock...";

	if (!HandleValidatedBlock(std::move(r), body, m_Cursor.m_ID.m_Height + 1, true, &id.m_Height))
	{
		LOG_WARNING() << "Invalid in its context";
		return false;
	}

	// evaluate the Definition
	Merkle::Hash hvDef, hv;
	cmmr.get_Hash(hv);
	get_Definition(hvDef, hv);

	if (s.m_Definition != hvDef)
	{
		LOG_WARNING() << "Definition mismatch";

		verify(HandleValidatedBlock(std::move(r), body, m_Cursor.m_ID.m_Height + 1, false, &id.m_Height));

		return false;
	}

	// Update DB state flags and cursor. This will also buils the MMR for prev states
	LOG_INFO() << "Building auxilliary datas...";

	TxVectors::Full txv;
	TxVectors::Writer txwr(txv, txv);
	ByteBuffer bbE;

	r.Reset();
	r.get_Start(body, s);
	for (bool bFirstTime = true; r.get_NextHdr(s); s.NextPrefix())
	{
		if (bFirstTime)
			bFirstTime = false;
		else
			s.m_ChainWork += s.m_PoW.m_Difficulty;

		s.get_ID(id);

		NodeDB::StateID sid;
		sid.m_Row = m_DB.StateFindSafe(id);
		if (!sid.m_Row)
			OnCorrupted();

		m_DB.SetStateFunctional(sid.m_Row);

		m_DB.DelStateBlockPRB(sid.m_Row); // if somehow it was downloaded

		txv.m_vKernels.clear();
		bbE.clear();

		for (; r.m_pKernel && (r.m_pKernel->m_Maturity == s.m_Height); r.NextKernel())
		{
			txwr.Write(*r.m_pKernel);

			r.m_pKernel->get_ID(hv);
			m_DB.InsertKernel(hv, r.m_pKernel->m_Maturity);
		}

		Serializer ser;
		ser.swap_buf(bbE);
		ser & Cast::Down<TxVectors::Eternal>(txv);
		ser.swap_buf(bbE);

		Blob bEmpty(nullptr, 0);
		m_DB.SetStateBlock(sid.m_Row, bEmpty, bbE);

		sid.m_Height = id.m_Height;
		m_DB.MoveFwd(sid);
	}

	m_Extra.m_LoHorizon = m_Extra.m_Fossil = m_Extra.m_TxoHi = m_Extra.m_TxoLo = id.m_Height;

	m_DB.ParamSet(NodeDB::ParamID::LoHorizon, &id.m_Height, NULL);
	m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &id.m_Height, NULL);
	m_DB.ParamSet(NodeDB::ParamID::HeightTxoLo, &id.m_Height, NULL);
	m_DB.ParamSet(NodeDB::ParamID::HeightTxoHi, &id.m_Height, NULL);

	InitCursor();
	RescanOwnedTxos();

	LOG_INFO() << "Macroblock import succeeded";

	return true;
}

TxoID NodeProcessor::get_TxosBefore(Height h)
{
	if (h < Rules::HeightGenesis)
		return 0;

	TxoID id;
	if (Rules::HeightGenesis == h)
	{
		id = 0;
		m_DB.ParamGet(NodeDB::ParamID::Treasury, &id, nullptr, nullptr);
	}
	else
	{
		id = m_DB.get_StateTxos(FindActiveAtStrict(h - 1));
		if (MaxHeight == id)
			OnCorrupted();
	}

	return id;
}

bool NodeProcessor::EnumTxos(ITxoWalker& wlk)
{
	return EnumTxos(wlk, HeightRange(Rules::HeightGenesis - 1, m_Cursor.m_ID.m_Height));
}

bool NodeProcessor::EnumTxos(ITxoWalker& wlkTxo, const HeightRange& hr)
{
	if (hr.IsEmpty())
		return true;
	assert(hr.m_Max <= m_Cursor.m_ID.m_Height);

	TxoID id1 = get_TxosBefore(hr.m_Min);
	Height h = hr.m_Min - 1; // don't care about overflow

	NodeDB::WalkerTxo wlk(m_DB);
	for (m_DB.EnumTxos(wlk, id1);  wlk.MoveNext(); )
	{
		if (wlk.m_ID >= id1)
		{
			if (++h > hr.m_Max)
				break;

			if (h < Rules::HeightGenesis)
				id1 = get_TxosBefore(Rules::HeightGenesis); // treasury?

			if (wlk.m_ID >= id1)
			{
				NodeDB::StateID sid;
				id1 = m_DB.FindStateByTxoID(sid, wlk.m_ID);

				assert(wlk.m_ID < id1);
				h = sid.m_Height;
			}
		}

		if (!wlkTxo.OnTxo(wlk, h))
			return false;
	}

	return true;
}

bool NodeProcessor::ITxoWalker::OnTxo(const NodeDB::WalkerTxo& wlk , Height hCreate)
{
	Deserializer der;
	der.reset(wlk.m_Value.p, wlk.m_Value.n);

	Output outp;
	der & outp;

	return OnTxo(wlk, hCreate, outp);
}

bool NodeProcessor::ITxoWalker::OnTxo(const NodeDB::WalkerTxo&, Height hCreate, Output&)
{
	assert(false);
	return false;
}

bool NodeProcessor::ITxoRecover::OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp)
{
	Key::IDV kidv;
	if (!m_This.Recover(kidv, outp))
		return true;

	return OnTxo(wlk, hCreate, outp, kidv);
}

bool NodeProcessor::Recover(Key::IDV& kidv, const Output& outp)
{
	struct Walker :public IKeyWalker
	{
		Key::IDV& m_Kidv;
		const Output& m_Outp;

		Walker(Key::IDV& kidv, const Output& outp)
			:m_Kidv(kidv)
			,m_Outp(outp)
		{
		}

		virtual bool OnKey(Key::IPKdf& tag, Key::Index) override
		{
			return !m_Outp.Recover(tag, m_Kidv);
		}

	} wlk(kidv, outp);

	return !EnumViewerKeys(wlk);
}

bool NodeProcessor::ITxoWalker_UnspentNaked::OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate)
{
	if (wlk.m_SpendHeight != MaxHeight)
		return true;

	uint8_t pNaked[s_TxoNakedMax];
	Blob val = wlk.m_Value;
	TxoToNaked(pNaked, val); // save allocation and deserialization of sig

	Deserializer der;
	der.reset(val.p, val.n);

	Output outp;
	der & outp;

	return Cast::Down<ITxoWalker>(*this).OnTxo(wlk, hCreate, outp);
}

void NodeProcessor::InitializeUtxos()
{
	assert(!m_Extra.m_Txos);

	struct Walker
		:public ITxoWalker_UnspentNaked
	{
		NodeProcessor& m_This;
		Walker(NodeProcessor& x) :m_This(x) {}

		virtual bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp) override
		{
			m_This.m_Extra.m_Txos = wlk.m_ID;
			if (!m_This.HandleBlockElement(outp, hCreate, nullptr, true))
				OnCorrupted();

			return true;
		}
	};

	Walker wlk(*this);
	EnumTxos(wlk);

	if (m_Cursor.m_ID.m_Height >= Rules::HeightGenesis)
	{
		// final check
		Merkle::Hash hv;
		get_Definition(hv, false);
		if (m_Cursor.m_Full.m_Definition != hv)
			OnCorrupted();
	}
}

bool NodeProcessor::GetBlock(const NodeDB::StateID& sid, ByteBuffer& bbEthernal, ByteBuffer& bbPerishable, Height h0, Height hLo1, Height hHi1)
{
	// h0 - current peer Height
	// hLo1 - HorizonLo that peer needs after the sync
	// hHi1 - HorizonL1 that peer needs after the sync
	if ((hLo1 > hHi1) || (h0 >= sid.m_Height))
		return false;

	// For every output:
	//	if SpendHeight > hHi1 (or null) then fully transfer
	//	if SpendHeight > hLo1 then transfer naked (remove Confidential, Public, AssetID)
	//	Otherwise - don't transfer

	// For every input (commitment only):
	//	if SpendHeight > hLo1 then transfer
	//	if CreateHeight <= h0 then transfer
	//	Otherwise - don't transfer

	hHi1 = std::max(hHi1, sid.m_Height); // valid block can't spend its own output. Hence this means full block should be transferred

	if (m_Extra.m_TxoHi > hHi1)
		return false;

	hLo1 = std::max(hLo1, sid.m_Height - 1);
	if (m_Extra.m_TxoLo > hLo1)
		return false;

	bool bFullBlock = (sid.m_Height >= hHi1);
	m_DB.GetStateBlock(sid.m_Row, bFullBlock ? &bbPerishable : nullptr, &bbEthernal, NULL);

	if (!bbPerishable.empty())
		return true;

	// re-create it from Txos
	if (!(m_DB.GetStateFlags(sid.m_Row) & NodeDB::StateFlags::Active))
		return false;

	TxBase txb;

	TxoID idInpCut = get_TxosBefore(h0 + 1);
	TxoID id0;

	TxoID id1 = m_DB.get_StateTxos(sid.m_Row);

	if (!m_DB.get_StateExtra(sid.m_Row, txb.m_Offset))
		OnCorrupted();

	uint64_t rowid = sid.m_Row;
	if (m_DB.get_Prev(rowid))
	{
		AdjustOffset(txb.m_Offset, rowid, false);
		id0 = m_DB.get_StateTxos(rowid);
	}
	else
		id0 = get_TxosBefore(Rules::HeightGenesis);

	uintBigFor<uint32_t>::Type nCount(Zero);

	// inputs
	std::vector<Input> vInputs;
	NodeDB::WalkerTxo wlk(m_DB);
	for (m_DB.EnumTxosBySpent(wlk, sid.m_Height); wlk.MoveNext(); )
	{
		assert(wlk.m_SpendHeight == sid.m_Height);

		//	if SpendHeight > hLo1 then transfer
		//	if CreateHeight <= h0 then transfer
		//	Otherwise - don't transfer
		if ((sid.m_Height > hLo1) || (wlk.m_ID < idInpCut))
		{
			vInputs.emplace_back();
			Input& inp = vInputs.back();
			ECC::Point& pt = inp.m_Commitment; // alias

			// extract input from output (which may be naked already)
			if (wlk.m_Value.n < sizeof(pt))
				OnCorrupted();

			const uint8_t* pSrc = reinterpret_cast<const uint8_t*>(wlk.m_Value.p);
			pt.m_Y = 1 & pSrc[0];
			memcpy(pt.m_X.m_pData, pSrc + 1, pt.m_X.nBytes);

			nCount.Inc();
		}
	}

	std::sort(vInputs.begin(), vInputs.end());

	Serializer ser;
	ser & txb;
	ser & nCount;

	for (size_t i = 0; i < vInputs.size(); i++)
		ser & vInputs[i];

	ByteBuffer bbBlob;
	nCount = Zero;

	// outputs
	for (m_DB.EnumTxos(wlk, id0); wlk.MoveNext(); )
	{
		if (wlk.m_ID >= id1)
			break;

		//	if SpendHeight > hHi1 (or null) then fully transfer
		//	if SpendHeight > hLo1 then transfer naked (remove Confidential, Public, AssetID)
		//	Otherwise - don't transfer

		if (wlk.m_SpendHeight <= hLo1)
			continue;

		uint8_t pNaked[s_TxoNakedMax];

		if (wlk.m_SpendHeight <= hHi1)
			TxoToNaked(pNaked, wlk.m_Value);

		nCount.Inc();

		const uint8_t* p = reinterpret_cast<const uint8_t*>(wlk.m_Value.p);
		bbBlob.insert(bbBlob.end(), p, p + wlk.m_Value.n);
	}

	ser & nCount;
	ser.swap_buf(bbPerishable);
	bbPerishable.insert(bbPerishable.end(), bbBlob.begin(), bbBlob.end());

	return true;
}

} // namespace beam
