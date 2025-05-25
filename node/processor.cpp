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
#include "../core/shielded.h"
#include "../bvm/bvm2.h"
#include "../bvm/evm.h"
#include "../core/serialization_adapters.h"
#include "../core/base58.h"
#include "../utility/serialize.h"
#include "../utility/logger.h"
#include "../utility/logger_checkpoints.h"
#include "../utility/blobmap.h"
#include <condition_variable>
#include <cctype>

namespace beam {

void NodeProcessor::OnCorrupted()
{
	CorruptionException exc;
	exc.m_sErr = "node data";
	throw exc;
}

NodeProcessor::Horizon::Horizon()
{
	SetInfinite();
}

void NodeProcessor::Horizon::SetInfinite()
{
	m_Branching = MaxHeight;
	m_Sync.Lo = MaxHeight;
	m_Sync.Hi = MaxHeight;
	m_Local.Lo = MaxHeight;
	m_Local.Hi = MaxHeight;
}

void NodeProcessor::Horizon::SetStdFastSync()
{
	uint32_t r = Rules::get().MaxRollback;
	m_Branching = r / 4; // inferior branches would be pruned when height difference is this.

	m_Sync.Hi = r;
	m_Sync.Lo = r * 3; // 3-day period

	m_Local.Hi = r * 2; // slightly higher than m_Sync.Loc, to feed other fast synchers
	m_Local.Lo = r * 180; // 180-day period
}

void NodeProcessor::Horizon::Normalize()
{
	std::setmax(m_Branching, Height(1));

	Height r = Rules::get().MaxRollback;

	std::setmax(m_Sync.Hi, std::max(r, m_Branching));
	std::setmax(m_Sync.Lo, m_Sync.Hi);

	// Some nodes in production have a bug: if (Sync.Lo == Sync.Hi) - the last generated block that they send may be incorrect
	// Workaround: make sure (Sync.Lo > Sync.Hi), at least by 1
	//
	// After HF2 the workaround can be removed
	if ((m_Sync.Lo == m_Sync.Hi) && (m_Sync.Hi < MaxHeight))
		m_Sync.Lo++;

	// though not required, we prefer m_Local to be no less than m_Sync
	std::setmax(m_Local.Hi, m_Sync.Hi);
	std::setmax(m_Local.Lo, std::max(m_Local.Hi, m_Sync.Lo));
}

void NodeProcessor::Initialize(const char* szPath)
{
	StartParams sp; // defaults
	Initialize(szPath, sp);
}

void NodeProcessor::Initialize(const char* szPath, const StartParams& sp, ILongAction* pExternalHandler)
{
	m_DB.Open(szPath);
	m_DbTx.Start(m_DB);
	m_pExternalHandler = pExternalHandler;
	if (sp.m_CheckIntegrity)
	{
		BEAM_LOG_INFO() << "DB integrity check...";
		m_DB.CheckIntegrity();
	}

	const auto& r = Rules::get();

	Merkle::Hash hv;
	Blob blob(hv);

	NodeDB::StateID sid;
	m_DB.get_Cursor(sid);
	InitCursor(false, sid);

	bool bUpdateChecksum = !m_DB.ParamGet(NodeDB::ParamID::CfgChecksum, NULL, &blob);
	if (!bUpdateChecksum)
	{
		const HeightHash* pFork = r.FindFork(hv);
		if (&r.get_LastFork() != pFork)
		{
			if (!pFork)
			{
				std::ostringstream os;
				os << "Data configuration is incompatible: " << hv;
				throw std::runtime_error(os.str());
			}

			if (m_Cursor.m_hh.m_Height >= pFork[1].m_Height)
			{
				std::ostringstream os;
				os << "Data configuration: " << hv << ", Fork didn't happen at " << pFork[1].m_Height;
				throw std::runtime_error(os.str());
			}

			bUpdateChecksum = true;
		}
	}

	if (bUpdateChecksum)
	{
		BEAM_LOG_INFO() << "Settings configuration";

		blob = Blob(r.get_LastFork().m_Hash);
		m_DB.ParamSet(NodeDB::ParamID::CfgChecksum, NULL, &blob);
	}

	ZeroObject(m_Extra);
	m_Extra.m_Fossil.v = m_DB.ParamIntGetDef(NodeDB::ParamID::NumberFossil, Rules::HeightGenesis - 1);
	m_Extra.m_TxoLo.v = m_DB.ParamIntGetDef(NodeDB::ParamID::NumberTxoLo, Rules::HeightGenesis - 1);
	m_Extra.m_TxoHi.v = m_DB.ParamIntGetDef(NodeDB::ParamID::NumberTxoHi, Rules::HeightGenesis - 1);

	ZeroObject(m_SyncData);

	blob.p = &m_SyncData;
	blob.n = sizeof(m_SyncData);
	m_DB.ParamGet(NodeDB::ParamID::SyncData, nullptr, &blob);

	LogSyncData();

	if (r.TreasuryChecksum == Zero)
		m_Extra.m_TxosTreasury = 1; // artificial gap
	else
		m_DB.ParamGet(NodeDB::ParamID::Treasury, &m_Extra.m_TxosTreasury, nullptr, nullptr);

	auto aidMax = m_DB.ParamIntGetDef(NodeDB::ParamID::AidMax);
	if (!aidMax && r.CA.ForeignEnd)
	{
		aidMax = r.CA.ForeignEnd;
		m_DB.ParamIntSet(NodeDB::ParamID::AidMax, aidMax);
	}

	m_Mmr.m_Assets.m_Count = aidMax - r.CA.ForeignEnd;
	m_Extra.m_ShieldedOutputs = m_DB.ShieldedOutpGet(std::numeric_limits<int64_t>::max());
	m_Mmr.m_Shielded.m_Count = m_DB.ParamIntGetDef(NodeDB::ParamID::ShieldedInputs);
	m_Mmr.m_Shielded.m_Count += m_Extra.m_ShieldedOutputs;

	InitializeMapped(szPath);
	m_Extra.m_Txos = get_TxosBefore(Block::Number(m_Cursor.m_Full.m_Number.v + 1));

	if (Rules::Consensus::Pbft == r.m_Consensus)
	{
		if (m_Cursor.m_Full.m_Number.v)
		{
			ByteBuffer buf;
			m_DB.ParamGet(NodeDB::ParamID::PbftState, nullptr, nullptr, &buf);

			Deserializer der;
			der.reset(buf);
			der & Cast::Down<Block::Pbft::State>(m_PbftState);

			m_PbftState.ResolveWhitelisted(r);
		}
		else
			m_PbftState.SetInitial();

		m_PbftState.m_Hash.m_Valid = false;
	}

	bool bRebuildNonStd = false;
	if ((StartParams::RichInfo::Off | StartParams::RichInfo::On) & sp.m_RichInfoFlags)
	{
		uint32_t bOn = !!(StartParams::RichInfo::On & sp.m_RichInfoFlags);
		if (m_DB.ParamIntGetDef(NodeDB::ParamID::RichContractInfo) != bOn)
		{
			m_DB.ParamIntSet(NodeDB::ParamID::RichContractInfo, bOn);
			if (bOn)
				bRebuildNonStd = true;
		}
	}

	if (StartParams::RichInfo::UpdShader & sp.m_RichInfoFlags)
	{
		m_DB.ParamSet(NodeDB::ParamID::RichContractParser, nullptr, &sp.m_RichParser);
		
		if (!bRebuildNonStd && m_DB.ParamIntGetDef(NodeDB::ParamID::RichContractInfo))
			bRebuildNonStd = true;
	}

	uint64_t nFlags1 = m_DB.ParamIntGetDef(NodeDB::ParamID::Flags1);
	if (bRebuildNonStd || (NodeDB::Flags1::PendingRebuildNonStd & nFlags1))
	{
		RebuildNonStd();
		m_DB.ParamIntSet(NodeDB::ParamID::Flags1, nFlags1 & ~NodeDB::Flags1::PendingRebuildNonStd);
	}

	TestDefinitionStrict();

	CommitDB();

	m_Horizon.Normalize();

	if (PruneOld() && !sp.m_Vacuum)
	{
		BEAM_LOG_INFO() << "Old data was just removed from the DB. Some space can be freed by vacuum";
	}

	if (sp.m_Vacuum)
		Vacuum();

	if (m_ManualSelection.Load())
		m_ManualSelection.Log();
	else
		m_ManualSelection.Reset();

	TryGoUp();
}

void NodeProcessor::ManualSelection::Reset()
{
	m_Sid.m_Number.v = MaxHeight; // don't set it to 0, it may interfer with treasury in RequestData()
	m_Sid.m_Hash = Zero;
	m_Forbidden = false;
}

void NodeProcessor::ManualSelection::ResetAndSave()
{
	Reset();
	Save();
	Log();
}

bool NodeProcessor::ManualSelection::Load()
{
	Blob blob(m_Sid.m_Hash);
	if (!get_ParentObj().m_DB.ParamGet(NodeDB::ParamID::ForbiddenState, &m_Sid.m_Number.v, &blob))
		return false;

	const uint64_t flag = (MaxHeight >> 1) + 1;
	m_Forbidden = !(flag & m_Sid.m_Number.v);

	if (!m_Forbidden)
		m_Sid.m_Number.v &= ~flag;

	return true;
}

void NodeProcessor::ManualSelection::Save() const
{
	auto n = m_Sid.m_Number.v;
	if (MaxHeight == n)
		get_ParentObj().m_DB.ParamDelSafe(NodeDB::ParamID::ForbiddenState);
	else
	{
		if (!m_Forbidden)
		{
			const uint64_t flag = (MaxHeight >> 1) + 1;
			n |= flag;
		}

		Blob blob(m_Sid.m_Hash);
		get_ParentObj().m_DB.ParamSet(NodeDB::ParamID::ForbiddenState, &n, &blob);
	}
}

void NodeProcessor::ManualSelection::Log() const
{
	if (MaxHeight == m_Sid.m_Number.v) {
		BEAM_LOG_INFO() << "Manual selection state reset";
	} else {
		BEAM_LOG_INFO() << (m_Forbidden ? "Forbidden" : "Selected") << " state: " << m_Sid;
	}
}

bool NodeProcessor::ManualSelection::IsAllowed(Block::Number num, const Merkle::Hash& hv) const
{
	return (m_Sid.m_Number.v == num.v) ? IsAllowed(hv) : true;
}

bool NodeProcessor::ManualSelection::IsAllowed(const Merkle::Hash& hv) const
{
	bool bMatch = (hv == m_Sid.m_Hash);
	if (bMatch != m_Forbidden)
		return true;

	Block::SystemState::ID sid;
	sid.m_Number = m_Sid.m_Number;
	sid.m_Hash = hv;
	BEAM_LOG_WARNING() << sid << " State forbidden";
	return false;
}

void NodeProcessor::InitializeMapped(const char* sz)
{
	if (InitMapping(sz, false))
	{
		BEAM_LOG_INFO() << "Mapping image found";
		if (TestDefinition())
			return; // ok

		BEAM_LOG_WARNING() << "Definition mismatch, discarding mapped image";
		m_Mapped.Close();
		InitMapping(sz, true);
	}

	InitializeUtxos();

	NodeDB::WalkerContractData wlk;
	for (m_DB.ContractDataEnum(wlk); wlk.MoveNext(); )
		m_Mapped.m_Contract.Toggle(wlk.m_Key, wlk.m_Val, true);
}

void NodeProcessor::TestDefinitionStrict()
{
	if (!TestDefinition())
	{
		BEAM_LOG_ERROR() << "Definition mismatch";
		OnCorrupted();
	}
}

bool NodeProcessor::TestDefinition()
{
	if (!m_Cursor.m_Full.m_Number.v || (m_Cursor.m_Full.m_Number.v < m_SyncData.m_TxoLo.v))
		return true; // irrelevant

	Merkle::Hash hv;
	Evaluator ev(*this);
	ev.get_Definition(hv);

	return m_Cursor.m_Full.m_Definition == hv;
}


// Ridiculous! Had to write this because strmpi isn't standard!
int My_strcmpi(const char* sz1, const char* sz2)
{
	while (true)
	{
		int c1 = std::tolower(*sz1++);
		int c2 = std::tolower(*sz2++);
		if (c1 < c2)
			return -1;
		if (c1 > c2)
			return 1;

		if (!c1)
			break;
	}
	return 0;
}

void NodeProcessor::get_MappingPath(std::string& sPath, const char* sz)
{
	// derive mapping path from db path
	sPath = sz;

	static const char szSufix[] = ".db";
	const size_t nSufix = _countof(szSufix) - 1;

	if ((sPath.size() >= nSufix) && !My_strcmpi(sPath.c_str() + sPath.size() - nSufix, szSufix))
		sPath.resize(sPath.size() - nSufix);

	sPath += "-utxo-image.bin";
}

bool NodeProcessor::InitMapping(const char* sz, bool bForceReset)
{
	// derive mapping path from db path
	std::string sPath;
	get_MappingPath(sPath, sz);

	Mapped::Stamp us;
	Blob blob(us);

	// don't use the saved image if no height: we may contain treasury UTXOs, but no way to verify the contents
	if (bForceReset || !m_Cursor.m_Full.m_Number.v || !m_DB.ParamGet(NodeDB::ParamID::MappingStamp, nullptr, &blob))
	{
		us = 1U;
		us.Negate();
	}

	return m_Mapped.Open(sPath.c_str(), us);
}

void NodeProcessor::LogSyncData()
{
	if (!IsFastSync())
		return;

	BEAM_LOG_INFO() << "Fast-sync mode up to block number " << m_SyncData.m_Target.m_Number.v;
}


void NodeProcessor::SaveSyncData()
{
	if (IsFastSync())
	{
		Blob blob(&m_SyncData, sizeof(m_SyncData));
		m_DB.ParamSet(NodeDB::ParamID::SyncData, nullptr, &blob);
	}
	else
		m_DB.ParamSet(NodeDB::ParamID::SyncData, nullptr, nullptr);
}

Asset::ID NodeProcessor::get_AidMax() const
{
	return (Asset::ID) (Rules::get().CA.ForeignEnd + m_Mmr.m_Assets.m_Count);
}

NodeProcessor::Mmr::Mmr(NodeDB& db)
	:m_States(db)
	,m_Shielded(db, NodeDB::StreamType::ShieldedMmr, true)
	,m_Assets(db, NodeDB::StreamType::AssetsMmr, true)
{
}

NodeProcessor::NodeProcessor()
	:m_Mmr(m_DB)
{
}

NodeProcessor::~NodeProcessor()
{
	if (m_DbTx.IsInProgress())
	{
		try {
			CommitMappingAndDB();
		} catch (const CorruptionException& e) {
			BEAM_LOG_ERROR() << "DB Commit failed: %s" << e.m_sErr;
		}
	}
}

void NodeProcessor::CommitMappingAndDB()
{
	Mapped::Stamp us;

	bool bFlushMapping = (m_Mapped.IsOpen() && m_Mapped.get_Hdr().m_Dirty);

	if (bFlushMapping)
	{
		Blob blob(us);

		if (m_DB.ParamGet(NodeDB::ParamID::MappingStamp, nullptr, &blob)) {
			ECC::Hash::Processor() << us >> us;
		} else {
			ECC::GenRandom(us);
		}

		m_DB.ParamSet(NodeDB::ParamID::MappingStamp, nullptr, &blob);
	}

	m_DbTx.Commit();

	if (bFlushMapping)
		m_Mapped.FlushStrict(us);
}

void NodeProcessor::Vacuum()
{
	if (m_DbTx.IsInProgress())
		m_DbTx.Commit();

	BEAM_LOG_INFO() << "DB compacting...";
	m_DB.Vacuum();
	BEAM_LOG_INFO() << "DB compacting completed";

	m_DbTx.Start(m_DB);
}

void NodeProcessor::CommitDB()
{
	if (m_DbTx.IsInProgress())
	{
		CommitMappingAndDB();
		m_DbTx.Start(m_DB);
	}
}

void NodeProcessor::RollbackDB()
{
	if (m_DbTx.IsInProgress())
	{
		m_DbTx.Rollback();
	}
}

void NodeProcessor::InitCursor(bool bMovingUp, const NodeDB::StateID& sid)
{
	if (sid.m_Number.v)
	{
		m_Cursor.m_Row = sid.m_Row;
		assert(m_Cursor.m_Row);

		m_Mmr.m_States.m_Count = sid.m_Number.v - 1;

		if (bMovingUp)
		{
			assert(m_Cursor.m_Full.m_Number.v == sid.m_Number.v); // must already initialized
			m_Cursor.m_History = m_Cursor.m_HistoryNext;

			assert(m_Cursor.m_Full.get_Height() == m_Cursor.m_hh.m_Height);
		}
		else
		{
			m_DB.get_State(sid.m_Row, m_Cursor.m_Full);
			m_Mmr.m_States.get_Hash(m_Cursor.m_History);

			m_DB.get_StateExtra(sid.m_Row, &m_Cursor.m_StateExtra, sizeof(m_Cursor.m_StateExtra));

			m_Cursor.m_bKernels = false;

			m_Cursor.m_Full.get_Hash(m_Cursor.m_hh.m_Hash);
			m_Cursor.m_hh.m_Height = m_Cursor.m_Full.get_Height();
		}

		m_Mmr.m_States.get_PredictedHash(m_Cursor.m_HistoryNext, m_Cursor.m_hh.m_Hash);
	}
	else
	{
		m_Mmr.m_States.m_Count = 0;
		ZeroObject(m_Cursor);
		m_Cursor.m_hh.m_Hash = Rules::get().Prehistoric;
	}

	m_Cursor.m_DifficultyNext = get_NextDifficulty();
}

Block::SystemState::ID NodeProcessor::Cursor::get_ID() const
{
	Block::SystemState::ID id;
	id.m_Number = m_Full.m_Number;
	id.m_Hash = m_hh.m_Hash;
	return id;
}

NodeDB::StateID NodeProcessor::Cursor::get_Sid() const
{
	NodeDB::StateID sid;
	sid.m_Number = m_Full.m_Number;
	sid.m_Row = m_Row;
	return sid;
}

void NodeProcessor::PbftState::Hash::Update()
{
	if (!m_Valid)
	{
		get_ParentObj().get_Hash(m_hv);
		m_Valid = true;
	}
}

NodeProcessor::CongestionCache::TipCongestion* NodeProcessor::CongestionCache::Find(const NodeDB::StateID& sid)
{
	TipCongestion* pRet = nullptr;

	for (TipList::iterator it = m_lstTips.begin(); m_lstTips.end() != it; ++it)
	{
		TipCongestion& x = *it;
		if (!x.IsContained(sid))
			continue;

		// in case of several matches prefer the one with lower height
		if (pRet && (pRet->m_Number.v <= x.m_Number.v))
			continue;

		pRet = &x;
	}

	return pRet;
}

bool NodeProcessor::CongestionCache::TipCongestion::IsContained(const NodeDB::StateID& sid)
{
	if (sid.m_Number.v > m_Number.v)
		return false;

	uint64_t dn = m_Number.v - sid.m_Number.v;
	if (dn >= m_Rows.size())
		return false;

	return (m_Rows.at(dn) == sid.m_Row);
}

NodeProcessor::CongestionCache::TipCongestion* NodeProcessor::EnumCongestionsInternal()
{
	assert(IsTreasuryHandled());

	CongestionCache cc;
	cc.m_lstTips.swap(m_CongestionCache.m_lstTips);

	CongestionCache::TipCongestion* pMaxTarget = nullptr;
	bool bMaxTargetNeedsHeaders = false;
	Difficulty::Raw cwMaxTarget;

	// Find all potentially missing data
	NodeDB::WalkerState ws;
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
					assert(p->m_Number.v >= sid.m_Number.v);
					while (p->m_Number.v > sid.m_Number.v)
					{
						p->m_Number.v--;
						p->m_Rows.pop_front();
					}

					if (pEntry)
					{
						for (size_t i = pEntry->m_Rows.size(); i--; p->m_Number.v++)
							p->m_Rows.push_front(pEntry->m_Rows.at(i));

						m_CongestionCache.m_lstTips.Delete(*pEntry);
					}

					cc.m_lstTips.erase(CongestionCache::TipList::s_iterator_to(*p));
					m_CongestionCache.m_lstTips.push_back(*p);

					while (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(p->m_Rows.at(p->m_Rows.size() - 1)))
						p->m_Rows.pop_back(); // already retrieved

					assert(p->m_Rows.size());

					sid.m_Row = p->m_Rows.at(p->m_Rows.size() - 1);
					sid.m_Number.v = p->m_Number.v - (p->m_Rows.size() - 1);

					pEntry = p;
					bCheckCache = false;
				}
			}

			if (!pEntry)
			{
				pEntry = m_CongestionCache.m_lstTips.Create_back();
				pEntry->m_Number.v = sid.m_Number.v;
			}

			if (bCheckCache)
			{
				CongestionCache::TipCongestion* p = m_CongestionCache.Find(sid);
				if (p)
				{
					assert(p != pEntry);

					// copy the rest
					for (size_t i = p->m_Number.v - sid.m_Number.v; i < p->m_Rows.size(); i++)
						pEntry->m_Rows.push_back(p->m_Rows.at(i));

					sid.m_Row = p->m_Rows.at(p->m_Rows.size() - 1);
					sid.m_Number.v = p->m_Number.v - (p->m_Rows.size() - 1);

					bCheckCache = false;
				}
			}

			if (pEntry->m_Number.v >= sid.m_Number.v + pEntry->m_Rows.size())
				pEntry->m_Rows.push_back(sid.m_Row);

			if (1u == sid.m_Number.v)
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

		Difficulty::Raw cw;
		m_DB.get_ChainWork(pEntry->m_Rows.at(0), cw);

		if (bNeedHdrs)
		{
			Difficulty::Raw cw2;
			m_DB.get_ChainWork(pEntry->m_Rows.at(pEntry->m_Rows.size() - 1), cw2);
			cw2.Negate();
			cw2 += cw; // difficulty of the very 1st block is missing, nevermind

			// ensure cw is no bigger than twice cw2 (proven Chainwork)
			cw2 += cw2;
			if (cw > cw2)
				cw = cw2;
		}

		// check if this candidate is better. Select the one with bigger ChainWork
		// If the candidate has all the headers down to the genesis - use the proven ChainWork
		// If headers are missing - use the *estimated* ChainWork, which is
		//		claimed ChainWork (in the last header)
		//		no bigger than twice proven ChainWork 

		if (!pMaxTarget || (cwMaxTarget < cw))
		{
			pMaxTarget = pEntry;
			cwMaxTarget = cw;
			bMaxTargetNeedsHeaders = bNeedHdrs;
		}
	}

	return bMaxTargetNeedsHeaders ? nullptr : pMaxTarget;
}

template <typename T>
bool IsBigger2(T a, T b1, T b2)
{
	b1 += b2;
	return (b1 >= b2) && (a > b1);
}

template <typename T>
bool IsBigger3(T a, T b1, T b2, T b3)
{
	b2 += b3;
	return (b2 >= b3) && IsBigger2(a, b1, b2);
}

void NodeProcessor::EnumCongestions()
{
	if (!IsTreasuryHandled())
	{
		Block::SystemState::ID id;
		ZeroObject(id);
		NodeDB::StateID sidTrg;
		sidTrg.SetNull();

		RequestData(id, true, sidTrg);
		return;
	}

	CongestionCache::TipCongestion* pMaxTarget = EnumCongestionsInternal();

	// Check the fast-sync status
	if (pMaxTarget)
	{
		bool bFirstTime =
			!IsFastSync() &&
			IsBigger3(pMaxTarget->m_Number.v, m_Cursor.m_Full.m_Number.v, m_Horizon.m_Sync.Hi, m_Horizon.m_Sync.Hi / 2);

		if (bFirstTime)
		{
			// first time target acquisition
			m_SyncData.m_n0.v = pMaxTarget->m_Number.v - pMaxTarget->m_Rows.size();

			if (pMaxTarget->m_Number.v > m_Horizon.m_Sync.Lo)
				m_SyncData.m_TxoLo.v = pMaxTarget->m_Number.v - m_Horizon.m_Sync.Lo;

			std::setmax(m_SyncData.m_TxoLo.v, m_Extra.m_TxoLo.v);
		}

		// check if the target should be moved fwd
		bool bTrgChange =
			(IsFastSync() || bFirstTime) &&
			IsBigger2(pMaxTarget->m_Number.v, m_SyncData.m_Target.m_Number.v, m_Horizon.m_Sync.Hi);

		if (bTrgChange)
		{
			Block::Number numTargetPrev(bFirstTime ? (pMaxTarget->m_Number.v - pMaxTarget->m_Rows.size()) : m_SyncData.m_Target.m_Number.v);

			m_SyncData.m_Target.m_Number.v = pMaxTarget->m_Number.v - m_Horizon.m_Sync.Hi;
			m_SyncData.m_Target.m_Row = pMaxTarget->m_Rows.at(pMaxTarget->m_Number.v - m_SyncData.m_Target.m_Number.v);

			if (m_SyncData.m_TxoLo.v)
			{
				// ensure no old blocks, which could be generated with incorrect TxLo
				//
				// Deleting all the blocks in the range is a time-consuming operation, whereas it's VERY unlikely there's any block in there
				// So we'll limit the height range by the maximum "sane" value (which is also very unlikely to contain any block).
				//
				// In a worst-case scenario (extremely unlikely) the sync will fail, then all the blocks will be deleted, and sync restarts
				Block::Number numMaxSane(m_Cursor.m_Full.m_Number.v + Rules::get().MaxRollback);
				if (numTargetPrev.v < numMaxSane.v)
				{
					if (m_SyncData.m_Target.m_Number.v <= numMaxSane.v)
						DeleteBlocksInRange(m_SyncData.m_Target, numTargetPrev);
					else
					{
						NodeDB::StateID sid;
						sid.m_Number = numMaxSane;
						sid.m_Row = pMaxTarget->m_Rows.at(pMaxTarget->m_Number.v - numMaxSane.v);

						DeleteBlocksInRange(sid, numTargetPrev);
					}
				}
			}

			SaveSyncData();
		}

		if (bFirstTime)
			LogSyncData();
	}

	// request missing data
	for (CongestionCache::TipList::iterator it = m_CongestionCache.m_lstTips.begin(); m_CongestionCache.m_lstTips.end() != it; ++it)
	{
		CongestionCache::TipCongestion& x = *it;

		if (!(x.m_bNeedHdrs || (&x == pMaxTarget)))
			continue; // current policy - ask only for blocks with the largest proven (wrt headers) chainwork

		Block::SystemState::ID id;

		NodeDB::StateID sidTrg;
		sidTrg.m_Number = x.m_Number;
		sidTrg.m_Row = x.m_Rows.at(0);

		if (!x.m_bNeedHdrs)
		{
			if (IsFastSync() && !x.IsContained(m_SyncData.m_Target))
				continue; // ignore irrelevant branches

			NodeDB::StateID sid;
			sid.m_Number.v = x.m_Number.v - (x.m_Rows.size() - 1);
			sid.m_Row = x.m_Rows.at(x.m_Rows.size() - 1);

			m_DB.get_StateHash(sid.m_Row, id.m_Hash);
			id.m_Number = sid.m_Number;

			RequestDataInternal(id, sid.m_Row, true, sidTrg);
		}
		else
		{
			uint64_t rowid = x.m_Rows.at(x.m_Rows.size() - 1);

			Block::SystemState::Full s;
			m_DB.get_State(rowid, s);

			id.m_Number.v = s.m_Number.v - 1;
			id.m_Hash = s.m_Prev;

			RequestDataInternal(id, rowid, false, sidTrg);
		}
	}
}

const uint64_t* NodeProcessor::get_CachedRows(const NodeDB::StateID& sid, Height nCountExtra)
{
	EnumCongestionsInternal();

	CongestionCache::TipCongestion* pVal = m_CongestionCache.Find(sid);
	if (pVal)
	{
		assert(pVal->m_Number.v >= sid.m_Number.v);
		uint64_t dn = (pVal->m_Number.v - sid.m_Number.v);

		if (pVal->m_Rows.size() > nCountExtra + dn)
			return &pVal->m_Rows.at(dn);
	}
	return nullptr;
}

uint32_t NodeProcessor::get_MaxAutoRollback()
{
	return Rules::get().MaxRollback;
}

Block::Number NodeProcessor::get_LowestManualReturnNumber()
{
	return Block::Number(std::max(m_Extra.m_TxoHi.v, m_Extra.m_Fossil.v));
}

Block::Number NodeProcessor::get_LowestReturnNumber()
{
	Block::Number numRet = get_LowestManualReturnNumber();

	Block::Number n0 = IsFastSync() ? m_SyncData.m_n0 : m_Cursor.m_Full.m_Number;
	uint32_t nMaxRollback = get_MaxAutoRollback();

	if (n0.v > nMaxRollback)
	{
		n0.v -= nMaxRollback;
		std::setmax(numRet.v, n0.v);
	}

	return numRet;
}

void NodeProcessor::RequestDataInternal(const Block::SystemState::ID& id, uint64_t row, bool bBlock, const NodeDB::StateID& sidTrg)
{
	if (id.m_Number.v < get_LowestReturnNumber().v) {
		m_UnreachableLog.Log(id);
		return;
	}

	if (!m_ManualSelection.IsAllowed(id.m_Number, id.m_Hash)) {
		return;
	}

	RequestData(id, bBlock, sidTrg);
}

void NodeProcessor::UnreachableLog::Log(const Block::SystemState::ID& id)
{
	uint32_t nTime_ms = GetTimeNnz_ms();
	if (m_hvLast == id.m_Hash) {
		// suppress spam logging for 10 sec.
		if (m_Time_ms && (nTime_ms - m_Time_ms < 10000))
			return;
	}
	else {
		m_hvLast = id.m_Hash;
	}

	m_Time_ms = nTime_ms;
	BEAM_LOG_WARNING() << id << " State unreachable"; // probably will pollute the log, but it's a critical situation anyway
}

struct NodeProcessor::MultiSigmaContext
{
	static const uint32_t s_Chunk = 0x400;

	struct Node
	{
		struct ID
			:public boost::intrusive::set_base_hook<>
		{
			TxoID m_Value;
			bool operator < (const ID& x) const { return (m_Value < x.m_Value); }

			IMPLEMENT_GET_PARENT_OBJ(Node, m_ID)
		} m_ID;

		ECC::Scalar::Native m_pS[s_Chunk];
		uint32_t m_Min, m_Max;

		typedef boost::intrusive::multiset<ID> IDSet;
	};


	std::mutex m_Mutex;
	Node::IDSet m_Set;

	void Add(TxoID id0, uint32_t nCount, const ECC::Scalar::Native*);
	void ClearLocked();

	~MultiSigmaContext()
	{
		ClearLocked();
	}

	void Calculate(ECC::Point::Native&, NodeProcessor&);

private:

	struct MyTask;

	void DeleteRaw(Node&);
	std::vector<ECC::Point::Native> m_vRes;

	virtual Sigma::CmList& get_List() = 0;
	virtual void PrepareList(NodeProcessor&, const Node&) = 0;
};

void NodeProcessor::MultiSigmaContext::ClearLocked()
{
	while (!m_Set.empty())
		DeleteRaw(m_Set.begin()->get_ParentObj());
}

void NodeProcessor::MultiSigmaContext::DeleteRaw(Node& n)
{
	m_Set.erase(Node::IDSet::s_iterator_to(n.m_ID));
	delete &n;
}

void NodeProcessor::MultiSigmaContext::Add(TxoID id0, uint32_t nCount, const ECC::Scalar::Native* pS)
{
	uint32_t nOffset = static_cast<uint32_t>(id0 % s_Chunk);

	Node::ID key;
	key.m_Value = id0 - nOffset;

	std::unique_lock<std::mutex> scope(m_Mutex);

	while (nCount)
	{
		uint32_t nPortion = std::min(nCount, s_Chunk - nOffset);
		
		Node::IDSet::iterator it = m_Set.find(key);
		bool bNew = (m_Set.end() == it);
		if (bNew)
		{
			Node* pN = new Node;
			pN->m_ID = key;
			m_Set.insert(pN->m_ID);
			it = Node::IDSet::s_iterator_to(pN->m_ID);
		}

		Node& n = it->get_ParentObj();
		if (bNew)
		{
			n.m_Min = nOffset;
			n.m_Max = nOffset + nPortion;
		}
		else
		{
			std::setmin(n.m_Min, nOffset);
			std::setmax(n.m_Max, nOffset + nPortion);
		}

		ECC::Scalar::Native* pT = n.m_pS + nOffset;
		for (uint32_t i = 0; i < nPortion; i++)
			pT[i] += pS[i];

		pS += nPortion;
		nCount -= nPortion;
		key.m_Value += s_Chunk;
		nOffset = 0;
	}
}

struct NodeProcessor::MultiSigmaContext::MyTask
	:public Executor::TaskSync
{
	MultiSigmaContext* m_pThis;
	const Node* m_pNode;

	virtual void Exec(Executor::Context& ctx) override
	{
		ECC::Point::Native& val = m_pThis->m_vRes[ctx.m_iThread];
		val = Zero;

		uint32_t i0, nCount;
		ctx.get_Portion(i0, nCount, m_pNode->m_Max - m_pNode->m_Min);
		i0 += m_pNode->m_Min;

		m_pThis->get_List().Calculate(val, i0, nCount, m_pNode->m_pS);
	}
};

void NodeProcessor::MultiSigmaContext::Calculate(ECC::Point::Native& res, NodeProcessor& np)
{
	Executor& ex = np.get_Executor();
	uint32_t nThreads = ex.get_Threads();

	while (!m_Set.empty())
	{
		Node& n = m_Set.begin()->get_ParentObj();
		assert(n.m_Min < n.m_Max);
		assert(n.m_Max <= s_Chunk);

		m_vRes.resize(nThreads);
		PrepareList(np, n);

		MyTask t;
		t.m_pThis = this;
		t.m_pNode = &n;

		ex.ExecAll(t);

		for (uint32_t i = 0; i < nThreads; i++)
			res += m_vRes[i];

		DeleteRaw(n);
	}
}

struct NodeProcessor::MultiShieldedContext
	:public NodeProcessor::MultiSigmaContext
{
	ValidatedCache m_Vc;

	void MoveToGlobalCache(ValidatedCache& vc)
	{
		m_Vc.MoveInto(vc);
		vc.ShrinkTo(10 * 1024);
	}

	bool IsValid(const TxVectors::Eternal&, Height, ECC::InnerProduct::BatchContext&, uint32_t iVerifier, uint32_t nTotal, ValidatedCache&);
	void Prepare(const TxVectors::Eternal&, NodeProcessor&, Height);

private:

	Sigma::CmListVec m_Lst;

	bool IsValid(const TxKernelShieldedInput&, Height hScheme, std::vector<ECC::Scalar::Native>& vBuf, ECC::InnerProduct::BatchContext&);

	virtual Sigma::CmList& get_List() override
	{
		return m_Lst;
	}

	virtual void PrepareList(NodeProcessor& np, const Node& n) override
	{
		m_Lst.m_vec.resize(s_Chunk); // will allocate if empty
		np.get_DB().ShieldedRead(n.m_ID.m_Value + n.m_Min, &m_Lst.m_vec.front() + n.m_Min, n.m_Max - n.m_Min);
	}

	struct Walker
		:public TxKernel::IWalker
	{
		virtual bool OnKrn(const TxKernelShieldedInput&) = 0;

		virtual bool OnKrn(const TxKernel& krn) override
		{
			if (TxKernel::Subtype::ShieldedInput != krn.get_Subtype())
				return true;
			return OnKrn(Cast::Up<TxKernelShieldedInput>(krn));
		}

	};
};

bool NodeProcessor::MultiShieldedContext::IsValid(const TxKernelShieldedInput& krn, Height hScheme, std::vector<ECC::Scalar::Native>& vKs, ECC::InnerProduct::BatchContext& bc)
{
	const Lelantus::Proof& x = krn.m_SpendProof;
	uint32_t N = x.m_Cfg.get_N();
	if (!N)
		return false;

	vKs.resize(N);
	memset0(&vKs.front(), sizeof(ECC::Scalar::Native) * N);

	ECC::Point::Native hGen;
	if (krn.m_pAsset)
		BEAM_VERIFY(hGen.Import(krn.m_pAsset->m_hGen)); // must already be tested in krn.IsValid();

	ECC::Oracle oracle;
	oracle << krn.get_Msg();

	if (Rules::get().IsPastFork_<3>(hScheme))
	{
		oracle << krn.m_NotSerialized.m_hvShieldedState;
		Asset::Proof::Expose(oracle, hScheme, krn.m_pAsset);
	}

	if (!x.IsValid(bc, oracle, &vKs.front(), &hGen))
		return false;

	TxoID id1 = krn.m_WindowEnd;
	if (id1 >= N)
		Add(id1 - N, N, &vKs.front());
	else
		Add(0, static_cast<uint32_t>(id1), &vKs.front() + N - static_cast<uint32_t>(id1));

	return true;
}

bool NodeProcessor::MultiShieldedContext::IsValid(const TxVectors::Eternal& txve, Height h, ECC::InnerProduct::BatchContext& bc, uint32_t iVerifier, uint32_t nTotal, ValidatedCache& vc)
{
	struct MyWalker
		:public Walker
	{
		std::vector<ECC::Scalar::Native> m_vKs;
		MultiShieldedContext* m_pThis;
		ValidatedCache* m_pVc;
		ECC::InnerProduct::BatchContext* m_pBc;
		uint32_t m_iVerifier;
		uint32_t m_Total;
		Height m_Height;

		virtual bool OnKrn(const TxKernelShieldedInput& v) override
		{
			if (!m_iVerifier)
			{
				ECC::Hash::Value hv;
				{
					ECC::Hash::Processor hp;
					hp.Serialize(v);
					hp << v.m_NotSerialized.m_hvShieldedState;
					hp >> hv;
				}

				bool bFound;
				{
					std::unique_lock<std::mutex> scope(m_pThis->m_Mutex);

					bFound =
						m_pThis->m_Vc.Find(hv) ||
						m_pVc->Find(hv);

					if (!bFound)
						m_pThis->m_Vc.Insert(hv, v.m_WindowEnd);
				}

				if (!bFound && !m_pThis->IsValid(v, m_Height, m_vKs, *m_pBc))
					return false;
			}

			if (++m_iVerifier == m_Total)
				m_iVerifier = 0;

			return true;
		}

	} wlk;
	wlk.m_pThis = this;
	wlk.m_pVc = &vc;
	wlk.m_pBc = &bc;
	wlk.m_iVerifier = iVerifier;
	wlk.m_Total = nTotal;
	wlk.m_Height = h;

	return wlk.Process(txve.m_vKernels);
}

void NodeProcessor::MultiShieldedContext::Prepare(const TxVectors::Eternal& txve, NodeProcessor& np, Height h)
{
	if (!Rules::get().IsPastFork_<3>(h))
		return;

	struct MyWalker
		:public Walker
	{
		NodeProcessor* m_pProc;

		virtual bool OnKrn(const TxKernelShieldedInput& v) override
		{
			auto& hv = Cast::NotConst(v.m_NotSerialized.m_hvShieldedState);
			// set it anyway, even if below HF3. This way the caching is more robust.

			auto nStatePos = v.m_WindowEnd - 1;
			if (nStatePos < m_pProc->m_Extra.m_ShieldedOutputs)
				m_pProc->get_DB().ShieldedStateRead(nStatePos, &hv, 1);
			else
				hv = Zero;

			return true;
		}

	} wlk;

	wlk.m_pProc = &np;

	wlk.Process(txve.m_vKernels);
}

struct NodeProcessor::MultiAssetContext
	:public NodeProcessor::MultiSigmaContext
{
	struct BatchCtx
		:public Asset::Proof::BatchContext
	{
		MultiAssetContext& m_Ctx;
		BatchCtx(MultiAssetContext& ctx) :m_Ctx(ctx) {}

		std::vector<ECC::Scalar::Native> m_vKs;

		virtual bool IsValid(Height, ECC::Point::Native& hGen, const Asset::Proof&) override;
	};

private:

	struct List
		:public Sigma::CmList
	{
		Asset::ID m_Begin;
		bool get_At(ECC::Point::Storage& pt_s, uint32_t iIdx) override
		{
			Asset::Base(m_Begin + iIdx).get_GeneratorSafe(pt_s);
			return true;
		}

	} m_Lst;

	virtual Sigma::CmList& get_List() override
	{
		return m_Lst;
	}

	virtual void PrepareList(NodeProcessor& np, const Node& n) override
	{
		static_assert(sizeof(n.m_ID.m_Value) >= sizeof(m_Lst.m_Begin));

		// TODO: maybe cache it in DB
		m_Lst.m_Begin = static_cast<Asset::ID>(n.m_ID.m_Value);
	}
};

bool NodeProcessor::MultiAssetContext::BatchCtx::IsValid(Height hScheme, ECC::Point::Native& hGen, const Asset::Proof& p)
{
	assert(ECC::InnerProduct::BatchContext::s_pInstance);
	ECC::InnerProduct::BatchContext& bc = *ECC::InnerProduct::BatchContext::s_pInstance;

	const Rules& r = Rules::get();
	const Sigma::Cfg& cfg = r.CA.m_ProofCfg;
	uint32_t N = cfg.get_N();
	if (!N)
		return false; // ?!

	m_vKs.resize(N); // will allocate if empty
	memset0(&m_vKs.front(), sizeof(ECC::Scalar::Native) * N);

	if (!p.IsValidPrepare(hGen, bc, &m_vKs.front()))
		return false;

	if (r.IsPastFork_<6>(hScheme))
	{
		m_Ctx.Add(0, 1, &m_vKs.front());
		m_Ctx.Add(p.m_Begin + 1u, N - 1, &m_vKs.front() + 1);
	}
	else
		m_Ctx.Add(p.m_Begin, N, &m_vKs.front());

	return true;
}

struct NodeProcessor::MultiblockContext
{
	NodeProcessor& m_This;

	std::mutex m_Mutex;

	TxoID m_id0;
	Block::NumberRange m_InProgress;
	PeerID  m_pidLast;

	MultiblockContext(NodeProcessor& np)
		:m_This(np)
	{
		m_InProgress.m_Max = m_This.m_Cursor.m_Full.m_Number;
		m_InProgress.m_Min.v = m_InProgress.m_Max.v + 1;
		assert(m_InProgress.IsEmpty());

		m_id0 = m_This.get_TxosBefore(Block::Number(m_This.m_SyncData.m_n0.v + 1)); // inputs of blocks below TxLo must be before this

		if (m_This.IsFastSync())
			m_Sigma.Import(m_This.m_SyncData.m_Sigma);
	}

	~MultiblockContext()
	{
		m_This.get_Executor().Flush();

		if (m_bBatchDirty)
		{
			// make sure we don't leave batch context in an invalid state
			struct Task0 :public Executor::TaskSync {
				virtual void Exec(Executor::Context&) override
				{
					ECC::InnerProduct::BatchContext* pBc = ECC::InnerProduct::BatchContext::s_pInstance;
					if (pBc)
						pBc->Reset();
				}
			};

			Task0 t;
			m_This.get_Executor().ExecAll(t);
		}
	}

	ECC::Scalar::Native m_Offset;
	ECC::Point::Native m_Sigma;

	MultiShieldedContext m_Msc;
	MultiAssetContext m_Mac;

	size_t m_SizePending = 0;
	bool m_bFail = false;
	bool m_bBatchDirty = false;
	std::string m_sErr;

	struct MyTask
		:public Executor::TaskAsync
	{
		virtual void Exec(Executor::Context&) override;
		virtual ~MyTask() {}

		struct Shared
		{
			typedef std::shared_ptr<Shared> Ptr;

			MultiblockContext& m_Mbc;
			uint32_t m_Done;

			Shared(MultiblockContext& mbc)
				:m_Mbc(mbc)
				,m_Done(0)
			{
			}

			virtual ~Shared() {} // auto

			virtual void Exec(uint32_t iVerifier) = 0;
		};

		struct SharedBlock
			:public Shared
		{
			typedef std::shared_ptr<SharedBlock> Ptr;

			Block::Body m_Body;
			size_t m_Size;
			TxBase::Context m_Ctx;
			Block::Number m_Number;

			SharedBlock(MultiblockContext& mbc, Block::Number num)
				:Shared(mbc)
				,m_Number(num)
			{
			}

			virtual ~SharedBlock() {} // auto

			virtual void Exec(uint32_t iVerifier) override;
		};

		Shared::Ptr m_pShared;
		uint32_t m_iVerifier;
	};

	bool Flush()
	{
		FlushInternal();
		return !m_bFail;
	}

	void FlushInternal()
	{
		if (m_bFail || m_InProgress.IsEmpty())
			return;

		Executor& ex = m_This.get_Executor();
		ex.Flush();

		if (m_bFail)
			return;

		if (m_bBatchDirty)
		{
			struct Task1 :public Executor::TaskSync
			{
				MultiblockContext* m_pMbc;
				ECC::Point::Native* m_pBatchSigma;
				virtual void Exec(Executor::Context&) override
				{
					ECC::InnerProduct::BatchContext* pBc = ECC::InnerProduct::BatchContext::s_pInstance;
					if (pBc && !pBc->Flush())
					{
						{
							std::unique_lock<std::mutex> scope(m_pMbc->m_Mutex);
							(*m_pBatchSigma) += pBc->m_Sum;

						}
						pBc->m_Sum = Zero;
					}
				}
			};

			ECC::Point::Native ptBatchSigma;

			Task1 t;
			t.m_pMbc = this;
			t.m_pBatchSigma = &ptBatchSigma;
			ex.ExecAll(t);
			assert(!m_bFail);
			m_bBatchDirty = false;

			m_Msc.Calculate(ptBatchSigma, m_This);
			m_Mac.Calculate(ptBatchSigma, m_This);

			if (!(ptBatchSigma == Zero))
			{
				m_sErr = "Sigma nnz";
				m_bFail = true;
				return;
			}
		}

		if (m_This.IsFastSync())
		{
			if (!(m_Offset == Zero))
			{
				ECC::Mode::Scope scopeFast(ECC::Mode::Fast);

				m_Sigma += ECC::Context::get().G * m_Offset;
				m_Offset = Zero;
			}

			if (m_InProgress.m_Max.v == m_This.m_SyncData.m_TxoLo.v)
			{
				Exc::CheckpointTxt cp("multi-block finalization"); // finalize multi-block arithmetics
				
				TxBase::Context ctx;
				ctx.m_Params.m_bAllowUnsignedOutputs = true; // ignore verification of locked coinbase

				// The following assmes PoW network, block number is height, and the height range is needed to calculate the reward
				// Otherise this has no effect
				ctx.m_Height.m_Min = m_This.m_SyncData.m_n0.v + 1;
				ctx.m_Height.m_Max = m_This.m_SyncData.m_TxoLo.v;

				ctx.m_Sigma = m_Sigma;

				try {
					ctx.TestValidBlock();
				} catch (const std::exception& e) {

					m_bFail = true;
					m_sErr = e.what();
					OnFastSyncFailedOnLo();

					return;
				}

				m_Sigma = Zero;
			}

			m_Sigma.Export(m_This.m_SyncData.m_Sigma);
			m_This.SaveSyncData();
		}
		else
		{
			assert(m_Offset == Zero);
			assert(m_Sigma == Zero);
		}

		m_InProgress.m_Min.v = m_InProgress.m_Max.v + 1;

		m_Msc.MoveToGlobalCache(m_This.m_ValCache);
	}

	void OnBlock(const PeerID& pid, const MyTask::SharedBlock::Ptr& pShared)
	{
		assert(pShared->m_Ctx.m_Height.m_Min == pShared->m_Ctx.m_Height.m_Max);
		assert(pShared->m_Ctx.m_Height.m_Min > m_This.m_Cursor.m_hh.m_Height);

		if (m_bFail)
			return;

		bool bMustFlush =
			!m_InProgress.IsEmpty() &&
			(
				(m_pidLast != pid) || // PeerID changed
				(m_InProgress.m_Max.v == m_This.m_SyncData.m_TxoLo.v) // range complete up to TxLo
			);

		if (bMustFlush && !Flush())
			return;

		m_pidLast = pid;

		const size_t nSizeMax = 1024 * 1024 * 10; // fair enough

		Executor& ex = m_This.get_Executor();
		for (uint32_t nTasks = static_cast<uint32_t>(-1); ; )
		{
			{
				std::unique_lock<std::mutex> scope(m_Mutex);
				if (m_SizePending <= nSizeMax)
				{
					m_SizePending += pShared->m_Size;
					break;
				}
			}

			assert(nTasks);
			nTasks = ex.Flush(nTasks - 1);
		}

		m_InProgress.m_Max = pShared->m_Number;

		bool bFull = (pShared->m_Number.v > m_This.m_SyncData.m_Target.m_Number.v);

		pShared->m_Ctx.m_Params.m_bAllowUnsignedOutputs = !bFull;
		pShared->m_Ctx.m_Params.m_pAbort = &m_bFail;
		pShared->m_Ctx.m_Params.m_nVerifiers = ex.get_Threads();

		// pre-Realize all the kernels, since they'll be tested asynchronously (in worker threads)
		for (const auto& pKrn : pShared->m_Body.m_vKernels)
			pKrn->EnsureID();

		m_Msc.Prepare(pShared->m_Body, m_This, pShared->m_Ctx.m_Height.m_Min);

		PushTasks(pShared, pShared->m_Ctx.m_Params);
	}

	void PushTasks(const MyTask::Shared::Ptr& pShared, TxBase::Context::Params& pars)
	{
		Executor& ex = m_This.get_Executor();
		m_bBatchDirty = true;

		pars.m_pAbort = &m_bFail;
		pars.m_nVerifiers = ex.get_Threads();

		for (uint32_t i = 0; i < pars.m_nVerifiers; i++)
		{
			auto pTask = std::make_unique<MyTask>();
			pTask->m_pShared = pShared;
			pTask->m_iVerifier = i;
			ex.Push(std::move(pTask));
		}
	}

	void OnFastSyncFailed(bool bDeleteBlocks)
	{
		// rapid rollback
		m_This.RollbackTo(m_This.m_SyncData.m_n0);
		m_InProgress.m_Max = m_This.m_Cursor.m_Full.m_Number;
		m_InProgress.m_Min.v = m_InProgress.m_Max.v + 1;

		if (bDeleteBlocks)
			m_This.DeleteBlocksInRange(m_This.m_SyncData.m_Target, m_This.m_SyncData.m_n0);

		m_This.m_SyncData.m_Sigma = Zero;

		if (m_This.m_SyncData.m_TxoLo.v > m_This.m_SyncData.m_n0.v)
		{
			BEAM_LOG_INFO() << "Retrying with lower TxLo";
			m_This.m_SyncData.m_TxoLo = m_This.m_SyncData.m_n0;
		}
		else {
			BEAM_LOG_WARNING() << "TxLo already low";
		}

		m_This.SaveSyncData();

		m_pidLast = Zero; // don't blame the last peer for the failure!
	}

	void OnFastSyncFailedOnLo()
	{
		// probably problem in lower blocks
		BEAM_LOG_WARNING() << "Fast-sync failed on first above-TxLo block.";
		m_pidLast = Zero; // don't blame the last peer
		OnFastSyncFailed(true);
	}
};

void NodeProcessor::MultiblockContext::MyTask::Exec(Executor::Context&)
{
	MultiAssetContext::BatchCtx bcAssets(m_pShared->m_Mbc.m_Mac);
	Asset::Proof::BatchContext::Scope scopeAssets(bcAssets);

	m_pShared->Exec(m_iVerifier);
}

void NodeProcessor::MultiblockContext::MyTask::SharedBlock::Exec(uint32_t iVerifier)
{
	TxBase::Context ctx;
	ctx.m_Params = m_Ctx.m_Params;
	ctx.m_Height = m_Ctx.m_Height;
	ctx.m_iVerifier = iVerifier;

	bool bSparse = (m_Number.v <= m_Mbc.m_This.m_SyncData.m_TxoLo.v);

	beam::TxBase txbDummy;
	if (bSparse)
		txbDummy.m_Offset = Zero;

	bool bValid = true;
	std::string sErr;

	try {
		ctx.ValidateAndSummarizeStrict(bSparse ? txbDummy : m_Body, m_Body.get_Reader());

		if (!m_Mbc.m_Msc.IsValid(m_Body, m_Ctx.m_Height.m_Min, *ECC::InnerProduct::BatchContext::s_pInstance, iVerifier, m_Ctx.m_Params.m_nVerifiers, m_Mbc.m_This.m_ValCache))
		{
			Exc::CheckpointTxt cp("Shielded proof");
			TxBase::Fail_Signature();
		}

	} catch (const std::exception& e) {
		sErr = e.what();
		bValid = false;
	}

	std::unique_lock<std::mutex> scope(m_Mbc.m_Mutex);

	if (!m_Mbc.m_bFail)
	{
		assert(m_Done < ctx.m_Params.m_nVerifiers);

		bool bLast = (++m_Done == ctx.m_Params.m_nVerifiers);
		if (bLast)
		{
			assert(m_Mbc.m_SizePending >= m_Size);
			m_Mbc.m_SizePending -= m_Size;
		}

		if (bValid)
		{
			try {

				m_Ctx.MergeStrict(ctx);

				if (bLast)
				{
					if (!bSparse)
						m_Ctx.TestValidBlock();
					else
					{
						m_Mbc.m_Offset += m_Body.m_Offset;
						m_Mbc.m_Sigma += m_Ctx.m_Sigma;
					}
				}

			} catch (const std::exception& e) {
				bValid = false;
				sErr = e.what();
			}
		}
	}

	if (!bValid)
	{
		m_Mbc.m_bFail = true;
		m_Mbc.m_sErr = std::move(sErr);
	}
}

void NodeProcessor::TryGoUp()
{
	if (!IsTreasuryHandled())
		return;

	bool bDirty = false;
	uint64_t rowid = m_Cursor.m_Row;

	while (true)
	{
		NodeDB::StateID sidTrg;

		{
			NodeDB::WalkerState ws;
			m_DB.EnumFunctionalTips(ws);

			if (!ws.MoveNext())
			{
				assert(!m_Cursor.m_Row);
				break; // nowhere to go
			}

			sidTrg = ws.m_Sid;

			Difficulty::Raw wrkTrg;
			m_DB.get_ChainWork(sidTrg.m_Row, wrkTrg);

			assert(wrkTrg >= m_Cursor.m_Full.m_ChainWork);
			if (wrkTrg == m_Cursor.m_Full.m_ChainWork)
				break; // already at maximum (though maybe at different tip)
		}

		TryGoTo(sidTrg);
		bDirty = true;
	}

	if (bDirty)
	{
		PruneOld();
		if (m_Cursor.m_Row != rowid)
			OnNewState();
	}
}

void NodeProcessor::TryGoTo(NodeDB::StateID& sidTrg)
{
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

	RollbackTo(sidTrg.m_Number);

	MultiblockContext mbc(*this);
	bool bContextFail = false, bKeepBlocks = false;

	NodeDB::StateID sidFwd = m_Cursor.get_Sid();

	size_t iPos = vPath.size();
	while (iPos)
	{
		sidFwd.m_Number.v = m_Cursor.m_Full.m_Number.v + 1;
		sidFwd.m_Row = vPath[--iPos];

		Block::SystemState::Full s;
		m_DB.get_State(sidFwd.m_Row, s); // need it for logging anyway

		HeightHash hh;
		s.get_ID(hh);

		if (!HandleBlock(hh, sidFwd.m_Row, s, mbc))
		{
			bContextFail = mbc.m_bFail = true;

			if (m_Cursor.m_Full.m_Number.v + 1 == m_SyncData.m_TxoLo.v)
				mbc.OnFastSyncFailedOnLo();

			break;
		}

		// Update mmr and cursor
		if (m_Cursor.m_Full.m_Number.v)
			m_Mmr.m_States.Append(m_Cursor.m_hh.m_Hash);

		m_DB.MoveFwd(sidFwd);
		m_Cursor.m_Full = s;
		m_Cursor.m_hh = hh;
		InitCursor(true, sidFwd);

		if (IsFastSync())
			m_DB.DelStateBlockPP(sidFwd.m_Row); // save space

		if (mbc.m_InProgress.m_Max.v == m_SyncData.m_Target.m_Number.v)
		{
			if (!mbc.Flush())
				break;

			OnFastSyncOver(mbc, bContextFail);

			if (mbc.m_bFail)
				bKeepBlocks = true;
		}

		if (mbc.m_bFail)
			break;
	}

	if (mbc.Flush())
		return; // at position

	if (!bContextFail)
		BEAM_LOG_WARNING() << "Context-free verification failed: " << mbc.m_sErr;

	RollbackTo(Block::Number(mbc.m_InProgress.m_Min.v - 1));

	if (bKeepBlocks)
		return;

	if (!(mbc.m_pidLast == Zero))
	{
		OnPeerInsane(mbc.m_pidLast);

		// delete all the consequent blocks from this peer
		for (; iPos; iPos--)
		{
			PeerID pid;
			if (!m_DB.get_Peer(vPath[iPos - 1], pid))
				break;

			if (pid != mbc.m_pidLast)
				break;

			sidFwd.m_Row = vPath[iPos - 1];
			sidFwd.m_Number.v++;
		}
	}

	BEAM_LOG_INFO() << "Deleting blocks range: " << (m_Cursor.m_Full.m_Number.v + 1) << "-" <<  sidFwd.m_Number.v;

	DeleteBlocksInRange(sidFwd, m_Cursor.m_Full.m_Number);
}

void NodeProcessor::OnFastSyncOver(MultiblockContext& mbc, bool& bContextFail)
{
	assert(mbc.m_InProgress.m_Max.v == m_SyncData.m_Target.m_Number.v);

	mbc.m_pidLast = Zero; // don't blame the last peer if something goes wrong
	NodeDB::StateID sidFail;
	sidFail.SetNull(); // suppress warning

	{
		// ensure no reduced UTXOs are left
		NodeDB::WalkerTxo wlk;
		for (m_DB.EnumTxos(wlk, mbc.m_id0); wlk.MoveNext(); )
		{
			if (wlk.m_SpendHeight != MaxHeight)
				continue;

			if (TxoIsNaked(wlk.m_Value))
			{
				bContextFail = mbc.m_bFail = true;
				mbc.m_sErr = "Utxo unsigned";
				m_DB.FindStateByTxoID(sidFail, wlk.m_ID);
				break;
			}
		}
	}

	if (mbc.m_bFail)
	{
		BEAM_LOG_WARNING() << "Fast-sync failed: " << mbc.m_sErr;

		if (!m_DB.get_Peer(sidFail.m_Row, mbc.m_pidLast))
			mbc.m_pidLast = Zero;

		if (m_SyncData.m_TxoLo.v > m_SyncData.m_n0.v)
		{
			mbc.OnFastSyncFailed(true);
		}
		else
		{
			// try to preserve blocks, recover them from the TXOs.

			ByteBuffer bbP, bbE;
			while (m_Cursor.m_Full.m_Number.v > m_SyncData.m_n0.v)
			{
				NodeDB::StateID sid = m_Cursor.get_Sid();

				bbP.clear();
				if (!GetBlock(sid, &bbE, &bbP, m_SyncData.m_n0, m_SyncData.m_TxoLo, m_SyncData.m_Target.m_Number, true))
					OnCorrupted();

				if (sidFail.m_Number.v == sid.m_Number.v)
				{
					bbP.clear();
					m_DB.SetStateNotFunctional(sid.m_Row);
				}

				RollbackTo(Block::Number(sid.m_Number.v - 1));

				PeerID peer;
				if (!m_DB.get_Peer(sid.m_Row, peer))
					peer = Zero;

				m_DB.SetStateBlock(sid.m_Row, bbP, bbE, peer);
				m_DB.set_StateTxosAndExtra(sid.m_Row, nullptr, nullptr, nullptr);
			}

			mbc.OnFastSyncFailed(false);
		}
	}
	else
	{
		BEAM_LOG_INFO() << "Fast-sync succeeded";

		// raise fossil height, hTxoLo, hTxoHi
		RaiseFossil(m_Cursor.m_Full.m_Number);
		RaiseTxoHi(m_Cursor.m_Full.m_Number);
		RaiseTxoLo(m_SyncData.m_TxoLo);

		ZeroObject(m_SyncData);
		SaveSyncData();

		OnFastSyncSucceeded();
	}
}

void NodeProcessor::DeleteBlocksInRange(const NodeDB::StateID& sidTop, Block::Number numStop)
{
	for (NodeDB::StateID sid = sidTop; sid.m_Number.v > numStop.v; )
	{
		DeleteBlock(sid.m_Row);

		if (!m_DB.get_Prev(sid))
			sid.SetNull();
	}
}

void NodeProcessor::DeleteBlock(uint64_t row)
{
	m_DB.DelStateBlockAll(row);
	m_DB.SetStateNotFunctional(row);
}

Height NodeProcessor::PruneOld()
{
	if (IsFastSync())
		return 0; // don't remove anything while in fast-sync mode

	Height hRet = 0;

	if (m_Cursor.m_Full.m_Number.v > m_Horizon.m_Branching + Rules::HeightGenesis - 1)
	{
		Block::Number num(m_Cursor.m_Full.m_Number.v - m_Horizon.m_Branching);

		while (true)
		{
			uint64_t rowid;
			{
				NodeDB::WalkerState ws;
				m_DB.EnumTips(ws);
				if (!ws.MoveNext())
					break;
				if (ws.m_Sid.m_Number.v >= num.v)
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

	if (IsBigger2(m_Cursor.m_Full.m_Number.v, m_Extra.m_Fossil.v, (uint64_t) Rules::get().MaxRollback))
		hRet += RaiseFossil(Block::Number(m_Cursor.m_Full.m_Number.v - Rules::get().MaxRollback));

	if (IsBigger2(m_Cursor.m_Full.m_Number.v, m_Extra.m_TxoLo.v, m_Horizon.m_Local.Lo))
		hRet += RaiseTxoLo(Block::Number(m_Cursor.m_Full.m_Number.v - m_Horizon.m_Local.Lo));

	if (IsBigger2(m_Cursor.m_Full.m_Number.v, m_Extra.m_TxoHi.v, m_Horizon.m_Local.Hi))
		hRet += RaiseTxoHi(Block::Number(m_Cursor.m_Full.m_Number.v - m_Horizon.m_Local.Hi));

	return hRet;
}

struct LongActionPlus
{
	LongAction m_La;
	bool m_Logging;

	LongActionPlus(const char* sz, Height h, Height hTrg, ILongAction* pExternalHandler)
	{
		assert(hTrg > h);
		auto dh = hTrg - h;
		if (dh < 10000)
		{
			m_Logging = false;
			return;
		}
		m_La.m_pExternal = pExternalHandler;
		m_Logging = true;
		m_La.Reset(sz, dh);
	}

	void OnProgress(Height h, Height hTrg)
	{
		if (m_Logging)
			m_La.OnProgress(m_La.m_Total + h - hTrg);
	}
};

Height NodeProcessor::RaiseFossil(Block::Number numTrg)
{
	if (numTrg.v <= m_Extra.m_Fossil.v)
		return 0;

	LongActionPlus la("Raising Fossil...", m_Extra.m_Fossil.v, numTrg.v, m_pExternalHandler);

	Height hRet = 0;

	while (m_Extra.m_Fossil.v < numTrg.v)
	{
		m_Extra.m_Fossil.v++;

		NodeDB::WalkerState ws;
		for (m_DB.EnumStatesAt(ws, m_Extra.m_Fossil); ws.MoveNext(); )
		{
			if (NodeDB::StateFlags::Active & m_DB.GetStateFlags(ws.m_Sid.m_Row))
				m_DB.DelStateBlockPPR(ws.m_Sid.m_Row);
			//else
			//	DeleteBlock(ws.m_Sid.m_Row);

			// Don't delete non-active blocks! For non-archieve nodes the whole abandoned branch will eventually be deleted.
			// For archieve node - keep abandoned blocks, to be able to analyze them later.

			hRet++;
		}
		la.OnProgress(m_Extra.m_Fossil.v, numTrg.v);

	}

	m_DB.ParamIntSet(NodeDB::ParamID::NumberFossil, m_Extra.m_Fossil.v);
	return hRet;
}

Height NodeProcessor::RaiseTxoLo(Block::Number numTrg)
{
	if (numTrg.v <= m_Extra.m_TxoLo.v)
		return 0;

	LongActionPlus la("Raising TxoLo...", m_Extra.m_TxoLo.v, numTrg.v, m_pExternalHandler);

	Height hRet = 0;
	std::vector<NodeDB::StateInput> v;

	while (m_Extra.m_TxoLo.v < numTrg.v)
	{
		++m_Extra.m_TxoLo.v;
		uint64_t rowid = FindActiveAtStrict(m_Extra.m_TxoLo);
		if (!m_DB.get_StateInputs(rowid, v))
			continue;

		size_t iRes = 0;
		for (size_t i = 0; i < v.size(); i++)
		{
			const NodeDB::StateInput& inp = v[i];
			TxoID id = inp.get_ID();
			if (id >= m_Extra.m_TxosTreasury)
				m_DB.TxoDel(id);
			else
			{
				if (iRes != i)
					v[iRes] = inp;
				iRes++;
			}
		}

		hRet += (v.size() - iRes);

		m_DB.set_StateInputs(rowid, &v.front(), iRes);

		la.OnProgress(m_Extra.m_TxoLo.v, numTrg.v);
	}

	m_Extra.m_TxoLo = numTrg;
	m_DB.ParamIntSet(NodeDB::ParamID::NumberTxoLo, m_Extra.m_TxoLo.v);

	return hRet;
}

Height NodeProcessor::RaiseTxoHi(Block::Number numTrg)
{
	if (numTrg.v <= m_Extra.m_TxoHi.v)
		return 0;

	LongActionPlus la("Raising TxoHi...", m_Extra.m_TxoHi.v, numTrg.v, m_pExternalHandler);


	Height hRet = 0;
	std::vector<NodeDB::StateInput> v;

	NodeDB::WalkerTxo wlk;

	while (m_Extra.m_TxoHi.v < numTrg.v)
	{
		++m_Extra.m_TxoHi.v;
		uint64_t rowid = FindActiveAtStrict(m_Extra.m_TxoHi);
		m_DB.get_StateInputs(rowid, v);

		for (size_t i = 0; i < v.size(); i++)
		{
			TxoID id = v[i].get_ID();

			m_DB.TxoGetValue(wlk, id);

			if (TxoIsNaked(wlk.m_Value))
				continue; //?!

			uint8_t pNaked[s_TxoNakedMax];
			TxoToNaked(pNaked, wlk.m_Value);

			m_DB.TxoSetValue(id, wlk.m_Value);
			hRet++;
		}

		la.OnProgress(m_Extra.m_TxoHi.v, numTrg.v);
	}

	m_DB.ParamIntSet(NodeDB::ParamID::NumberTxoHi, m_Extra.m_TxoHi.v);

	return hRet;
}

void NodeProcessor::TxoToNaked(uint8_t* pBuf, Blob& v)
{
	if (v.n < s_TxoNakedMin)
		OnCorrupted();

	const uint8_t* pSrc = reinterpret_cast<const uint8_t*>(v.p);
	v.p = pBuf;

	if (!(0x10 & pSrc[0]))
	{
		// simple case - just remove some flags and truncate.
		memcpy(pBuf, pSrc, s_TxoNakedMin);
		v.n = s_TxoNakedMin;
		pBuf[0] &= 3;

		return;
	}

	// complex case - the UTXO has Incubation period. Utxo must be re-read
	Deserializer der;
	der.reset(pSrc, v.n);

	Output outp;
	der & outp;

	outp.m_pConfidential.reset();
	outp.m_pPublic.reset();
	outp.m_pAsset.reset();

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

struct NodeProcessor::KrnFlyMmr
	:public Merkle::FlyMmr
{
	const TxVectors::Eternal& m_Txve;

	KrnFlyMmr(const TxVectors::Eternal& txve)
		:m_Txve(txve)
	{
		m_Count = txve.m_vKernels.size();
	}

	virtual void LoadElement(Merkle::Hash& hv, uint64_t n) const override {
		assert(n < m_Count);
		hv = m_Txve.m_vKernels[n]->get_ID();
	}
};

void NodeProcessor::EnsureCursorKernels()
{
	if (!m_Cursor.m_bKernels && m_Cursor.m_Row)
	{
		TxVectors::Eternal txve;
		ReadKrns(m_Cursor.m_Row, txve);

		KrnFlyMmr fmmr(txve);
		fmmr.get_Hash(m_Cursor.m_hvKernels);
		m_Cursor.m_bKernels = true;

	}
}

NodeProcessor::Evaluator::Evaluator(NodeProcessor& p)
	:m_Proc(p)
{
	m_Height = m_Proc.m_Cursor.m_hh.m_Height;
	m_Number = m_Proc.m_Cursor.m_Full.m_Number;
}

bool NodeProcessor::Evaluator::get_History(Merkle::Hash& hv)
{
	const Cursor& c = m_Proc.m_Cursor;
	if (m_Number.v == c.m_Full.m_Number.v)
		hv = c.m_History;
	else
	{
		assert(m_Number.v == c.m_Full.m_Number.v + 1);
		hv = c.m_HistoryNext;
	}
	return true;
}

bool NodeProcessor::Evaluator::get_Utxos(Merkle::Hash& hv)
{
	m_Proc.m_Mapped.m_Utxo.get_Hash(hv);
	return true;
}

bool NodeProcessor::Evaluator::get_Kernels(Merkle::Hash& hv)
{
	m_Proc.EnsureCursorKernels();
	hv = m_Proc.m_Cursor.m_hvKernels;
	return true;
}

bool NodeProcessor::Evaluator::get_Logs(Merkle::Hash& hv)
{
	hv = m_Proc.m_Cursor.m_StateExtra.m_hvLogs;
	return true;
}

void NodeProcessor::ReadKrns(uint64_t rowid, TxVectors::Eternal& txve)
{
	ByteBuffer bbE;
	m_DB.GetStateBlock(rowid, nullptr, &bbE, nullptr);

	Deserializer der;
	der.reset(bbE);
	der & txve;
}

bool NodeProcessor::Evaluator::get_Shielded(Merkle::Hash& hv)
{
	m_Proc.m_Mmr.m_Shielded.get_Hash(hv);
	return true;
}

bool NodeProcessor::Evaluator::get_Assets(Merkle::Hash& hv)
{
	m_Proc.m_Mmr.m_Assets.get_Hash(hv);
	return true;
}

bool NodeProcessor::Evaluator::get_Contracts(Merkle::Hash& hv)
{
	m_Proc.m_Mapped.m_Contract.get_Hash(hv);
	return true;
}

bool NodeProcessor::EvaluatorEx::get_Kernels(Merkle::Hash& hv)
{
	hv = m_hvKernels;
	return true;
}

bool NodeProcessor::EvaluatorEx::get_Logs(Merkle::Hash& hv)
{
	hv = m_Comms.m_hvLogs;
	return true;
}

bool NodeProcessor::EvaluatorEx::get_CSA(Merkle::Hash& hv)
{
	if (!Evaluator::get_CSA(hv))
		return false;

	m_Comms.m_hvCSA = hv;
	return true;
}

void NodeProcessor::ProofBuilder::OnProof(Merkle::Hash& hv, bool bNewOnRight)
{
	m_Proof.emplace_back();
	m_Proof.back().first = bNewOnRight;
	m_Proof.back().second = hv;
}

void NodeProcessor::ProofBuilderHard::OnProof(Merkle::Hash& hv, bool bNewOnRight)
{
	m_Proof.emplace_back();
	m_Proof.back() = hv;
}

struct NodeProcessor::ProofBuilder_PrevState
	:public ProofBuilder
{
	Merkle::Hash m_hvHistory;
	StateExtra::Full m_StateExtra;

	ProofBuilder_PrevState(NodeProcessor& p, Merkle::Proof& proof, const NodeDB::StateID& sid)
		:ProofBuilder(p, proof)
	{
		if (p.m_Cursor.m_Full.m_Number.v == sid.m_Number.v)
		{
			m_hvHistory = p.m_Cursor.m_History;
			Cast::Down<StateExtra::Comms>(m_StateExtra) = Cast::Down<StateExtra::Comms>(p.m_Cursor.m_StateExtra);
		}
		else
		{
			uint64_t nCount = sid.m_Number.v - 1;
			TemporarySwap<uint64_t> ts(nCount, p.m_Mmr.m_States.m_Count);
			p.m_Mmr.m_States.get_Hash(m_hvHistory);

			p.m_DB.get_StateExtra(sid.m_Row, &m_StateExtra, sizeof(m_StateExtra));
		}
	}

	virtual bool get_History(Merkle::Hash& hv) override
	{
		hv = m_hvHistory;
		return true;
	}

	virtual bool get_CSA(Merkle::Hash& hv) override
	{
		hv = m_StateExtra.m_hvCSA;
		return true;
	}
};

Height NodeProcessor::get_ProofKernel(Merkle::Proof* pProof, TxKernel::Ptr* ppRes, NodeDB::StateID& sid, const Merkle::Hash& idKrn, const HeightPos* pPos)
{
	Height h;

	if (pPos)
	{
		if (pPos->m_Height > m_Cursor.m_hh.m_Height)
			return 0;
		h = pPos->m_Height;
	}
	else
		h = m_DB.FindKernel(idKrn);

	if (h < Rules::HeightGenesis)
		return 0;

	FindActiveAtStrict(sid, h);

	TxVectors::Eternal txve;
	ReadKrns(sid.m_Row, txve);

	// find the target kernel
	auto iTrg = static_cast<uint32_t>(txve.m_vKernels.size());
	if (pPos)
	{
		if (pPos->m_Pos >= iTrg)
			return 0; // oob
		iTrg = pPos->m_Pos;
	}
	else
	{
		while (true)
		{
			if (!iTrg)
				OnCorrupted(); // not found

			TxKernel::Ptr& p = txve.m_vKernels[--iTrg];
			if (p->get_ID() == idKrn)
				break;
		}
	}

	if (pProof)
	{
		Merkle::FixedMmr mmr;
		mmr.Resize(txve.m_vKernels.size());

		for (const auto& p : txve.m_vKernels)
			mmr.Append(p->get_ID());

		mmr.get_Proof(*pProof, iTrg);

		if (Rules::get().IsPastFork_<3>(h))
		{
			struct MyProofBuilder
				:public ProofBuilder_PrevState
			{
				using ProofBuilder_PrevState::ProofBuilder_PrevState;

				virtual bool get_Kernels(Merkle::Hash&) override { return false; }

				virtual bool get_Logs(Merkle::Hash& hv) override
				{
					hv = m_StateExtra.m_hvLogs;
					return true;
				}
			};

			MyProofBuilder pb(*this, *pProof, sid);
			pb.GenerateProof();
		}
	}

	if (ppRes)
		*ppRes = std::move(txve.m_vKernels[iTrg]);

	return h;
}

bool NodeProcessor::get_ProofContractLog(Merkle::Proof& proof, const HeightPos& pos)
{
	if (pos.m_Height < Rules::HeightGenesis)
		return false; // it's possible to include contracts in treasury, and have logs. But proof won't be available

	Merkle::FixedMmr lmmr;
	uint64_t iTrg = static_cast<uint64_t>(-1);

	{
		NodeDB::ContractLog::Walker wlk;
		for (m_DB.ContractLogEnum(wlk, HeightPos(pos.m_Height), HeightPos(pos.m_Height, static_cast<uint32_t>(-1))); wlk.MoveNext(); )
		{
			if (!IsContractVarStoredInMmr(wlk.m_Entry.m_Key))
				continue;

			if (pos.m_Pos == wlk.m_Entry.m_Pos.m_Pos)
				iTrg = lmmr.m_Count; // found!

			Merkle::Hash hv;
			Block::get_HashContractLog(hv, wlk.m_Entry.m_Key, wlk.m_Entry.m_Val, wlk.m_Entry.m_Pos.m_Pos);

			lmmr.Resize(lmmr.m_Count + 1);
			lmmr.Append(hv);
		}
	}

	if (lmmr.m_Count <= iTrg)
		return false;

	lmmr.get_Proof(proof, iTrg);

	NodeDB::StateID sid;
	FindActiveAtStrict(sid, pos.m_Height);

	struct MyProofBuilder
		:public ProofBuilder_PrevState
	{
		using ProofBuilder_PrevState::ProofBuilder_PrevState;

		Merkle::Hash m_hvKernels;

		virtual bool get_Kernels(Merkle::Hash& hv) override
		{
			hv = m_hvKernels;
			return true;
		}

		virtual bool get_Logs(Merkle::Hash& hv) override
		{
			return false;
		}
	};

	MyProofBuilder pb(*this, proof, sid);

	{
		TxVectors::Eternal txve;
		ReadKrns(sid.m_Row, txve);

		KrnFlyMmr fmmr(txve);
		fmmr.get_Hash(pb.m_hvKernels);
	}

	pb.GenerateProof();

	return true;
}

struct NodeProcessor::BlockInterpretCtx
{
	Height m_Height;
	uint32_t m_nKrnIdx = 0;
	bool m_Fwd;

	bool m_AlreadyValidated = false; // Block/tx already validated, i.e. this is not the 1st invocation (reorgs, block generation multi-pass, etc.)
	bool m_Temporary = false; // Interpretation will be followed by 'undo', try to avoid heavy state changes (use mem vars whenever applicable)
	bool m_SkipDefinition = false; // no need to calculate the full definition (i.e. not generating/interpreting a block), MMR updates and etc. can be omitted
	bool m_DontUpdatePbft = true;
	bool m_LimitExceeded = false;
	bool m_TxValidation = false; // tx or block
	bool m_DependentCtxSet = false;
	bool m_SkipInOuts = false;
	uint8_t m_TxStatus = proto::TxStatus::Unspecified;
	std::ostream* m_pTxErrorInfo = nullptr;

	std::vector<InputAux> m_vInpAux;

	uint32_t m_ShieldedIns = 0;
	uint32_t m_ShieldedOuts = 0;
	Asset::ID m_AidMax = static_cast<Asset::ID>(-1); // last valid Asset ID

	uint32_t m_ContractLogs = 0;
	std::vector<Merkle::Hash> m_vLogs;

	ByteBuffer m_Rollback;

	Merkle::Hash m_hvDependentCtx;

	struct Ser
		:public Serializer
	{
		typedef uintBigFor<uint32_t>::Type Marker;

		BlockInterpretCtx& m_This;
		size_t m_Pos;

		Ser(BlockInterpretCtx&);
		~Ser();
	};

	struct Der
		:public Deserializer
	{
		Der(BlockInterpretCtx&);
	private:
		void SetBwd(ByteBuffer&, uint32_t nPortion);
	};

	BlobMap::Set m_Dups; // mirrors 'unique' DB table in temporary mode

	typedef std::multiset<Blob> BlobPtrSet; // like BlobMap, but buffers are not allocated/copied
	BlobPtrSet m_KrnIDs; // mirrors kernel ID DB table in temporary mode

	struct VmProcessorBase
	{
		BlockInterpretCtx& m_Bic;
		NodeProcessor& m_Proc;

		VmProcessorBase(BlockInterpretCtx& bic, NodeProcessor& db);

		struct RecoveryTag {
			typedef uint8_t Type;
			static const Type Terminator = 0;
			static const Type Insert = 1;
			static const Type Update = 2;
			static const Type Delete = 3;
			static const Type AssetCreate = 4;
			static const Type AssetEmit = 5;
			static const Type AssetDestroy = 6;
			static const Type Log = 7;
			static const Type Recharge = 8;
		};

		void ContractDataInsert(const Blob& key, const Blob&);
		void ContractDataUpdate(const Blob& key, const Blob& val, const Blob& valOld);
		void ContractDataDel(const Blob& key, const Blob& valOld);

		void ContractDataToggleTree(const Blob& key, const Blob&, bool bAdd);

		void ContractDataSaveWithRecovery(BlobMap::Entry&, const Blob&);

		void UpdChargeWithRecovery(uint32_t);

		void UndoVars();
	};

	struct BvmProcessor
		:public VmProcessorBase
		,public bvm2::ProcessorContract
	{

		uint32_t m_AssetEvtSubIdx = 0;

		using VmProcessorBase::VmProcessorBase;


		virtual void LoadVar(const Blob& key, Blob& res) override;
		virtual void LoadVarEx(Blob& key, Blob& res, bool bExact, bool bBigger) override;
		virtual uint32_t SaveVar(const Blob& key, const Blob&) override;
		virtual uint32_t OnLog(const Blob& key, const Blob& val) override;
		virtual bool get_AssetInfo(Asset::Full&) override;

		virtual Height get_Height() override;
		virtual bool get_HdrAt(Block::SystemState::Full&, Height) override;

		virtual Asset::ID AssetCreate(const Asset::Metadata&, const PeerID&, Amount& valDeposit) override;
		virtual bool AssetEmit(Asset::ID, const PeerID&, AmountSigned) override;
		virtual bool AssetDestroy(Asset::ID, const PeerID&, Amount& valDeposit) override;

		BlobMap::Entry* FindVarEx(const Blob& key, bool bExact, bool bBigger);
		bool EnsureNoVars(const bvm2::ContractID&);
		static bool IsOwnedVar(const bvm2::ContractID&, const Blob& key);

		bool Invoke(const bvm2::ContractID&, uint32_t iMethod, const TxKernelContractControl&);

		void ParseExtraInfo(ContractInvokeExtraInfo&, const bvm2::ShaderID&, uint32_t iMethod, const Blob& args);

		virtual void CallFar(const bvm2::ContractID&, uint32_t iMethod, Wasm::Word pArgs, uint32_t nArgs, uint32_t nFlags) override;
		virtual void OnRet(Wasm::Word nRetAddr) override;

		uint32_t m_iCurrentInvokeExtraInfo = 0;
		uint32_t m_iSig0 = 0;

		void CopyExtraInfoSigs();
	};


	struct MyEvmProcessor
		:public VmProcessorBase
		,public Evm::Processor
	{
		Height get_Height() override;
		bool get_BlockHeader(BlockHeader&, Height) override;

		Evm::Processor::Account& LoadAccount(const Evm::Address& addr) override;
		Evm::Processor::Account::Slot& LoadSlot(Evm::Processor::Account&, const Evm::Word&) override;

		struct AccountWrap :public Evm::Processor::Account
		{
			BlobMap::Entry* m_pEntry;
			BlobMap::Entry* m_pContractCode;
			uint64_t m_Nonce;
		};

		struct SlotWrap :public Evm::Processor::Account::Slot {
			BlobMap::Entry* m_pEntry;
		};

		struct AccountKey {
			Evm::Address m_Addr;
			Evm::Word m_Var;
		};

		AccountWrap& LoadAccountInternal(const Evm::Address& addr);

		using VmProcessorBase::VmProcessorBase;
		void InvokeGuarded(const TxKernelEvmInvoke&);
		void CommitChanges();
	};

	uint32_t m_ChargePerBlock = bvm2::Limits::BlockCharge;

	BlobMap::Set m_ContractVars;
	BlobMap::Entry& get_ContractVar(const Blob& key, NodeDB& db);

	std::vector<ContractInvokeExtraInfo>* m_pvC = nullptr;

	BlockInterpretCtx(Height h, bool bFwd)
		:m_Height(h)
		,m_Fwd(bFwd)
	{
	}

	void SetAidMax(const NodeProcessor& np)
	{
		m_AidMax = np.get_AidMax();
	}

	bool ValidateAssetRange(const Asset::Proof::Ptr& p) const
	{
		if (!p || (p->m_Begin <= m_AidMax))
			return true;

		if (m_pTxErrorInfo)
			*m_pTxErrorInfo << "asset range oob " << p->m_Begin << ", limit=" << m_AidMax;

		return false;

	}

	void AddKrnInfo(Serializer&, NodeDB& db);

	static uint64_t get_AssetEvtIdx(uint32_t nKrnIdx, uint32_t nSubIdx) {
		return (static_cast<uint64_t>(nKrnIdx) << 32) | nSubIdx;
	}

	void AssetEvtInsert(NodeDB&, NodeDB::AssetEvt&, uint32_t nSubIdx);

	struct ChangesFlushGlobal
	{
		TxoID m_ShieldedInputs;
		ChangesFlushGlobal(NodeProcessor& p)
		{
			m_ShieldedInputs = p.get_ShieldedInputs();
		}

		void Do(NodeProcessor& p)
		{
			TxoID val = p.get_ShieldedInputs();
			if (val != m_ShieldedInputs)
				p.m_DB.ParamIntSet(NodeDB::ParamID::ShieldedInputs, val);
		}
	};

	struct ChangesFlush
		:public ChangesFlushGlobal
	{
		TxoID m_ShieldedOutps;
		ChangesFlush(NodeProcessor& p)
			:ChangesFlushGlobal(p)
		{
			m_ShieldedOutps = p.m_Extra.m_ShieldedOutputs;
		}

		void Do(NodeProcessor& p, Height h)
		{
			if (p.m_Extra.m_ShieldedOutputs != m_ShieldedOutps)
				p.m_DB.ShieldedOutpSet(h, p.m_Extra.m_ShieldedOutputs);

			ChangesFlushGlobal::Do(p);
		}
	};
};

bool NodeProcessor::ExtractTreasury(const Blob& blob, Treasury::Data& td)
{
	Deserializer der;
	der.reset(blob.p, blob.n);

	try {
		der & td;
	}
	catch (const std::exception&) {
		BEAM_LOG_WARNING() << "Treasury corrupt";
		return false;
	}

	return true;
}

bool NodeProcessor::HandleTreasury(const Blob& blob)
{
	assert(!IsTreasuryHandled());

	Treasury::Data td;
	if (!ExtractTreasury(blob, td))
		return false;

	if (!td.IsValid())
	{
		BEAM_LOG_WARNING() << "Treasury validation failed";
		return false;
	}

	{
		std::vector<Treasury::Data::Burst> vBursts = td.get_Bursts();

		std::ostringstream os;
		os << "Treasury check. Total bursts=" << vBursts.size();

		for (size_t i = 0; i < vBursts.size(); i++)
		{
			const Treasury::Data::Burst& b = vBursts[i];
			os << "\n\t" << "Height=" << b.m_Height << ", Value=" << b.m_Value;
		}

		BEAM_LOG_INFO() << os.str();
	}

	BlockInterpretCtx bic(0, true);
	BlockInterpretCtx::ChangesFlush cf(*this);
	bic.SetAidMax(*this);
	for (size_t iG = 0; iG < td.m_vGroups.size(); iG++)
	{
		if (!HandleValidatedTx(td.m_vGroups[iG].m_Data, bic))
		{
			// undo partial changes
			bic.m_Fwd = false;
			while (iG--)
			{
				if (!HandleValidatedTx(td.m_vGroups[iG].m_Data, bic))
					OnCorrupted(); // although should not happen anyway
			}

			BEAM_LOG_WARNING() << "Treasury invalid";
			return false;
		}
	}

	cf.Do(*this, 0);

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

	return true;
}

std::ostream& operator << (std::ostream& s, const LogSid& sid)
{
	Block::SystemState::ID id;
	id.m_Number = sid.m_Sid.m_Number;
	sid.m_DB.get_StateHash(sid.m_Sid.m_Row, id.m_Hash);

	s << id;
	return s;
}

void NodeProcessor::EvaluatorEx::set_Kernels(const TxVectors::Eternal& txe)
{
	KrnFlyMmr fmmr(txe);
	fmmr.get_Hash(m_hvKernels);
}

void NodeProcessor::EvaluatorEx::set_Logs(const std::vector<Merkle::Hash>& v)
{
	struct MyMmr
		:public Merkle::FlyMmr
	{
		const Merkle::Hash* m_pArr;

		virtual void LoadElement(Merkle::Hash& hv, uint64_t n) const override {
			hv = m_pArr[n];
		}

	} lmmr;

	lmmr.m_Count = v.size();
	if (lmmr.m_Count)
		lmmr.m_pArr = &v.front();

	lmmr.get_Hash(m_Comms.m_hvLogs);
}

struct NodeProcessor::MyRecognizer
{
	struct Handler
		:public Recognizer::IHandler
	{
		NodeProcessor& m_Proc;
		Handler(NodeProcessor& proc) :m_Proc(proc) {}


		void OnDummy(const CoinID& cid, Height h) override
		{
			m_Proc.OnDummy(cid, h);
		}

		void OnEvent(Height h, const proto::Event::Base& evt) override
		{
			m_Proc.OnEvent(h, evt);
		}

		void AssetEvtsGetStrict(NodeDB::AssetEvt& event, Height h, uint32_t nKrnIdx) override
		{
			NodeDB::WalkerAssetEvt wlk;
			m_Proc.m_DB.AssetEvtsGetStrict(wlk, h, BlockInterpretCtx::get_AssetEvtIdx(nKrnIdx, 0));
			event = Cast::Down<NodeDB::AssetEvt>(wlk);
		}

		void InsertEvent(const HeightPos& pos, const Blob& b, const Blob& key) override
		{
			m_Proc.m_DB.InsertEvent(m_pAccount->m_iAccount, pos, b, key);
		}

		bool FindEvents(const Blob& key, Recognizer::IEventHandler& h) override
		{
			NodeDB::WalkerEvent wlk;
			for (m_Proc.m_DB.FindEvents(wlk, m_pAccount->m_iAccount, key); wlk.MoveNext(); )
			{
				if (h.OnEvent(wlk.m_Pos.m_Height, wlk.m_Body))
					return true;
			}

			return false;
		}

	} m_Handler;

	Recognizer m_Recognizer;

	MyRecognizer(NodeProcessor& x)
		:m_Handler(x)
		,m_Recognizer(m_Handler, x.m_Extra)
	{
	}
};

void NodeProcessor::Account::InitFromOwner()
{
	m_vSh.resize(1); // Change this if/when we decide to use multiple keys

	for (Key::Index nIdx = 0; nIdx < m_vSh.size(); nIdx++)
		m_vSh[nIdx].FromOwner(*m_pOwner, nIdx);
}

std::string NodeProcessor::Account::get_Endpoint() const
{
	Key::ID kid(Zero);
	kid.m_Type = ECC::Key::Type::EndPoint;

	PeerID pid;
	kid.get_Hash(pid);

	ECC::Point::Native ptN;
	m_pOwner->DerivePKeyG(ptN, pid);
	pid.Import(ptN);

	return Base58::to_string(pid);
}

bool NodeProcessor::TestBlock(const HeightHash& id, const Block::SystemState::Full& s, const proto::BodyBuffers& bufs)
{
	MultiblockContext mbc(*this);

	if (!HandleBlockInternal(id, s, mbc, bufs, true, true, Zero, 0))
		return false;

	return mbc.Flush();
}

bool NodeProcessor::HandleBlock(const HeightHash& id, uint64_t row, const Block::SystemState::Full& s, MultiblockContext& mbc)
{
	proto::BodyBuffers bufs;
	m_DB.GetStateBlock(row, &bufs.m_Perishable, &bufs.m_Eternal, nullptr);

	PeerID pid = Zero;

	bool bFirstTime = (m_DB.get_StateTxos(row) == MaxHeight);
	if (bFirstTime)
		m_DB.get_Peer(row, pid);

	return HandleBlockInternal(id, s, mbc, bufs, bFirstTime, false, pid, row);
}

bool NodeProcessor::HandleBlockInternal(const HeightHash& id, const Block::SystemState::Full& s, MultiblockContext& mbc, const proto::BodyBuffers& bufs, bool bFirstTime, bool bTestOnly, const PeerID& pid, uint64_t row)
{
	if (s.m_Number.v == m_ManualSelection.m_Sid.m_Number.v)
	{
		if (!m_ManualSelection.IsAllowed(id.m_Hash))
			return false;
	}

	MultiblockContext::MyTask::SharedBlock::Ptr pShared = std::make_shared<MultiblockContext::MyTask::SharedBlock>(mbc, s.m_Number);
	Block::Body& block = pShared->m_Body;

	const auto& r = Rules::get();

	try {
		Deserializer der;
		der.reset(bufs.m_Perishable);
		der & Cast::Down<Block::BodyBase>(block);
		der & Cast::Down<TxVectors::Perishable>(block);

		der.reset(bufs.m_Eternal);
		der & Cast::Down<TxVectors::Eternal>(block);

		if (Rules::Consensus::Pbft == r.m_Consensus)
		{
			Exc::CheckpointTxt cp("pbft block data");

			const auto& d = Cast::Reinterpret<Block::Pbft::HdrData>(s.m_PoW);

			m_PbftState.m_Hash.Update();
			if (d.m_hvVsPrev != m_PbftState.m_Hash.m_hv)
				Exc::Fail();

			uint32_t nSizeMetadata;
			d.m_nSizeMetadata.Export(nSizeMetadata);

			auto nBytes0 = der.bytes_left();
			if (nBytes0 < nSizeMetadata)
				Exc::Fail();

			// TODO: deserialize the metadata, check d.m_hvMetadata

			if (nBytes0 - der.bytes_left() != nSizeMetadata)
				Exc::Fail(); // wrong metadata size

			if (bFirstTime)
			{
				if (m_Cursor.m_Full.m_Number.v)
				{
					auto iSlot0 = r.m_Pbft.T2S(m_Cursor.m_Full.get_Timestamp_ms());
					auto iSlot1 = r.m_Pbft.T2S_strict(s.get_Timestamp_ms());

					if (iSlot1 <= iSlot0)
						Exc::Fail("invalid slot");

					if (iSlot1 - iSlot0 != (id.m_Height - m_Cursor.m_hh.m_Height))
						Exc::Fail("invalid time/slot/difficulty");
				}
				else
				{
					if (d.m_Difficulty.m_Packed != r.DA.Difficulty0.m_Packed)
						Exc::Fail("invalid 1st block height");

					assert(id.m_Height == 1u);
				}

				if (bTestOnly || r.m_Pbft.m_RequiredWhite)
				{
					// during block test the QC isn't ready yet
					// In whitelist mode the QC isn't appended to the block anyway
				}
				else
				{
					// Deserialize QC
					Block::Pbft::Quorum qc;
					der & qc;

					if (!m_PbftState.CheckQuorum(id.m_Hash, qc))
						Exc::Fail("QC");
				}

				// make sure nothing is appended instead of QC
				if (der.bytes_left())
					Exc::Fail("trailing junk");

				uint8_t nFlags = 0;
				if (block.IsEmpty() && !nSizeMetadata && m_Cursor.m_Full.m_Number.v)
					nFlags = Block::Pbft::HdrData::Flags::Empty;

				if (d.m_Flags1 != nFlags)
					Exc::Fail("flags mismatch");
			}
		}
	}
	catch (const std::exception& e) {
		BEAM_LOG_WARNING() << id << " Block deserialization failed " << e.what();
		return false;
	}

	if (bFirstTime)
	{
		pShared->m_Size = bufs.m_Perishable.size() + bufs.m_Eternal.size();
		pShared->m_Ctx.m_Height = id.m_Height;

		mbc.OnBlock(pid, pShared);

		if (Rules::Consensus::Pbft != r.m_Consensus)
		{
			// Chainwork test isn't really necessary, already tested in DB. Just for more safety.
			Difficulty::Raw wrk = m_Cursor.m_Full.m_ChainWork + s.m_PoW.m_Difficulty;

			if (wrk != s.m_ChainWork)
			{
				BEAM_LOG_WARNING() << id << " Chainwork expected=" << wrk << ", actual=" << s.m_ChainWork;
				return false;
			}

			if (m_Cursor.m_DifficultyNext.m_Packed != s.m_PoW.m_Difficulty.m_Packed)
			{
				BEAM_LOG_WARNING() << id << " Difficulty expected=" << m_Cursor.m_DifficultyNext << ", actual=" << s.m_PoW.m_Difficulty;
				return false;
			}

			if (s.m_TimeStamp <= get_MovingMedian())
			{
				BEAM_LOG_WARNING() << id << " Timestamp inconsistent wrt median";
				return false;
			}
		}
	}

	TxoID id0 = m_Extra.m_Txos;

	BlockInterpretCtx bic(id.m_Height, true);
	if (bTestOnly)
		bic.m_Temporary = true;

	BlockInterpretCtx::ChangesFlush cf(*this);
	bic.SetAidMax(*this);
	if (!bFirstTime)
		bic.m_AlreadyValidated = true;

	std::ostringstream osErr;
	bic.m_pTxErrorInfo = &osErr;

	std::vector<ContractInvokeExtraInfo> vC;
	if (m_DB.ParamIntGetDef(NodeDB::ParamID::RichContractInfo))
		bic.m_pvC = &vC;

	bool bOk = HandleValidatedBlock(block, bic);
	if (!bOk)
	{
		assert(bFirstTime);
		assert(m_Extra.m_Txos == id0);
		BEAM_LOG_WARNING() << id << " invalid in its context: " << osErr.str();
	}
	else
	{
		assert(m_Extra.m_Txos > id0);
	}

	EvaluatorEx ev(*this);
	ev.set_Kernels(block);
	ev.set_Logs(bic.m_vLogs);

	Merkle::Hash hvDef;
	ev.m_Height = id.m_Height;
	ev.m_Number = s.m_Number;

	bool bPastFork3 = r.IsPastFork_<3>(id.m_Height);
	bool bPastFastSync = (s.m_Number.v >= m_SyncData.m_TxoLo.v);
	bool bDefinition = bPastFork3 || bPastFastSync;

	if (bDefinition)
		ev.get_Definition(hvDef);

	if (bOk)
	{
		if (bFirstTime)
		{
			if (bDefinition)
			{
				// check the validity of state description.
				if (s.m_Definition != hvDef)
				{
					BEAM_LOG_WARNING() << id << " Header Definition mismatch";
					bOk = false;
				}
			}

			if (bPastFork3)
			{
				if (bPastFastSync)
				{
					get_Utxos().get_Hash(hvDef);
					if (s.m_Kernels != hvDef)
					{
						BEAM_LOG_WARNING() << id << " Utxos mismatch";
						bOk = false;
					}
				}
			}
			else
			{
				if (s.m_Kernels != ev.m_hvKernels)
				{
					BEAM_LOG_WARNING() << id << " Kernel commitment mismatch";
					bOk = false;
				}
			}

			if (s.m_Number.v <= m_SyncData.m_TxoLo.v)
			{
				// make sure no spent txos above the requested h0
				for (size_t i = 0; i < bic.m_vInpAux.size(); i++)
				{
					if (bic.m_vInpAux[i].m_ID >= mbc.m_id0)
					{
						BEAM_LOG_WARNING() << id << " Invalid input in sparse block";
						bOk = false;
						break;
					}
				}
			}
		}

		if (bOk && (Rules::Consensus::Pbft == r.m_Consensus))
		{
			// exec metadata, modify the validator set
			m_PbftState.m_Hash.Update();
			if (m_PbftState.m_Hash.m_hv != Cast::Reinterpret<Block::Pbft::HdrData>(s.m_PoW).m_hvVsNext)
			{
				BEAM_LOG_WARNING() << id << " pbft vs mismatch";
				bOk = false;
			}
		}

		if (!bOk || bTestOnly)
		{
			bic.m_Fwd = false;
			BEAM_VERIFY(HandleValidatedBlock(block, bic));
		}
	}

	if (bOk && !bTestOnly)
	{
		m_Cursor.m_hvKernels = ev.m_hvKernels;
		m_Cursor.m_bKernels = true;

		AdjustOffset(m_Cursor.m_StateExtra.m_TotalOffset, block.m_Offset, true);

		StateExtra::Comms& comms = m_Cursor.m_StateExtra; // downcast
		if (bDefinition)
			comms = ev.m_Comms;
		else
		{
			assert(!bPastFork3);
			ZeroObject(comms);
		}

		Blob blobExtra;
		blobExtra.p = &m_Cursor.m_StateExtra;
		if (bPastFork3)
		{
			blobExtra.n = sizeof(m_Cursor.m_StateExtra);

			// omit trailing hashes if they're zero
			for (; blobExtra.n; blobExtra.n--)
				if (reinterpret_cast<const uint8_t*>(blobExtra.p)[blobExtra.n - 1])
					break;
		}
		else
			blobExtra.n = sizeof(m_Cursor.m_StateExtra.m_TotalOffset);

		Blob blobRB(bic.m_Rollback);
		m_DB.set_StateTxosAndExtra(row, &m_Extra.m_Txos, &blobExtra, &blobRB);

		std::vector<NodeDB::StateInput> v;
		v.reserve(block.m_vInputs.size());

		assert(block.m_vInputs.size() == bic.m_vInpAux.size());
		for (size_t i = 0; i < block.m_vInputs.size(); i++)
		{
			const Input& x = *block.m_vInputs[i];
			auto txoID = bic.m_vInpAux[i].m_ID;

			m_DB.TxoSetSpent(txoID, id.m_Height);
			v.emplace_back().Set(txoID, x.m_Commitment);
		}

		if (!v.empty())
			m_DB.set_StateInputs(row, &v.front(), v.size());

		Serializer ser;

		if (Rules::Consensus::Pbft == r.m_Consensus)
		{
			bic.m_Rollback.clear();

			ser.swap_buf(bic.m_Rollback); // optimization
			ser & m_PbftState;
			ser.swap_buf(bic.m_Rollback);

			Blob blob(bic.m_Rollback);
			m_DB.ParamSet(NodeDB::ParamID::PbftState, nullptr, &blob);
		}

		// recognize all
		MyRecognizer rec(*this);

		for (const auto& acc : m_vAccounts)
		{
			rec.m_Handler.m_pAccount = &acc;
			rec.m_Recognizer.m_Pos = id.m_Height;
			rec.m_Recognizer.RecognizeBlock(block, bic.m_ShieldedOuts);
		}

		bic.m_Rollback.clear();
		ser.swap_buf(bic.m_Rollback); // optimization

		for (size_t i = 0; i < block.m_vOutputs.size(); i++)
		{
			const Output& x = *block.m_vOutputs[i];

			ser.reset();
			ser & x;

			SerializeBuffer sb = ser.buffer();
			m_DB.TxoAdd(id0++, Blob(sb.first, static_cast<uint32_t>(sb.second)));
		}

		m_RecentStates.Push(row, s);

		cf.Do(*this, id.m_Height);

		if (bic.m_pvC)
			bic.AddKrnInfo(ser, m_DB);
	}
	else
	{
		m_DB.AssetEvtsDeleteFrom(id.m_Height);

		if (!bOk)
			OnInvalidBlock(s, block);
	}

	return bOk;
}

void NodeProcessor::ReadOffset(ECC::Scalar& offs, uint64_t rowid)
{
	static_assert(sizeof(StateExtra::Base) == sizeof(offs));
	if (m_DB.get_StateExtra(rowid, &offs, sizeof(offs)) < sizeof(offs))
		OnCorrupted();
}

void NodeProcessor::AdjustOffset(ECC::Scalar& offs, const ECC::Scalar& offsPrev, bool bAdd)
{
	ECC::Scalar::Native s(offsPrev);
	if (!bAdd)
		s = -s;

	s += offs;
	offs = s;
}

template <typename TKey, typename TEvt>
bool NodeProcessor::Recognizer::FindEvent(const TKey& key, TEvt& evt, std::vector<TEvt>* pvDups /* = nullptr */)
{
	struct MyHandler
		:public Recognizer::IEventHandler
	{
		TEvt& m_Evt;
		std::vector<TEvt>* m_pvDups;
		bool m_Found = false;

		MyHandler(TEvt& x) :m_Evt(x) {}

		bool OnEvent(Height, const Blob& body) override
		{
			Deserializer der;

			proto::Event::Type::Enum eType;
			der.reset(body.p, body.n);
			eType = proto::Event::Type::Load(der);

			if (TEvt::s_Type != eType)
				return false;

			if (m_Found)
			{
				assert(m_pvDups);
				der & m_pvDups->emplace_back();
			}
			else
			{
				der & m_Evt;
				m_Found = true;
			}

			return !m_pvDups; // stop if not interested in dups
		}

	} h(evt);
	h.m_pvDups = pvDups;

	m_Handler.FindEvents(Blob(&key, sizeof(key)), h);

	return h.m_Found;
}

template <typename TEvt>
void NodeProcessor::Recognizer::AddEventInternal(const TEvt& evt, const Blob& key)
{
	Serializer ser;
	ser & TEvt::s_Type;
	ser & evt;

	m_Handler.InsertEvent(m_Pos, Blob(ser.buffer().first, static_cast<uint32_t>(ser.buffer().second)), key);
	m_Pos.m_Pos++;

	m_Handler.OnEvent(m_Pos.m_Height, evt);
}

template <typename TEvt, typename TKey>
void NodeProcessor::Recognizer::AddEvent(const TEvt& evt, const TKey& key)
{
	AddEventInternal(evt, Blob(&key, sizeof(key)));
}

template <typename TEvt>
void NodeProcessor::Recognizer::AddEvent(const TEvt& evt)
{
	AddEventInternal(evt, Blob(nullptr, 0));
}

NodeProcessor::Recognizer::Recognizer(IHandler& h, Extra& extra)
	: m_Handler(h)
	, m_Extra(extra)
{

}

void NodeProcessor::Recognizer::RecognizeBlock(const TxVectors::Full& block, uint32_t shieldedOuts, bool validateShieldedOuts)
{
	assert(m_Handler.m_pAccount);
	const auto& acc = *m_Handler.m_pAccount;

	// recognize all
	for (size_t i = 0; i < block.m_vInputs.size(); i++)
		Recognize(*block.m_vInputs[i]);

	for (size_t i = 0; i < block.m_vOutputs.size(); i++)
		Recognize(*block.m_vOutputs[i], *acc.m_pOwner);

	if (!acc.m_vSh.empty())
	{
		KrnWalkerRecognize wlkKrn(*this);
		wlkKrn.m_Height = m_Pos.m_Height;

		TxoID nOuts = m_Extra.m_ShieldedOutputs;
		m_Extra.m_ShieldedOutputs -= shieldedOuts;

		wlkKrn.Process(block.m_vKernels);
		if (validateShieldedOuts)
		{
			assert(m_Extra.m_ShieldedOutputs == nOuts);
			nOuts; // supporess unused var warning in release
		}
	}
}

void NodeProcessor::Recognizer::Recognize(const Input& x)
{
	const EventKey::Utxo& key = x.m_Commitment;
	proto::Event::Utxo evt;

	std::vector<proto::Event::Utxo> vDups;
	if (!FindEvent(key, evt, &vDups))
		return;

	auto* pEvt = &evt;

	if (!vDups.empty())
	{
		// there're dups.
		assert(proto::Event::Flags::Add & evt.m_Flags); // the very 1st evt must be input

		for (uint32_t iOutp = 0; iOutp < vDups.size(); iOutp++)
		{
			const auto& evtOutp = vDups[iOutp];
			if (proto::Event::Flags::Add & evtOutp.m_Flags)
				continue;

			// this is output event. Pair it with the appropriate input event
			if ((proto::Event::Flags::Add & evt.m_Flags) && (evt.m_Maturity == evtOutp.m_Maturity))
				evt.m_Maturity = MaxHeight;
			else
			{
				for (uint32_t iInp = 0; ; iInp++)
				{
					assert(iInp < iOutp);
					auto& evtInp = vDups[iInp];

					if ((proto::Event::Flags::Add & evtInp.m_Flags) && (evtInp.m_Maturity == evtOutp.m_Maturity))
					{
						evtInp.m_Maturity = MaxHeight;
						break;
					}
				}
			}
		}

		// make sure we select the remaining input with minimum maturity
		for (uint32_t iInp = 0; iInp < vDups.size(); iInp++)
		{
			auto& evtInp = vDups[iInp];

			if ((proto::Event::Flags::Add & evtInp.m_Flags) && (evtInp.m_Maturity < pEvt->m_Maturity))
			{
				pEvt = &evtInp;
				break;
			}
		}

	}

	pEvt->m_Flags &= ~proto::Event::Flags::Add;
	AddEvent(*pEvt);
}

void NodeProcessor::Recognizer::Recognize(const TxKernelStd&, uint32_t)
{
}

void NodeProcessor::Recognizer::Recognize(const TxKernelShieldedInput& x, uint32_t)
{
	EventKey::Shielded key = x.m_SpendProof.m_SpendPk;
	key.m_Y |= EventKey::s_FlagShielded;

	proto::Event::Shielded evt;
	if (!FindEvent(key, evt))
		return;

	evt.m_Flags &= ~proto::Event::Flags::Add;

	AddEvent(evt);
}

void NodeProcessor::Recognizer::Recognize(const TxKernelContractCreate&, uint32_t)
{
}

void NodeProcessor::Recognizer::Recognize(const TxKernelContractInvoke&, uint32_t)
{
}

void NodeProcessor::Recognizer::Recognize(const TxKernelEvmInvoke&, uint32_t)
{
}

bool NodeProcessor::KrnWalkerShielded::OnKrn(const TxKernel& krn)
{
	switch (krn.get_Subtype())
	{
	case TxKernel::Subtype::ShieldedInput:
		return OnKrnEx(Cast::Up<TxKernelShieldedInput>(krn));
	case TxKernel::Subtype::ShieldedOutput:
		return OnKrnEx(Cast::Up<TxKernelShieldedOutput>(krn));

	default:
		break; // suppress warning
	}

	return true;
}

bool NodeProcessor::KrnWalkerRecognize::OnKrn(const TxKernel& krn)
{
	switch (krn.get_Subtype())
	{
#define THE_MACRO(id, name) \
	case TxKernel::Subtype::name: \
		m_Rec.Recognize(Cast::Up<TxKernel##name>(krn), m_nKrnIdx); \
		break;

	BeamKernelsAll(THE_MACRO)
#undef THE_MACRO

	default:
		break; // suppress warning
	}

	return true;
}

void NodeProcessor::Recognizer::Recognize(const TxKernelShieldedOutput& v, uint32_t)
{
	TxoID nID = m_Extra.m_ShieldedOutputs++;

	assert(m_Handler.m_pAccount);
	const auto& acc = *m_Handler.m_pAccount;

	for (Key::Index nIdx = 0; nIdx < acc.m_vSh.size(); nIdx++)
	{
		const ShieldedTxo& txo = v.m_Txo;

		ShieldedTxo::Data::Params pars;
		if (!pars.m_Ticket.Recover(txo.m_Ticket, acc.m_vSh[nIdx]))
			continue;

		ECC::Oracle oracle;
		oracle << v.get_Msg();

		if (!pars.m_Output.Recover(txo, pars.m_Ticket.m_SharedSecret, m_Pos.m_Height, oracle))
			continue;

		proto::Event::Shielded evt;
		evt.m_TxoID = nID;
		pars.ToID(evt.m_CoinID);
		evt.m_CoinID.m_Key.m_nIdx = nIdx;
		evt.m_Flags = proto::Event::Flags::Add;

		EventKey::Shielded key = pars.m_Ticket.m_SpendPk;
		key.m_Y |= EventKey::s_FlagShielded;

		AddEvent(evt, key);
		break;
	}
}

void NodeProcessor::Recognizer::Recognize(const Output& x, Key::IPKdf& keyViewer)
{
	CoinID cid;
	Output::User user;
	if (!x.Recover(m_Pos.m_Height, keyViewer, cid, &user))
		return;

	// filter-out dummies
	if (cid.IsDummy())
	{
		m_Handler.OnDummy(cid, m_Pos.m_Height);
		return;
	}

	// bingo!
	proto::Event::Utxo evt;
	evt.m_Flags = proto::Event::Flags::Add;
	evt.m_Cid = cid;
	evt.m_Commitment = x.m_Commitment;
	evt.m_Maturity = x.get_MinMaturity(m_Pos.m_Height);
	evt.m_User = user;

	const EventKey::Utxo& key = x.m_Commitment;
	AddEvent(evt, key);
}

void NodeProcessor::Recognizer::Recognize(const TxKernelAssetCreate& v, uint32_t nKrnIdx)
{
	assert(m_Handler.m_pAccount);
	const auto& acc = *m_Handler.m_pAccount;

	EventKey::AssetCtl key;
	v.m_MetaData.get_Owner(key, *acc.m_pOwner);
	if (key != v.m_Owner)
		return;

	// recognized!
	proto::Event::AssetCtl evt;

	evt.m_Flags = proto::Event::Flags::Add;
	evt.m_EmissionChange = 0; // no change upon creation

	NodeDB::AssetEvt wlk;
	m_Handler.AssetEvtsGetStrict(wlk, m_Pos.m_Height, nKrnIdx);
	assert(wlk.m_ID > Asset::s_MaxCount);

	evt.m_Info.m_ID = wlk.m_ID - Asset::s_MaxCount;
	evt.m_Info.m_LockHeight = m_Pos.m_Height;
	TemporarySwap<ByteBuffer> ts(Cast::NotConst(v).m_MetaData.m_Value, evt.m_Info.m_Metadata.m_Value);
	evt.m_Info.m_Owner = v.m_Owner;
	evt.m_Info.m_Value = Zero;
	evt.m_Info.m_Deposit = Rules::get().get_DepositForCA(m_Pos.m_Height);
	evt.m_Info.SetCid(nullptr);

	AddEvent(evt, key);
}

void NodeProcessor::AssetDataPacked::set_Strict(const Blob& blob)
{
	if (sizeof(*this) != blob.n)
		OnCorrupted();
	memcpy(this, blob.p, sizeof(*this));
}

void NodeProcessor::Recognizer::Recognize(const TxKernelAssetEmit& v, uint32_t nKrnIdx)
{
	proto::Event::AssetCtl evt;
	if (!FindEvent(v.m_Owner, evt))
		return;

	evt.m_Flags = 0;
	evt.m_EmissionChange = v.m_Value;

	NodeDB::AssetEvt wlk;
	m_Handler.AssetEvtsGetStrict(wlk, m_Pos.m_Height, nKrnIdx);
	assert(wlk.m_ID == evt.m_Info.m_ID);

	AssetDataPacked adp;
	adp.set_Strict(wlk.m_Body);

	evt.m_Info.m_Value = adp.m_Amount;
	adp.m_LockHeight.Export(evt.m_Info.m_LockHeight);
	evt.m_Info.m_Deposit = Rules::get().CA.DepositForList2; // not used anyway

	AddEvent(evt);
}

void NodeProcessor::Recognizer::Recognize(const TxKernelAssetDestroy& v, uint32_t nKrnIdx)
{
	proto::Event::AssetCtl evt;
	if (!FindEvent(v.m_Owner, evt))
		return;

	evt.m_Flags = proto::Event::Flags::Delete;
	evt.m_EmissionChange = 0;

	evt.m_Info.m_Value = Zero;
	evt.m_Info.m_Deposit = v.get_Deposit();

	AddEvent(evt);
}

bool NodeProcessor::HandleKernelType(const TxKernelContractCreate& krn, BlockInterpretCtx& bic)
{
	if (bic.m_Fwd)
	{
		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, krn.m_Data);
		bvm2::ContractID cid;
		bvm2::get_CidViaSid(cid, sid, krn.m_Args);

		auto& e = bic.get_ContractVar(cid, m_DB);
		if (!e.m_Data.empty())
		{
			bic.m_TxStatus = proto::TxStatus::ContractFailNode;

			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "Contract " << cid << " already exists";

			return false; // contract already exists
		}

		BlockInterpretCtx::BvmProcessor proc(bic, *this);

		Blob blob = krn.m_Data;
		proc.AddRemoveShader(cid, &blob);

		if (!proc.Invoke(cid, 0, krn))
			return false;

	}
	else
	{
		BlockInterpretCtx::VmProcessorBase proc(bic, *this);
		proc.UndoVars();
	}

	return true;
}

bool NodeProcessor::HandleKernelType(const TxKernelContractInvoke& krn, BlockInterpretCtx& bic)
{
	if (bic.m_Fwd)
	{
		if (!krn.m_iMethod)
		{
			bic.m_TxStatus = proto::TxStatus::ContractFailNode;

			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "Contract " << krn.m_Cid << " c'tor call attempt";

			return false; // c'tor call attempt
		}

		BlockInterpretCtx::BvmProcessor proc(bic, *this);
		if (!proc.Invoke(krn.m_Cid, krn.m_iMethod, krn))
			return false;

		if (1 == krn.m_iMethod)
		{
			// d'tor called. Make sure no variables are left except for the contract data
			proc.AddRemoveShader(krn.m_Cid, nullptr);

			if (!proc.EnsureNoVars(krn.m_Cid))
			{
				bic.m_TxStatus = proto::TxStatus::ContractFailNode;

				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << "Contract " << krn.m_Cid << " d'tor not fully clean";

				proc.UndoVars();
				return false;
			}
		}
	}
	else
	{
		BlockInterpretCtx::VmProcessorBase proc(bic, *this);
		proc.UndoVars();
	}

	return true;
}

bool NodeProcessor::HandleKernelType(const TxKernelEvmInvoke& krn, BlockInterpretCtx& bic)
{
	if (bic.m_Fwd)
	{
		BlockInterpretCtx::MyEvmProcessor proc(bic, *this);

		try
		{
			proc.InvokeGuarded(krn);
			return true;
		}
		catch (const std::exception& e)
		{
			bic.m_TxStatus = proto::TxStatus::ContractFailFirst;

			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << e.what();
		}
		
		proc.UndoVars();
		return false;
	}
	else
	{
		BlockInterpretCtx::VmProcessorBase proc(bic, *this);
		proc.UndoVars();
	}

	return true;
}


bool NodeProcessor::BlockInterpretCtx::BvmProcessor::EnsureNoVars(const bvm2::ContractID& cid)
{
	Blob key(cid);

	auto* pE = FindVarEx(key, true, true);
	return !(pE && IsOwnedVar(cid, pE->ToBlob()));
}

bool NodeProcessor::BlockInterpretCtx::BvmProcessor::IsOwnedVar(const bvm2::ContractID& cid, const Blob& key)
{
	return
		(key.n >= cid.nBytes) &&
		!memcmp(cid.m_pData, key.p, cid.nBytes);
}

void NodeProcessor::RescanAccounts(uint32_t nRecent)
{
	if (!nRecent)
		return;
	assert(nRecent <= m_vAccounts.size());

	MyRecognizer rec(*this);

	struct TxoRecover
		:public ITxoRecover
	{
		MyRecognizer& m_Rec;
		uint32_t m_Total = 0;
		uint32_t m_Unspent = 0;

		const Account* m_pAcc;
		uint32_t m_nAcc;

		TxoRecover(MyRecognizer& rec)
			:m_Rec(rec)
		{
		}

		bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp) override
		{
			for (uint32_t iAcc = 0; iAcc < m_nAcc; iAcc++)
			{
				m_Rec.m_Handler.m_pAccount = m_pAcc + iAcc;
				m_pKey = m_Rec.m_Handler.m_pAccount->m_pOwner.get();

				if (!ITxoRecover::OnTxo(wlk, hCreate, outp))
					return false;
			}

			return true;
		}

		bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp, const CoinID& cid, const Output::User& user) override
		{
			if (cid.IsDummy())
			{
				m_Rec.m_Handler.m_Proc.OnDummy(cid, hCreate);
				return true;
			}

			proto::Event::Utxo evt;
			evt.m_Flags = proto::Event::Flags::Add;
			evt.m_Cid = cid;
			evt.m_Commitment = outp.m_Commitment;
			evt.m_Maturity = outp.get_MinMaturity(hCreate);
			evt.m_User = user;

			m_Rec.m_Recognizer.m_Pos.m_Height = hCreate;
			// don't reset the Pos.Index. Its value is not important, it only should be monotonic
			const EventKey::Utxo& key = outp.m_Commitment;
			m_Rec.m_Recognizer.AddEvent(evt, key);

			m_Total++;

			if (MaxHeight == wlk.m_SpendHeight)
				m_Unspent++;
			else
			{
				evt.m_Flags = 0;
				m_Rec.m_Recognizer.m_Pos.m_Height = wlk.m_SpendHeight;
				m_Rec.m_Recognizer.AddEvent(evt);
			}

			return true;
		}
	};

	{
		LongAction la("Rescanning owned Txos...", 0, m_pExternalHandler);

		TxoRecover wlk(rec);
		wlk.m_pLa = &la;
		wlk.m_pAcc = &m_vAccounts.front() + m_vAccounts.size() - nRecent;
		wlk.m_nAcc = nRecent;

		EnumTxos(wlk);

		BEAM_LOG_INFO() << "Recovered " << wlk.m_Unspent << "/" << wlk.m_Total << " unspent/total Txos";
	}

	// shielded items
	Height h0 = Rules::get().pForks[2].m_Height;
	if (m_Cursor.m_hh.m_Height >= h0)
	{
		TxoID nOuts = m_Extra.m_ShieldedOutputs;
		m_Extra.m_ShieldedOutputs = 0;

		LongAction la("Rescanning shielded Txos...", 0, m_pExternalHandler);

		struct MyKrnWalker
			:public KrnWalkerRecognize
		{
			using KrnWalkerRecognize::KrnWalkerRecognize;

			const Account* m_pAcc;
			uint32_t m_nAcc;

			bool ProcessBlock(const NodeDB::StateID& sid, const std::vector<TxKernel::Ptr>& v) override
			{
				TxoID nOuts = m_Rec.m_Extra.m_ShieldedOutputs;
				m_Rec.m_Pos.m_Height = m_Height;

				for (uint32_t iAcc = 0; iAcc < m_nAcc; iAcc++)
				{
					m_Rec.m_Extra.m_ShieldedOutputs = nOuts;

					m_Rec.m_Handler.m_pAccount = m_pAcc + iAcc;

					if (!KrnWalkerRecognize::ProcessBlock(sid, v))
						return false;
				}

				return true;
			}

		};

		MyKrnWalker wlkKrn(rec.m_Recognizer);
		wlkKrn.m_pAcc = &m_vAccounts.front() + m_vAccounts.size() - nRecent;
		wlkKrn.m_nAcc = nRecent;

		Block::NumberRange nr;
		nr.m_Min = FindAtivePastHeight(h0);
		nr.m_Max = m_Cursor.m_Full.m_Number;

		wlkKrn.m_pLa = &la;
		EnumKernels(wlkKrn, nr);

		assert(m_Extra.m_ShieldedOutputs == nOuts);
		nOuts; // suppress unused var warning in release
	}
}

Height NodeProcessor::FindVisibleKernel(const Merkle::Hash& id, const BlockInterpretCtx& bic)
{
	assert(!bic.m_AlreadyValidated);

	if (bic.m_Temporary)
	{
		auto it = bic.m_KrnIDs.find(id);
		if (bic.m_KrnIDs.end() != it)
			return bic.m_Height;
	}

	Height h = m_DB.FindKernel(id);
	if (h >= Rules::HeightGenesis)
	{
		assert(h <= bic.m_Height);

		const Rules& r = Rules::get();
		if (r.IsPastFork_<2>(bic.m_Height) && (bic.m_Height - h > r.MaxKernelValidityDH))
			return 0; // Starting from Fork2 - visibility horizon is limited
	}

	return h;
}


bool NodeProcessor::HandleKernelType(const TxKernelStd& krn, BlockInterpretCtx& bic)
{
	if (bic.m_Fwd && krn.m_pRelativeLock && !bic.m_AlreadyValidated)
	{
		const TxKernelStd::RelativeLock& x = *krn.m_pRelativeLock;

		Height h0 = FindVisibleKernel(x.m_ID, bic);
		if (h0 < Rules::HeightGenesis)
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "RelLock not found " << x.m_ID;

			return false;
		}

		HeightAdd(h0, x.m_LockHeight);
		if (h0 > bic.m_Height)
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "RelLock too early " << x.m_ID << ", dH=" << x.m_LockHeight;

			return false;
		}
	}

	return true;
}

bool NodeProcessor::InternalAssetAdd(Asset::Full& ai, bool bMmr)
{
	ai.m_Value = Zero;
	if (!m_DB.AssetAdd(ai))
		return false;

	assert(ai.m_ID);

	if (bMmr)
	{
		uint32_t nCount = ai.m_ID - Rules::get().CA.ForeignEnd;

		if (m_Mmr.m_Assets.m_Count < nCount)
			m_Mmr.m_Assets.ResizeTo(nCount);

		Merkle::Hash hv;
		ai.get_Hash(hv);
		m_Mmr.m_Assets.Replace(nCount - 1, hv);
	}

	return true;
}

void NodeProcessor::InternalAssetDel(Asset::ID nAssetID, bool bMmr)
{
	Asset::ID aidMax = m_DB.AssetDelete(nAssetID);

	if (bMmr)
	{
		Asset::ID aid0 = Rules::get().CA.ForeignEnd;
		uint32_t nCount = aidMax - aid0;
		assert(nCount <= m_Mmr.m_Assets.m_Count);

		if (nCount < m_Mmr.m_Assets.m_Count)
			m_Mmr.m_Assets.ResizeTo(nCount);
		else
		{
			assert(nAssetID < aidMax);
			m_Mmr.m_Assets.Replace(nAssetID - (aid0 + 1), Zero);
		}
	}
}

bool NodeProcessor::HandleKernelType(const TxKernelAssetCreate& krn, BlockInterpretCtx& bic)
{
	Asset::ID aid;
	Amount valDeposit;
	return HandleAssetCreate(krn.m_Owner, nullptr, krn.m_MetaData, bic, aid, valDeposit);
}

bool NodeProcessor::HandleAssetCreate(const PeerID& pidOwner, const ContractID* pCid, const Asset::Metadata& md, BlockInterpretCtx& bic, Asset::ID& aid, Amount& valDeposit, uint32_t nSubIdx)
{
	if (bic.m_Fwd)
	{
		if (!bic.m_AlreadyValidated && m_DB.AssetFindByOwner(pidOwner))
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "duplicate asset owner: " << pidOwner;
			return false;
		}

		Asset::Full ai;
		ai.m_ID = 0; // auto
		ai.m_Owner = pidOwner;
		ai.m_LockHeight = bic.m_Height;
		ai.m_Deposit = valDeposit = Rules::get().get_DepositForCA(bic.m_Height);
		ai.SetCid(pCid);
		ai.m_Metadata.m_Hash = md.m_Hash;

		{
			TemporarySwap<ByteBuffer> ts(Cast::NotConst(md).m_Value, ai.m_Metadata.m_Value);

			if (!InternalAssetAdd(ai, !bic.m_SkipDefinition))
			{
				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << "assets overflow";
				return false;
			}
		}

		BlockInterpretCtx::Ser ser(bic);
		ser & ai.m_ID;

		if (!bic.m_Temporary)
		{
			NodeDB::AssetEvt evt;
			evt.m_ID = ai.m_ID + Asset::s_MaxCount;

			ByteBuffer bufBlob;
			bufBlob.resize(sizeof(AssetCreateInfoPacked) + md.m_Value.size());
			auto* pAcip = reinterpret_cast<AssetCreateInfoPacked*>(&bufBlob.front());

			pAcip->m_OwnedByContract = !!pCid;
			Cast::Down<ECC::uintBig>(pAcip->m_Owner) = pAcip->m_OwnedByContract ? (*pCid) : Cast::Down<ECC::uintBig>(pidOwner);
			if (!md.m_Value.empty())
				memcpy(pAcip + 1, &md.m_Value.front(), md.m_Value.size());

			evt.m_Body = bufBlob;

			bic.AssetEvtInsert(m_DB, evt, nSubIdx);
		}

		aid = ai.m_ID;
	}
	else
	{
		BlockInterpretCtx::Der der(bic);

		der & aid;

		InternalAssetDel(aid, !bic.m_SkipDefinition);
	}

	return true;
}

bool NodeProcessor::HandleKernelType(const TxKernelAssetDestroy& krn, BlockInterpretCtx& bic)
{
	Amount valDeposit = krn.get_Deposit();
	if (!HandleAssetDestroy(krn.m_Owner, nullptr, bic, krn.m_AssetID, valDeposit, true))
		return false;

	return true;
}

bool NodeProcessor::HandleAssetDestroy(const PeerID& pidOwner, const ContractID* pCid, BlockInterpretCtx& bic, Asset::ID aid, Amount& valDeposit, bool bDepositCheck, uint32_t nSubIdx)
{
	if (HandleAssetDestroy2(pidOwner, pCid, bic, aid, valDeposit, bDepositCheck, nSubIdx))
		return true;

	if (bic.m_pTxErrorInfo)
		*bic.m_pTxErrorInfo << ", AssetID=" << aid;

	return false;
}

bool NodeProcessor::HandleAssetDestroy2(const PeerID& pidOwner, const ContractID* pCid, BlockInterpretCtx& bic, Asset::ID aid, Amount& valDeposit, bool bDepositCheck, uint32_t nSubIdx)
{
	if (bic.m_Fwd)
	{
		Asset::Full ai;
		ai.m_ID = aid;
		if (!m_DB.AssetGetSafe(ai))
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "not found";
			return false;
		}

		if (!bic.m_AlreadyValidated)
		{
			if (ai.m_Owner != pidOwner)
			{
				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << "Not owned";
				return false;
			}

			if (ai.m_Value != Zero)
			{
				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << "Value=" << AmountBig::Printable(ai.m_Value);
				return false;
			}

			if (ai.m_LockHeight + Rules::get().CA.LockPeriod > bic.m_Height)
			{
				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << "LockHeight=" << ai.m_LockHeight;

				return false;
			}

			if (bDepositCheck && (valDeposit != ai.m_Deposit))
			{
				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << "Deposit expected=" << ai.m_Deposit << ", actual=" << valDeposit;
				return false;
			}
		}

		// looks good
		InternalAssetDel(aid, !bic.m_SkipDefinition);

		BlockInterpretCtx::Ser ser(bic);
		ser
			& ai.m_Metadata
			& ai.m_LockHeight;

		if (Rules::get().IsPastFork_<5>(bic.m_Height))
			ser & ai.m_Deposit;
		else
			assert(Rules::get().CA.DepositForList2 == ai.m_Deposit);

		valDeposit = ai.m_Deposit;

		if (!bic.m_Temporary)
		{
			NodeDB::AssetEvt evt;
			evt.m_ID = aid + Asset::s_MaxCount;
			ZeroObject(evt.m_Body);
			bic.AssetEvtInsert(m_DB, evt, nSubIdx);
		}
	}
	else
	{
		Asset::Full ai;
		ai.m_ID = aid;
		ai.SetCid(pCid);

		BlockInterpretCtx::Der der(bic);
		der
			& ai.m_Metadata
			& ai.m_LockHeight;

		if (pCid)
			ai.m_Metadata.get_Owner(ai.m_Owner, *pCid);
		else
			ai.m_Owner = pidOwner;

		if (Rules::get().IsPastFork_<5>(bic.m_Height))
			der & ai.m_Deposit;
		else
			ai.m_Deposit = Rules::get().CA.DepositForList2;

		if (!InternalAssetAdd(ai, !bic.m_SkipDefinition))
			OnCorrupted();

		if (ai.m_ID != aid)
			OnCorrupted();
	}

	return true;
}

bool NodeProcessor::HandleKernelType(const TxKernelAssetEmit& krn, BlockInterpretCtx& bic)
{
	return HandleAssetEmit(krn.m_Owner, bic, krn.m_AssetID, krn.m_Value);
}

bool NodeProcessor::HandleAssetEmit(const PeerID& pidOwner, BlockInterpretCtx& bic, Asset::ID aid, AmountSigned val, uint32_t nSubIdx)
{
	bool bRes = Rules::get().CA.IsForeign(aid) ?
		HandleAssetEmitForeign(pidOwner, bic, aid, val, nSubIdx) :
		HandleAssetEmitLocal(pidOwner, bic, aid, val, nSubIdx);

	if (bRes)
		return true;

	if (bic.m_pTxErrorInfo)
		*bic.m_pTxErrorInfo << ", AssetID=" << aid;

	return false;
}

bool NodeProcessor::HandleAssetEmitLocal(const PeerID& pidOwner, BlockInterpretCtx& bic, Asset::ID aid, AmountSigned val, uint32_t nSubIdx)
{
	Asset::Full ai;
	ai.m_ID = aid;
	if (!m_DB.AssetGetSafe(ai))
	{
		if (bic.m_pTxErrorInfo)
			*bic.m_pTxErrorInfo << "not found";
		return false;
	}

	bool bAdd;
	Amount valUns = SplitAmountSigned(val, bAdd);

	if (bic.m_Fwd)
	{
		if (!bic.m_AlreadyValidated && (ai.m_Owner != pidOwner))
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "not owned";
			return false; // as well
		}
	}
	else
		bAdd = !bAdd;

	auto val0 = ai.m_Value.ToNumber();
	AmountBig::Number valDelta = valUns;

	bool bWasZero = (ai.m_Value == Zero);

	if (bAdd)
	{
		val0 += valDelta;
		if (val0 < valDelta)
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "too large";

			return false; // overflow (?!)
		}
	}
	else
	{
		if (val0 < valDelta)
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "too low";

			return false; // not enough to burn
		}

		val0 -= valDelta;
	}

	ai.m_Value.FromNumber(val0);

	bool bZero = (val0 == Zero);
	if (bZero != bWasZero)
	{
		if (bic.m_Fwd)
		{
			BlockInterpretCtx::Ser ser(bic);
			ser & ai.m_LockHeight;

			ai.m_LockHeight = bic.m_Height;
		}
		else
		{
			BlockInterpretCtx::Der der(bic);
			der & ai.m_LockHeight;
		}
	}

	m_DB.AssetSetValue(ai.m_ID, ai.m_Value, ai.m_LockHeight);

	if (!bic.m_SkipDefinition)
	{
		Merkle::Hash hv;
		ai.get_Hash(hv);

		m_Mmr.m_Assets.Replace(ai.m_ID - (Rules::get().CA.ForeignEnd + 1), hv);
	}

	if (bic.m_Fwd && !bic.m_Temporary)
	{
		AssetDataPacked adp;
		adp.m_Amount = ai.m_Value;
		adp.m_LockHeight = ai.m_LockHeight;

		NodeDB::AssetEvt evt;
		evt.m_ID = aid;
		evt.m_Body.p = &adp;
		evt.m_Body.n = sizeof(adp);

		bic.AssetEvtInsert(m_DB, evt, nSubIdx);
	}

	return true;
}

bool NodeProcessor::BridgeAddInfo(const PeerID& pidOwner, const HeightPos& pos, Asset::ID aid, Amount val)
{
	ForeignDetailsPacked fdp;
	fdp.m_Aid = aid;
	fdp.m_Amount = val;
	Blob blobVal(&fdp, sizeof(fdp));

	return m_DB.BridgeInsertSafe(pos, pidOwner, &blobVal);
}

bool NodeProcessor::FindExternalAssetEmit(const PeerID& pidOwner, bool bEmit, ForeignDetailsPacked& fdp)
{
	ECC::Point key;
	key.m_X = Cast::Down<ECC::uintBig>(pidOwner);
	key.m_Y = 4;

	if (!bEmit)
		key.m_Y |= 8;

	NodeDB::Recordset rs;
	Blob blob(&key, sizeof(key));
	if (!m_DB.UniqueFind(blob, rs))
		return false;

	const auto& fep = rs.get_As<ForeignEmitPacked>(0);
	fdp = fep.m_Details;

	return true;
}

bool NodeProcessor::HandleAssetEmitForeign(const PeerID& pidOwner, BlockInterpretCtx& bic, Asset::ID aid, AmountSigned val, uint32_t nSubIdx)
{
	ECC::Point key;
	key.m_X = Cast::Down<ECC::uintBig>(pidOwner);
	key.m_Y = 4;

	bool bAdd;
	Amount valUns = SplitAmountSigned(val, bAdd);

	if (!bAdd)
		key.m_Y |= 8;
	Blob blobKey(&key, sizeof(key));

	ForeignEmitPacked fep;
	fep.m_Details.m_Aid = aid;
	fep.m_Details.m_Amount = valUns;

	if (bic.m_Fwd && !bic.m_AlreadyValidated && bAdd)
	{
		NodeDB::Recordset rs;
		Blob blobDetails;

		m_DB.BridgeFind(pidOwner, blobDetails, rs);
		if (sizeof(ForeignDetailsPacked) != blobDetails.n)
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "bridge no key";
			return false;
		}

		const auto& fdp = *reinterpret_cast<const ForeignDetailsPacked*>(blobDetails.p);
		if ((fdp.m_Aid != fep.m_Details.m_Aid) || (fdp.m_Amount != fep.m_Details.m_Amount))
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "bridge details mismatch";
			return false;
		}
	}

	fep.m_Height = bic.m_Height;
	fep.m_nIdx = bic.m_nKrnIdx;

	Blob blobVal(&fep, sizeof(fep));

	if (!ValidateUniqueNoDup(bic, blobKey, &blobVal))
	{
		if (bic.m_pTxErrorInfo)
			*bic.m_pTxErrorInfo << "double-emit";
		return false;
	}

	if (bic.m_Fwd && !bic.m_Temporary)
	{
		// register the emission in evts
		AssetDataPacked adp;
		adp.m_LockHeight = Zero;
		adp.m_Amount = valUns;

		if (bAdd)
			adp.m_Amount.Negate();

		{
			NodeDB::WalkerAssetEvt wlk;
			m_DB.AssetEvtsEnumBwd(wlk, aid, bic.m_Height);
			if (wlk.MoveNext())
			{
				AssetDataPacked adp0;
				adp0.set_Strict(wlk.m_Body);
				adp.m_Amount += adp0.m_Amount;
			}
		}

		NodeDB::AssetEvt evt;
		evt.m_ID = aid;
		evt.m_Body.p = &adp;
		evt.m_Body.n = sizeof(adp);

		bic.AssetEvtInsert(m_DB, evt, 0); // will include height+krnIdx
	}

	return true;
}

bool NodeProcessor::HandleKernelType(const TxKernelShieldedOutput& krn, BlockInterpretCtx& bic)
{
	const ECC::Point& key = krn.m_Txo.m_Ticket.m_SerialPub;
	Blob blobKey(&key, sizeof(key));

	if (bic.m_Fwd)
	{
		if (!bic.m_AlreadyValidated)
		{
			if (bic.m_ShieldedOuts >= Rules::get().Shielded.MaxOuts)
			{
				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << "Shielded outp limit exceeded";

				bic.m_LimitExceeded = true;
				return false;
			}

			if (!bic.ValidateAssetRange(krn.m_Txo.m_pAsset))
				return false;
		}

		ShieldedOutpPacked sop;
		sop.m_Height = bic.m_Height;
		sop.m_MmrIndex = m_Mmr.m_Shielded.m_Count;
		sop.m_TxoID = m_Extra.m_ShieldedOutputs;
		sop.m_Commitment = krn.m_Txo.m_Commitment;

		Blob blobVal(&sop, sizeof(sop));

		if (!ValidateUniqueNoDup(bic, blobKey, &blobVal))
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "Shielded outp duplicate";

			return false;
		}

		if (!bic.m_Temporary)
		{
			ECC::Point::Native pt, pt2;
			pt.Import(krn.m_Txo.m_Commitment); // don't care if Import fails (kernels are not necessarily tested at this stage)
			pt2.Import(krn.m_Txo.m_Ticket.m_SerialPub);
			pt += pt2;

			ECC::Point::Storage pt_s;
			pt.Export(pt_s);

			// Append to cmList
			m_DB.ShieldedResize(m_Extra.m_ShieldedOutputs + 1, m_Extra.m_ShieldedOutputs);
			m_DB.ShieldedWrite(m_Extra.m_ShieldedOutputs, &pt_s, 1);

			// Append state hash
			ECC::Hash::Value hvState;
			if (m_Extra.m_ShieldedOutputs)
				m_DB.ShieldedStateRead(m_Extra.m_ShieldedOutputs - 1, &hvState, 1);
			else
				hvState = Zero;

			ShieldedTxo::UpdateState(hvState, pt_s);

			m_DB.ShieldedStateResize(m_Extra.m_ShieldedOutputs + 1, m_Extra.m_ShieldedOutputs);
			m_DB.ShieldedStateWrite(m_Extra.m_ShieldedOutputs, &hvState, 1);
		}

		if (!bic.m_SkipDefinition)
		{
			ShieldedTxo::DescriptionOutp d;
			d.m_SerialPub = krn.m_Txo.m_Ticket.m_SerialPub;
			d.m_Commitment = krn.m_Txo.m_Commitment;
			d.m_ID = m_Extra.m_ShieldedOutputs;
			d.m_Height = bic.m_Height;

			Merkle::Hash hv;
			d.get_Hash(hv);
			m_Mmr.m_Shielded.Append(hv);
		}

		m_Extra.m_ShieldedOutputs++;

		bic.m_ShieldedOuts++; // ok

	}
	else
	{
		ValidateUniqueNoDup(bic, blobKey, nullptr);

		if (!bic.m_Temporary)
		{
			m_DB.ShieldedResize(m_Extra.m_ShieldedOutputs - 1, m_Extra.m_ShieldedOutputs);
			m_DB.ShieldedStateResize(m_Extra.m_ShieldedOutputs - 1, m_Extra.m_ShieldedOutputs);
		}

		if (!bic.m_SkipDefinition)
			m_Mmr.m_Shielded.ShrinkTo(m_Mmr.m_Shielded.m_Count - 1);
		

		assert(bic.m_ShieldedOuts);
		bic.m_ShieldedOuts--;

		assert(m_Extra.m_ShieldedOutputs);
		m_Extra.m_ShieldedOutputs--;
	}

	return true;
}

bool NodeProcessor::HandleKernelType(const TxKernelShieldedInput& krn, BlockInterpretCtx& bic)
{
	ECC::Point key = krn.m_SpendProof.m_SpendPk;
	key.m_Y |= 2;
	Blob blobKey(&key, sizeof(key));

	if (bic.m_Fwd)
	{
		if (!bic.m_AlreadyValidated)
		{
			if (!bic.ValidateAssetRange(krn.m_pAsset))
				return false;

			if (bic.m_ShieldedIns >= Rules::get().Shielded.MaxIns)
			{
				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << "Shielded inp limit exceeded";

				bic.m_LimitExceeded = true;
				return false;
			}

			if (!IsShieldedInPool(krn))
			{
				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << "Shielded inp oob";

				return false; // references invalid pool window
			}
		}
 
		ShieldedInpPacked sip;
		sip.m_Height = bic.m_Height;
		sip.m_MmrIndex = m_Mmr.m_Shielded.m_Count;

		Blob blobVal(&sip, sizeof(sip));
		if (!ValidateUniqueNoDup(bic, blobKey, &blobVal))
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "Shielded inp duplicate";
			return false;
		}

		if (!bic.m_SkipDefinition)
		{
			ShieldedTxo::DescriptionInp d;
			d.m_SpendPk = krn.m_SpendProof.m_SpendPk;
			d.m_Height = bic.m_Height;

			Merkle::Hash hv;
			d.get_Hash(hv);
			m_Mmr.m_Shielded.Append(hv);
		}

		bic.m_ShieldedIns++; // ok

	}
	else
	{
		ValidateUniqueNoDup(bic, blobKey, nullptr);

		if (!bic.m_SkipDefinition)
			m_Mmr.m_Shielded.ShrinkTo(m_Mmr.m_Shielded.m_Count - 1);

		assert(bic.m_ShieldedIns);
		bic.m_ShieldedIns--;
	}

	return true;
}

template <typename T>
bool NodeProcessor::HandleElementVecFwd(const T& vec, BlockInterpretCtx& bic, size_t& n)
{
	assert(bic.m_Fwd);

	for (; n < vec.size(); n++)
		if (!HandleBlockElement(*vec[n], bic))
			return false;

	return true;
}

template <typename T>
void NodeProcessor::HandleElementVecBwd(const T& vec, BlockInterpretCtx& bic, size_t n)
{
	assert(!bic.m_Fwd);

	while (n--)
		if (!HandleBlockElement(*vec[n], bic))
			OnCorrupted();
}

bool NodeProcessor::HandleValidatedTx(const TxVectors::Full& txv, BlockInterpretCtx& bic)
{
	size_t pN[3];

	bool bOk = true;
	if (bic.m_Fwd)
	{
		bic.m_vInpAux.reserve(bic.m_vInpAux.size() + txv.m_vInputs.size());

		ZeroObject(pN);
		bOk =
			HandleElementVecFwd(txv.m_vInputs, bic, pN[0]) &&
			HandleElementVecFwd(txv.m_vOutputs, bic, pN[1]) &&
			HandleElementVecFwd(txv.m_vKernels, bic, pN[2]);

		if (bOk)
			return true;

		bic.m_Fwd = false; // rollback partial changes
	}
	else
	{
		// rollback all
		pN[0] = txv.m_vInputs.size();
		pN[1] = txv.m_vOutputs.size();
		pN[2] = txv.m_vKernels.size();
	}

	HandleElementVecBwd(txv.m_vKernels, bic, pN[2]);
	HandleElementVecBwd(txv.m_vOutputs, bic, pN[1]);
	HandleElementVecBwd(txv.m_vInputs, bic, pN[0]);

	if (!bOk)
		bic.m_Fwd = true; // restore it to prevent confuse

	return bOk;
}

bool NodeProcessor::HandleValidatedBlock(const Block::Body& block, BlockInterpretCtx& bic)
{
	// make sure we adjust txo count, to prevent the same Txos for consecutive blocks after cut-through
	if (!bic.m_Fwd)
	{
		assert(m_Extra.m_Txos);
		m_Extra.m_Txos--;
	}

	if (!HandleValidatedTx(block, bic))
		return false;

	// currently there's no extra info in the block that's needed

	if (bic.m_Fwd)
		m_Extra.m_Txos++;

	return true;
}

struct NodeProcessor::DependentContextSwitch
{
	typedef std::vector<const TxPool::Dependent::Element*> Vec;

	NodeProcessor& m_This;
	BlockInterpretCtx& m_Bic;
	Vec m_vec;
	uint32_t m_Applied;

	DependentContextSwitch(NodeProcessor& np, BlockInterpretCtx& bic)
		:m_This(np)
		,m_Bic(bic)
		,m_Applied(0)
	{
	}

	~DependentContextSwitch()
	{
		m_Bic.m_Fwd = false;
		while (m_Applied)
			m_This.HandleValidatedTx(*m_vec[--m_Applied]->m_pValue, m_Bic);
	}

	static void Convert(Vec& vec, const TxPool::Dependent::Element* pTop)
	{
		uint32_t n = 0;
		for (auto p = pTop; p; p = p->m_pParent)
			n++;

		vec.resize(n);
		for (auto p = pTop; p; p = p->m_pParent)
			vec[--n] = p;
	}

	bool Apply(const TxPool::Dependent::Element* pTop) 
	{
		Convert(m_vec, pTop);

		assert(m_Bic.m_Fwd && !m_Applied);

		for (; m_Applied < m_vec.size(); m_Applied++)
			if (!m_This.HandleValidatedTx(*m_vec[m_Applied]->m_pValue, m_Bic))
				return false;

		return true;
	}
};

bool NodeProcessor::ExecInDependentContext(IWorker& wrk, const Merkle::Hash* pCtx, const TxPool::Dependent& txp)
{
	if (!pCtx)
		wrk.Do();
	else
	{
		if (m_Cursor.m_Full.m_Prev == *pCtx)
			wrk.Do();
		else
		{
			auto itCtx = txp.m_setContexts.find(*pCtx, TxPool::Dependent::Element::Context::Comparator());
			if (txp.m_setContexts.end() == itCtx)
				return false;

			BlockInterpretCtx bic(m_Cursor.m_hh.m_Height + 1, true);
			bic.SetAidMax(*this);
			bic.m_Temporary = true;
			bic.m_TxValidation = true;
			bic.m_SkipDefinition = true;
			bic.m_AlreadyValidated = true;
			bic.m_SkipInOuts = true;

			DependentContextSwitch dcs(*this, bic);
			if (!dcs.Apply(&itCtx->get_ParentObj()))
				return false;

			wrk.Do();
		}
	}

	return true;
}


bool NodeProcessor::HandleBlockElement(const Input& v, BlockInterpretCtx& bic)
{
	if (bic.m_SkipInOuts)
		return true;

	if (bic.m_Fwd)
	{
		struct Traveler :public UtxoTree::ITraveler {
			virtual bool OnLeaf(const RadixTree::Leaf& x) override {
				return false; // stop iteration
			}
		} t;


		UtxoTree::Key kMin, kMax;

		UtxoTree::Key::Data d;
		d.m_Commitment = v.m_Commitment;
		d.m_Maturity = Rules::HeightGenesis - 1;
		kMin = d;
		d.m_Maturity = bic.m_Height - 1;
		kMax = d;

		UtxoTree::Cursor cu;
		t.m_pCu = &cu;
		t.m_pBound[0] = kMin.V.m_pData;
		t.m_pBound[1] = kMax.V.m_pData;

		if (m_Mapped.m_Utxo.Traverse(t))
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "not found Input " << v.m_Commitment;
			return false;
		}

		UtxoTree::MyLeaf* p = &Cast::Up<UtxoTree::MyLeaf>(cu.get_Leaf());

		d = p->m_Key;
		assert(d.m_Commitment == v.m_Commitment);
		assert(d.m_Maturity < bic.m_Height);

		TxoID nID = p->m_ID;

		if (!p->IsExt())
			m_Mapped.m_Utxo.Delete(cu);
		else
		{
			nID = m_Mapped.m_Utxo.PopID(*p);
			cu.InvalidateElement();
			m_Mapped.m_Utxo.OnDirty();
		}

		auto& aux = bic.m_vInpAux.emplace_back();
		aux.m_Maturity = d.m_Maturity;
		aux.m_ID = nID;

	} else
	{
		assert(!bic.m_vInpAux.empty());
		auto aux = std::move(bic.m_vInpAux.back());
		bic.m_vInpAux.pop_back();

		UndoInput(v, aux);
	}

	return true;
}

void NodeProcessor::UndoInput(const Input& v, const InputAux& aux)
{
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;
	d.m_Maturity = aux.m_Maturity;

	bool bCreate = true;
	UtxoTree::Key key;
	key = d;

	m_Mapped.m_Utxo.EnsureReserve();

	UtxoTree::Cursor cu;
	UtxoTree::MyLeaf* p = m_Mapped.m_Utxo.Find(cu, key, bCreate);

	if (bCreate)
		p->m_ID = aux.m_ID;
	else
	{
		m_Mapped.m_Utxo.PushID(aux.m_ID, *p);
		cu.InvalidateElement();
		m_Mapped.m_Utxo.OnDirty();
	}

}

bool NodeProcessor::HandleBlockElement(const Output& v, BlockInterpretCtx& bic)
{
	if (bic.m_SkipInOuts)
		return true;

	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;
	d.m_Maturity = bic.m_SkipDefinition ?
		(bic.m_Height - 1) : // allow this output to be spent in exactly this block. Won't happen in normal blocks (matching in/outs are not allowed), but ok when assembling a block, before cut-through
		v.get_MinMaturity(bic.m_Height);

	UtxoTree::Key key;
	key = d;

	m_Mapped.m_Utxo.EnsureReserve();

	UtxoTree::Cursor cu;
	bool bCreate = true;
	UtxoTree::MyLeaf* p = m_Mapped.m_Utxo.Find(cu, key, bCreate);

	cu.InvalidateElement();
	m_Mapped.m_Utxo.OnDirty();

	if (bic.m_Fwd)
	{
		if (!bic.ValidateAssetRange(v.m_pAsset))
			return false;

		TxoID nID = m_Extra.m_Txos;

		if (bCreate)
			p->m_ID = nID;
		else
		{
			// protect again overflow attacks, though it's highly unlikely (Input::Count is currently limited to 32 bits, it'd take millions of blocks)
			Input::Count nCountInc = p->get_Count() + 1;
			if (!nCountInc)
			{
				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << "overflow output " << v.m_Commitment;
				return false;
			}

			m_Mapped.m_Utxo.PushID(nID, *p);
		}

		m_Extra.m_Txos++;

	} else
	{
		assert(m_Extra.m_Txos);
		m_Extra.m_Txos--;

		if (!p->IsExt())
			m_Mapped.m_Utxo.Delete(cu);
		else
			m_Mapped.m_Utxo.PopID(*p);
	}

	return true;
}

void NodeProcessor::ManageKrnID(BlockInterpretCtx& bic, const TxKernel& krn)
{
	if (bic.m_Height < Rules::HeightGenesis)
		return; // for historical reasons treasury kernels are ignored

	const auto& key = krn.get_ID();

	if (bic.m_Temporary)
	{
		if (bic.m_AlreadyValidated)
			return;

		if (bic.m_Fwd)
			bic.m_KrnIDs.insert(key);
		else
		{
			auto it = bic.m_KrnIDs.find(key);
			assert(bic.m_KrnIDs.end() != it);
			bic.m_KrnIDs.erase(it);
		}
	}
	else
	{
		if (bic.m_Fwd)
			m_DB.InsertKernel(key, bic.m_Height);
		else
			m_DB.DeleteKernel(key, bic.m_Height);
	}

}

bool NodeProcessor::HandleBlockElement(const TxKernel& v, BlockInterpretCtx& bic)
{
	const Rules& r = Rules::get();
	if (bic.m_Fwd && r.IsPastFork_<2>(bic.m_Height) && !bic.m_AlreadyValidated)
	{
		Height hPrev = FindVisibleKernel(v.get_ID(), bic);
		if (hPrev >= Rules::HeightGenesis)
		{
			if (bic.m_pTxErrorInfo)
				*bic.m_pTxErrorInfo << "Kernel ID=" << v.get_ID() << " duplicated at " << hPrev;

			return false; // duplicated
		}
	}

	if (!bic.m_Fwd)
		ManageKrnID(bic, v);

	if (!HandleKernel(v, bic))
	{
		if (bic.m_pTxErrorInfo)
			*bic.m_pTxErrorInfo << " <- Kernel ID=" << v.get_ID();

		if (!bic.m_Fwd)
			OnCorrupted();
		return false;
	}

	if (bic.m_Fwd)
		ManageKrnID(bic, v);

	return true;
}

bool NodeProcessor::HandleKernel(const TxKernel& v, BlockInterpretCtx& bic)
{
	size_t n = 0;
	bool bOk = true;

	if (bic.m_Fwd)
	{
		// nested
		for (; n < v.m_vNested.size(); n++)
		{
			if (!HandleKernel(*v.m_vNested[n], bic))
			{
				if (bic.m_pTxErrorInfo)
					*bic.m_pTxErrorInfo << " <- Nested Kernel " << n;

				bOk = false;
				break;
			}
		}
	}
	else
	{
		n = v.m_vNested.size();

		assert(bic.m_nKrnIdx);
		bic.m_nKrnIdx--;
	}

	if (bOk)
	{
		bOk = HandleKernelTypeAny(v, bic);

		if (!bOk && bic.m_pTxErrorInfo)
			*bic.m_pTxErrorInfo << " <- Kernel Type " << (uint32_t) v.get_Subtype();
	}

	if (bOk)
	{
		if (bic.m_Fwd)
			bic.m_nKrnIdx++;

		bool bHandleFee = v.m_Fee && !bic.m_TxValidation && !bic.m_SkipDefinition && !bic.m_DontUpdatePbft && (Rules::Consensus::Pbft == Rules::get().m_Consensus);
		if (bHandleFee)
		{
			if (bic.m_Fwd)
				m_PbftState.m_Totals.m_Revenue += v.m_Fee;
			else
				m_PbftState.m_Totals.m_Revenue -= v.m_Fee;

			m_PbftState.m_Hash.m_Valid = false;
		}

	}
	else
	{
		if (!bic.m_Fwd)
			OnCorrupted();
		bic.m_Fwd = false;
	}

	if (!bic.m_Fwd)
	{
		// nested
		while (n--)
			if (!HandleKernel(*v.m_vNested[n], bic))
				OnCorrupted();
	}

	if (!bOk)
		bic.m_Fwd = true; // restore it back

	return bOk;
}

bool NodeProcessor::HandleKernelTypeAny(const TxKernel& krn, BlockInterpretCtx& bic)
{
	auto eType = krn.get_Subtype();

	bool bContextChange = !krn.m_CanEmbed && (TxKernel::Subtype::Std != eType);
	if (bContextChange && !bic.m_Fwd && bic.m_Temporary)
	{
		BlockInterpretCtx::Der der(bic);
		der & bic.m_hvDependentCtx;
	}

	switch (eType)
	{
#define THE_MACRO(id, name) \
	case TxKernel::Subtype::name: \
		if (!HandleKernelType(Cast::Up<TxKernel##name>(krn), bic)) \
			return false; \
		break; 

	BeamKernelsAll(THE_MACRO)
#undef THE_MACRO

	default:
		assert(false); // should not happen!
	}

	if (bContextChange && bic.m_Fwd)
	{
		const auto& hvPrev = bic.m_DependentCtxSet ? bic.m_hvDependentCtx : m_Cursor.m_Full.m_Prev;
		if (bic.m_Temporary)
		{
			BlockInterpretCtx::Ser ser(bic);
			ser & hvPrev;
		}

		DependentContext::get_Ancestor(bic.m_hvDependentCtx, hvPrev, krn.get_ID());
		bic.m_DependentCtxSet = true;
	}

	return true;
}

bool NodeProcessor::IsShieldedInPool(const Transaction& tx)
{
	struct Walker
		:public TxKernel::IWalker
	{
		NodeProcessor* m_pThis;
		virtual bool OnKrn(const TxKernel& krn) override
		{
			if (krn.get_Subtype() != TxKernel::Subtype::ShieldedInput)
				return true;

			return m_pThis->IsShieldedInPool(Cast::Up<TxKernelShieldedInput>(krn));
		}
	} wlk;
	wlk.m_pThis = this;

	return wlk.Process(tx.m_vKernels);
}

bool NodeProcessor::IsShieldedInPool(const TxKernelShieldedInput& krn)
{
	const Rules& r = Rules::get();
	if (!r.Shielded.Enabled)
		return false;

	if (krn.m_WindowEnd > m_Extra.m_ShieldedOutputs)
		return false;

	if (!(krn.m_SpendProof.m_Cfg == r.Shielded.m_ProofMin))
	{
		if (!(krn.m_SpendProof.m_Cfg == r.Shielded.m_ProofMax))
			return false; // cfg not allowed

		if (m_Extra.m_ShieldedOutputs > krn.m_WindowEnd + r.Shielded.MaxWindowBacklog)
			return false; // large anonymity set is no more allowed, expired
	}

	return true;
}

void NodeProcessor::BlockInterpretCtx::AddKrnInfo(Serializer& ser, NodeDB& db)
{
	assert(m_pvC);
	auto& vC = *m_pvC;

	for (uint32_t i = 0; i < vC.size(); i++)
	{
		const auto& info = vC[i];

		NodeDB::KrnInfo::Entry x;
		x.m_Pos.m_Height = m_Height;
		x.m_Pos.m_Pos = i + 1;
		x.m_Cid = info.m_Cid;

		ser.reset();
		ser & info;

		SerializeBuffer sb = ser.buffer();
		x.m_Val = Blob(sb.first, static_cast<uint32_t>(sb.second));

		db.KrnInfoInsert(x);
	}
}

void NodeProcessor::BlockInterpretCtx::AssetEvtInsert(NodeDB& db, NodeDB::AssetEvt& evt, uint32_t nSubIdx)
{
	evt.m_Height = m_Height;
	evt.m_Index = get_AssetEvtIdx(m_nKrnIdx, nSubIdx);
	db.AssetEvtsInsert(evt);
}

NodeProcessor::BlockInterpretCtx::Ser::Ser(BlockInterpretCtx& bic)
	:m_This(bic)
{
	m_Pos = bic.m_Rollback.size();
	swap_buf(bic.m_Rollback);
}

NodeProcessor::BlockInterpretCtx::Ser::~Ser()
{
	if (!std::uncaught_exceptions())
	{
		Marker mk = static_cast<uint32_t>(buffer().second - m_Pos);
		*this & mk;
	}
	swap_buf(m_This.m_Rollback);
}

NodeProcessor::BlockInterpretCtx::Der::Der(BlockInterpretCtx& bic)
{
	ByteBuffer& buf = bic.m_Rollback; // alias

	Ser::Marker mk;
	SetBwd(buf, mk.nBytes);
	*this & mk;

	uint32_t n;
	mk.Export(n);
	SetBwd(buf, n);
}

void NodeProcessor::BlockInterpretCtx::Der::SetBwd(ByteBuffer& buf, uint32_t nPortion)
{
	if (buf.size() < nPortion)
		OnCorrupted();

	size_t nVal = buf.size() - nPortion;
	reset(&buf.front() + nVal, nPortion);

	if (nVal < buf.size()) // to avoid stringop-overflow warning
		buf.resize(nVal); // it's safe to call resize() while the buffer is being used, coz std::vector does NOT reallocate on shrink
}

bool NodeProcessor::ValidateUniqueNoDup(BlockInterpretCtx& bic, const Blob& key, const Blob* pVal)
{
	if (bic.m_Temporary)
	{
		if (bic.m_AlreadyValidated)
			return true;

		auto* pE = bic.m_Dups.Find(key);
		if (bic.m_Fwd)
		{
			if (pE)
				return false; // duplicated

			NodeDB::Recordset rs;
			if (m_DB.UniqueFind(key, rs))
				return false; // duplicated

			pE = bic.m_Dups.Create(key);
		}
		else
		{
			assert(pE);
			bic.m_Dups.Delete(*pE);
		}
	}
	else
	{
		if (bic.m_Fwd)
		{
			if (!m_DB.UniqueInsertSafe(key, pVal))
				return false; // duplicated
		}
		else
			m_DB.UniqueDeleteStrict(key);
	}

	return true;
}

NodeProcessor::BlockInterpretCtx::VmProcessorBase::VmProcessorBase(BlockInterpretCtx& bic, NodeProcessor& proc)
	:m_Bic(bic)
	,m_Proc(proc)
{
	if (bic.m_Fwd)
	{
		BlockInterpretCtx::Ser ser(bic);

		RecoveryTag::Type n = RecoveryTag::Terminator;
		ser & n;
	}
}

bool NodeProcessor::BlockInterpretCtx::BvmProcessor::Invoke(const bvm2::ContractID& cid, uint32_t iMethod, const TxKernelContractControl& krn)
{
	bool bRes = false;
	try
	{
		m_Charge = m_Bic.m_ChargePerBlock;

		if (m_Bic.m_pTxErrorInfo)
			m_FarCalls.m_SaveLocal = true;

		InitStackPlus(m_Stack.AlignUp(static_cast<uint32_t>(krn.m_Args.size())));
		m_Stack.PushAlias(krn.m_Args);

		m_Instruction.m_Mode = 
			IsPastFork_<4>() ?
			Wasm::Reader::Mode::Standard :
			m_Bic.m_TxValidation ?
				Wasm::Reader::Mode::Restrict :
				Wasm::Reader::Mode::Emulate_x86;

		ECC::Hash::Processor hp;
		FundsChangeMap fundsIO;

		if (!m_Bic.m_AlreadyValidated)
		{
			const auto& hvCtx = m_Bic.m_DependentCtxSet ? m_Bic.m_hvDependentCtx : m_Proc.m_Cursor.m_Full.m_Prev;
			krn.Prepare(hp, &hvCtx);

			m_pSigValidate = &hp;
			m_pFundsIO = &fundsIO;
		}

		if (m_Bic.m_pvC)
			m_pFundsIO = &fundsIO;

		CallFar(cid, iMethod, m_Stack.get_AlasSp(), (uint32_t)krn.m_Args.size(), 0);

		while (!IsDone())
		{
			DischargeUnits(bvm2::Limits::Cost::Cycle);
			RunOnce();
		}

		if (!m_Bic.m_AlreadyValidated)
			CheckSigs(krn.m_Commitment, krn.m_Signature);

		bRes = true;

		UpdChargeWithRecovery(m_Charge);

	}
	catch (const Exc& e)
	{
		uint32_t n = e.m_Type + proto::TxStatus::ContractFailFirst;
		m_Bic.m_TxStatus = (n < proto::TxStatus::ContractFailLast) ? static_cast<uint8_t>(n) : proto::TxStatus::ContractFailFirst;

		if (e.m_Type == bvm2::ErrorSubType::NoCharge)
			m_Bic.m_LimitExceeded = true;

		if (m_Bic.m_pTxErrorInfo)
			*m_Bic.m_pTxErrorInfo << e.what();
	}
	catch (const std::exception& e)
	{
		m_Bic.m_TxStatus = proto::TxStatus::ContractFailFirst;

		if (m_Bic.m_pTxErrorInfo)
			*m_Bic.m_pTxErrorInfo << e.what();
	}

	if (!bRes)
	{
		if (m_Bic.m_pTxErrorInfo)
		{
			DumpCallstack(*m_Bic.m_pTxErrorInfo);
			*m_Bic.m_pTxErrorInfo << " <- cid=" << cid << " method=" << iMethod;
		}

		UndoVars();
	}

	if (m_Instruction.m_ModeTriggered) {
		BEAM_LOG_WARNING() << " Potential wasm conflict";
	}

	return bRes;
}

void NodeProcessor::BlockInterpretCtx::MyEvmProcessor::InvokeGuarded(const TxKernelEvmInvoke& krn)
{
	const auto& r = Rules::get();
	if (!r.Evm.Groth2Wei || !r.Evm.BaseGasPrice)
		Exc::Fail("Evm disabled");

	auto& acc = LoadAccountInternal(krn.m_From);
	if (acc.m_pContractCode)
		Exc::Fail("not EOA");
	if (acc.m_Nonce != krn.m_Nonce)
		Exc::Fail("nonce gap");

	m_Top.m_pAccount = &acc;
	m_Top.EnsureAccountCreated(acc);

	bool bAdd;
	Amount valUns = SplitAmountSigned(krn.m_Subsidy, bAdd);
	if (valUns)
	{
		Evm::Word wSubsidy;
		wSubsidy.FromNumber(MultiWord::From(valUns) * MultiWord::From(r.Evm.Groth2Wei)); // won't overflow

		if (bAdd)
		{
			wSubsidy += acc.m_Balance.m_Value;
			if (wSubsidy < acc.m_Balance.m_Value)
				Exc::Fail("balance overflow");

		}
		else
		{
			if (wSubsidy > acc.m_Balance.m_Value)
				Exc::Fail("balance underflow");

			wSubsidy.Negate();
			wSubsidy += acc.m_Balance.m_Value;
		}

		m_Top.UpdateBalance(acc, wSubsidy);
	}

	// gas parameters. Gas is payed from the tx kernel fee, not sender balance
	auto nGasLimit = m_Bic.m_ChargePerBlock;

	auto numGas = (MultiWord::From(krn.m_Fee) * MultiWord::From(r.Evm.Groth2Wei)) / MultiWord::From(r.Evm.BaseGasPrice);

	if (numGas.get_ConstSlice().get_clz_Words() >= numGas.nWords - MultiWord::NumberForType<uint32_t>::Type::nWords)
	{
		numGas.get_Element(nGasLimit);
		std::setmin(nGasLimit, m_Bic.m_ChargePerBlock);
	}

	if (nGasLimit < r.Evm.MinTxGasUnits)
		Exc::Fail("gas below min");

	m_Top.m_Gas = nGasLimit - r.Evm.MinTxGasUnits;

	Args args;
	args.m_Buf = krn.m_Args;
	args.m_CallValue = krn.m_CallValue;

	Call(krn.m_To, args, krn.m_Nonce);

	while (!m_lstFrames.empty())
		RunOnce();

	acc.m_Nonce++;
	acc.m_Balance.m_Modified++; // to ensure it'll be saved

	// everything is fine. Commit the changes, and create recovery info
	uint32_t nGasConsumed = nGasLimit - static_cast<uint32_t>(m_Top.m_Gas);
	assert(nGasConsumed <= m_Bic.m_ChargePerBlock);

	UpdChargeWithRecovery(m_Bic.m_ChargePerBlock - nGasConsumed);

	CommitChanges();
}

void NodeProcessor::BlockInterpretCtx::MyEvmProcessor::CommitChanges()
{
	for (auto& ad_ : m_Accounts)
	{
		auto& ad = Cast::Up<AccountWrap>(ad_);
		assert(ad.m_pEntry);

		bool bChanged =
			ad.m_Balance.m_Modified ||
			ad.m_Code.m_Modified ||
			ad.m_Exists.m_Modified;

		bool bIsContract = ad.m_pContractCode || ad.m_Code.m_Modified;

		if (bChanged)
		{
			if (ad.m_Exists.m_Value)
			{
				if (bIsContract)
				{
					EvmAccount::Contract ca;
					ca.m_Balance_Wei = ad.m_Balance.m_Value;

					if (ad.m_Code.m_Modified)
					{
						if (ad.m_pContractCode)
							ContractDataSaveWithRecovery(*ad.m_pContractCode, Blob(nullptr, 0));

						HashOf(ca.m_CodeHash, ad.m_Code.m_Value);

						AccountKey ak;
						ak.m_Addr = ad.m_Key;
						ak.m_Var = ca.m_CodeHash;

						auto& e = m_Bic.get_ContractVar(Blob(&ak, sizeof(ak)), m_Proc.m_DB);
						ContractDataSaveWithRecovery(e, ad.m_Code.m_Value);
					}
					else
					{
						// use previous value
						if (ad.m_pEntry)
							ca.m_CodeHash = reinterpret_cast<EvmAccount::Contract*>(&ad.m_pEntry->m_Data.front())->m_CodeHash;
						else
							ca.m_CodeHash = Zero;
					}

					ContractDataSaveWithRecovery(*ad.m_pEntry, Blob(&ca, sizeof(ca)));
				}
				else
				{
					EvmAccount::User ua;
					ua.m_Balance_Wei = ad.m_Balance.m_Value;
					ua.m_Nonce = ad.m_Nonce;

					ContractDataSaveWithRecovery(*ad.m_pEntry, Blob(&ua, sizeof(ua)));
				}

			}
			else
			{
				// account was erased
				ContractDataSaveWithRecovery(*ad.m_pEntry, Blob(nullptr, 0));

				if (ad.m_pContractCode)
					ContractDataSaveWithRecovery(*ad.m_pContractCode, Blob(nullptr, 0));
			}
		}

		// vars
		for (auto& sd_ : ad.m_Slots)
		{
			auto& sd = Cast::Up<SlotWrap>(sd_);
			assert(sd.m_pEntry);

			if (sd.m_Data.m_Modified)
			{
				Blob newVal = sd.m_Data.m_Value;
				if (sd.m_Data.m_Value == Zero)
					newVal.n = 0;

				ContractDataSaveWithRecovery(*sd.m_pEntry, newVal);
			}
		}
		
	}

}

Evm::Processor::Account& NodeProcessor::BlockInterpretCtx::MyEvmProcessor::LoadAccount(const Evm::Address& addr)
{
	return LoadAccountInternal(addr);
}

NodeProcessor::BlockInterpretCtx::MyEvmProcessor::AccountWrap& NodeProcessor::BlockInterpretCtx::MyEvmProcessor::LoadAccountInternal(const Evm::Address& addr)
{
	AccountWrap* p = new AccountWrap;
	p->m_Key = addr;
	m_Accounts.insert(*p);

	auto& e = m_Bic.get_ContractVar(addr, m_Proc.m_DB);
	p->m_pEntry = &e;

	p->m_pContractCode = nullptr;
	p->m_Nonce = 0;
	p->m_Code.m_Value.n = 0;

	if (e.m_Data.empty())
	{
		p->m_Balance.m_Value = Zero;
		p->m_Exists.m_Value = false;
	}
	else
	{
		static_assert(sizeof(EvmAccount::User) != sizeof(EvmAccount::Contract));

		p->m_Exists.m_Value = true;

		if (e.m_Data.size() == sizeof(EvmAccount::User))
		{
			const auto& x = *reinterpret_cast<EvmAccount::User*>(&e.m_Data.front());
			p->m_Balance.m_Value = x.m_Balance_Wei;
			x.m_Nonce.Export(p->m_Nonce);
		}
		else
		{
			if (e.m_Data.size() != sizeof(EvmAccount::Contract))
				OnCorrupted();
			const auto& x = *reinterpret_cast<EvmAccount::Contract*>(&e.m_Data.front());
			p->m_Balance.m_Value = x.m_Balance_Wei;

			AccountKey ak;
			ak.m_Addr = addr;
			ak.m_Var = x.m_CodeHash;

			p->m_pContractCode = &m_Bic.get_ContractVar(Blob(&ak, sizeof(ak)), m_Proc.m_DB);

			p->m_Code.m_Value = p->m_pContractCode->m_Data;
		}
	}

	return *p;
}

Evm::Processor::Account::Slot& NodeProcessor::BlockInterpretCtx::MyEvmProcessor::LoadSlot(Evm::Processor::Account& acc, const Evm::Word& key)
{
	SlotWrap* p = new SlotWrap;
	p->m_Key = key;
	acc.m_Slots.insert(*p);

	AccountKey ak;
	ak.m_Addr = acc.m_Key;
	ak.m_Var = key;

	auto& e = m_Bic.get_ContractVar(Blob(&ak, sizeof(ak)), m_Proc.m_DB);
	p->m_pEntry = &e;

	if (e.m_Data.empty())
		p->m_Data.m_Value = Zero;
	else
	{
		if (sizeof(Evm::Word) != e.m_Data.size())
			OnCorrupted();

		p->m_Data.m_Value = *reinterpret_cast<Evm::Word*>(&e.m_Data.front());
	}

	return *p;
}

Height NodeProcessor::BlockInterpretCtx::MyEvmProcessor::get_Height()
{
	return m_Bic.m_Height - 1;
}

bool NodeProcessor::BlockInterpretCtx::MyEvmProcessor::get_BlockHeader(BlockHeader& res, Height h)
{
	Block::SystemState::Full s;
	if (!m_Proc.get_HdrAt(s, h))
		return false;

	s.get_Hash(res.m_Hash);
	s.m_PoW.m_Difficulty.Unpack(res.m_Difficulty);
	res.m_GasLimit = Zero;
	Cast::Down<Evm::Address::Base>(res.m_Coinbase) = Zero;
	res.m_Timestamp = s.m_TimeStamp;

	return true;
}

struct NodeProcessor::ProcessorInfoParser
	:public bvm2::ProcessorManager
{
	NodeProcessor& m_Proc;

	Height m_Height;
	ByteBuffer m_bufParser;
	std::ostringstream m_os;

	void SelectContext(bool /* bDependent */, uint32_t /* nChargeNeeded */) override {
		m_Context.m_Height = m_Height;
	}
	bool get_HdrAt(Block::SystemState::Full& s, Height h) override
	{
		if (h > m_Height)
			return false;
		return m_Proc.get_HdrAt(s, h);
	}

	void VarsEnum(const Blob& kMin, const Blob& kMax, IReadVars::Ptr& pOut) override
	{
		struct Context
			:public IReadVars
		{
			NodeDB::WalkerContractData m_Wlk;
			ByteBuffer m_Buf1, m_Buf2;

			bool MoveNext() override
			{
				if (!m_Wlk.MoveNext())
					return false;

				m_LastKey = m_Wlk.m_Key;
				m_LastVal = m_Wlk.m_Val;
				return true;
			}
		};

		pOut = std::make_unique<Context>();
		auto& x = Cast::Up<Context>(*pOut);

		kMin.Export(x.m_Buf1);
		kMax.Export(x.m_Buf2);

		m_Proc.m_DB.ContractDataEnum(x.m_Wlk, x.m_Buf1, x.m_Buf2);
	}

	void LogsEnum(const Blob& kMin, const Blob& kMax, const HeightPos* pPosMin, const HeightPos* pPosMax, IReadLogs::Ptr& pOut) override
	{
		struct Context
			:public IReadLogs
		{
			NodeDB::ContractLog::Walker m_Wlk;
			ByteBuffer m_Buf1, m_Buf2;

			bool MoveNext() override
			{
				if (!m_Wlk.MoveNext())
					return false;

				m_LastKey = m_Wlk.m_Entry.m_Key;
				m_LastVal = m_Wlk.m_Entry.m_Val;
				m_LastPos = m_Wlk.m_Entry.m_Pos;
				return true;
			}
		};

		pOut = std::make_unique<Context>();
		auto& x = Cast::Up<Context>(*pOut);

		HeightPos hpMin, hpMax;
		if (!pPosMin)
		{
			pPosMin = &hpMin;
			hpMin = HeightPos(0);
		}

		if (!pPosMax)
		{
			pPosMax = &hpMax;
			hpMax = HeightPos(MaxHeight);
		}

		if (kMin.n && kMax.n)
		{
			kMin.Export(x.m_Buf1);
			kMax.Export(x.m_Buf2);
			m_Proc.m_DB.ContractLogEnum(x.m_Wlk, x.m_Buf1, x.m_Buf2, *pPosMin, *pPosMax);
		}
		else
			m_Proc.m_DB.ContractLogEnum(x.m_Wlk, *pPosMin, *pPosMax);
	}
/*
	bool VarGetProof(Blob& key, ByteBuffer& val, beam::Merkle::Proof&) override {
		return false;
	}

	bool LogGetProof(const HeightPos&, beam::Merkle::Proof&) override {
		return false;
	}

	bool get_SpecialParam(const char*, Blob&) override {
		return false;
	}
*/
	ProcessorInfoParser(NodeProcessor& p)
		:m_Proc(p)
	{
		m_Height = p.m_Cursor.m_hh.m_Height;
	}

	bool Init(uint32_t nStackBytesExtra)
	{
		m_Proc.m_DB.ParamGet(NodeDB::ParamID::RichContractParser, nullptr, nullptr, &m_bufParser);
		if (m_bufParser.empty())
			return false;

		InitMem(nStackBytesExtra);
		m_Code = m_bufParser;

		m_pOut = &m_os;
		return true;
	}

	std::string Execute()
	{
		while (!IsDone())
			RunOnce();

		auto ret = m_os.str();
		if (2 == ret.size())
			ret.clear(); // remove empty group
		return ret;
	}

	template <typename T>
	Wasm::Word PushArgAlias(const T& arg)
	{
		m_Stack.AliasAlloc(sizeof(T));
		*(T*) m_Stack.get_AliasPtr() = arg;
		return m_Stack.get_AlasSp();
	}

	template <typename T>
	void PushArgBoth(const T& arg)
	{
		Wasm::Word val = PushArgAlias(arg);
		m_Stack.Push(val);
	}
};


void NodeProcessor::BlockInterpretCtx::BvmProcessor::ParseExtraInfo(ContractInvokeExtraInfo& x, const bvm2::ShaderID& sid, uint32_t iMethod, const Blob& args)
{
	try
	{
		ProcessorInfoParser proc(m_Proc);
		proc.m_Height = m_Bic.m_Height - 1;

		if (!proc.Init(m_Stack.AlignUp(args.n)))
			return;

		proc.m_Stack.PushAlias(args);
		Wasm::Word pArgs_ = proc.m_Stack.get_AlasSp();

		proc.PushArgBoth(sid);
		proc.PushArgBoth(x.m_Cid);
		proc.m_Stack.Push(iMethod);
		proc.m_Stack.Push(pArgs_);
		proc.m_Stack.Push(args.n);

		proc.CallMethod(0);

		x.m_sParsed = proc.Execute();
	}
	catch (const std::exception& e)
	{
		BEAM_LOG_WARNING() << "contract parser error: " << e.what();
	}
}

void NodeProcessor::get_ContractDescr(const ECC::uintBig& sid, const ECC::uintBig& cid, std::string& res, bool bFullState)
{
	try
	{
		ProcessorInfoParser proc(*this);
		if (!proc.Init(0))
			return;

		proc.PushArgBoth(sid);
		proc.PushArgBoth(cid);

		proc.CallMethod(bFullState ? 2 : 1);

		res = proc.Execute();
	}
	catch (const std::exception& e)
	{
		BEAM_LOG_WARNING() << "contract parser error: " << e.what();
	}
}

BlobMap::Entry& NodeProcessor::BlockInterpretCtx::get_ContractVar(const Blob& key, NodeDB& db)
{
	auto* pE = m_ContractVars.Find(key);
	if (!pE)
	{
		pE = m_ContractVars.Create(key);

		Blob data;
		NodeDB::Recordset rs;
		if (db.ContractDataFind(key, data, rs))
			data.Export(pE->m_Data);
	}
	return *pE;
}

void NodeProcessor::BlockInterpretCtx::BvmProcessor::LoadVar(const Blob& key, Blob& res)
{
	auto& e = m_Bic.get_ContractVar(key, m_Proc.m_DB);
	res = e.m_Data;
}

BlobMap::Entry* NodeProcessor::BlockInterpretCtx::BvmProcessor::FindVarEx(const Blob& key, bool bExact, bool bBigger)
{
	auto* pE = &m_Bic.get_ContractVar(key, m_Proc.m_DB);
	if (pE->m_Data.empty() || !bExact)
	{
		while (true)
		{
			NodeDB::Recordset rs;
			Blob keyDB = pE->ToBlob();
			bool bNextDB = bBigger ?
				m_Proc.m_DB.ContractDataFindNext(keyDB, rs) :
				m_Proc.m_DB.ContractDataFindPrev(keyDB, rs);

			if (bNextDB)
				m_Bic.get_ContractVar(keyDB, m_Proc.m_DB);

			auto it = BlobMap::Set::s_iterator_to(*pE);
			if (bBigger)
			{
				++it;
				if (m_Bic.m_ContractVars.end() == it)
					return nullptr;
			}
			else
			{
				if (m_Bic.m_ContractVars.begin() == it)
					return nullptr;
				--it;
			}

			pE = &(*it);
			if (!pE->m_Data.empty())
				break;
		}
	}
	return pE;
}

void NodeProcessor::BlockInterpretCtx::BvmProcessor::LoadVarEx(Blob& key, Blob& res, bool bExact, bool bBigger)
{
	auto* pE = FindVarEx(key, bExact, bBigger);
	if (pE)
	{
		key = pE->ToBlob();
		res = pE->m_Data;
	}
	else
	{
		key.n = 0;
		res.n = 0;
	}
}

uint32_t NodeProcessor::BlockInterpretCtx::BvmProcessor::SaveVar(const Blob& key, const Blob& data)
{
	auto& e = m_Bic.get_ContractVar(key, m_Proc.m_DB);
	auto nOldSize = static_cast<uint32_t>(e.m_Data.size());

	ContractDataSaveWithRecovery(e, data);

	return nOldSize;
}

void NodeProcessor::BlockInterpretCtx::VmProcessorBase::ContractDataSaveWithRecovery(BlobMap::Entry& e, const Blob& data)
{
	if (Blob(e.m_Data) != data)
	{
		auto key = e.ToBlob();

		RecoveryTag::Type nTag = RecoveryTag::Insert;

		if (data.n)
		{
			if (e.m_Data.size())
			{
				nTag = RecoveryTag::Update;
				ContractDataUpdate(key, data, e.m_Data);
			}
			else
			{
				nTag = RecoveryTag::Delete;
				ContractDataInsert(key, data);
			}
		}
		else
		{
			assert(e.m_Data.size());
			ContractDataDel(key, e.m_Data);
		}

		BlockInterpretCtx::Ser ser(m_Bic);
		ser& nTag;
		ser& key.n;
		ser.WriteRaw(key.p, key.n);
		if (e.m_Data.size())
			ser & e.m_Data;

		data.Export(e.m_Data);
	}
}

void NodeProcessor::BlockInterpretCtx::VmProcessorBase::UpdChargeWithRecovery(uint32_t val)
{
	if (m_Bic.m_Temporary)
	{
		BlockInterpretCtx::Ser ser(m_Bic);
		RecoveryTag::Type nTag = RecoveryTag::Recharge;
		ser & nTag;
		ser & m_Bic.m_ChargePerBlock;
	}

	m_Bic.m_ChargePerBlock = val;

}


void NodeProcessor::BlockInterpretCtx::VmProcessorBase::ContractDataInsert(const Blob& key, const Blob& data)
{
	ContractDataToggleTree(key, data, true);
	if (!m_Bic.m_Temporary)
		m_Proc.m_DB.ContractDataInsert(key, data);
}

void NodeProcessor::BlockInterpretCtx::VmProcessorBase::ContractDataUpdate(const Blob& key, const Blob& val, const Blob& valOld)
{
	ContractDataToggleTree(key, val, true);
	ContractDataToggleTree(key, valOld, false);
	if (!m_Bic.m_Temporary)
		m_Proc.m_DB.ContractDataUpdate(key, val);
}

void NodeProcessor::BlockInterpretCtx::VmProcessorBase::ContractDataDel(const Blob& key, const Blob& valOld)
{
	ContractDataToggleTree(key, valOld, false);
	if (!m_Bic.m_Temporary)
		m_Proc.m_DB.ContractDataDel(key);
}

bool NodeProcessor::Mapped::Contract::IsStored(const Blob& key)
{
	if (key.n > bvm2::ContractID::nBytes)
	{
		uint8_t nTag = reinterpret_cast<const uint8_t*>(key.p)[bvm2::ContractID::nBytes];
		if (Shaders::KeyTag::InternalStealth == nTag)
			return false;
	}

	return true;
}

void NodeProcessor::Mapped::Contract::Toggle(const Blob& key, const Blob& data, bool bAdd)
{
	if (!IsStored(key))
		return;

	Merkle::Hash hv;
	Block::get_HashContractVar(hv, key, data);

	if (bAdd)
		EnsureReserve();

	bool bCreate = true;
	RadixHashOnlyTree::Cursor cu;
	Find(cu, hv, bCreate);

	if (!bAdd)
		Delete(cu);

	if (bAdd != bCreate)
	{
		Exc::CheckpointTxt cp("SaveVar collision");

		if (!bAdd)
			OnCorrupted();

		Exc::Fail();
	}
}

void NodeProcessor::BlockInterpretCtx::VmProcessorBase::ContractDataToggleTree(const Blob& key, const Blob& data, bool bAdd)
{
	if (!m_Bic.m_SkipDefinition)
		m_Proc.m_Mapped.m_Contract.Toggle(key, data, bAdd);
}

uint32_t NodeProcessor::BlockInterpretCtx::BvmProcessor::OnLog(const Blob& key, const Blob& val)
{
	assert(m_Bic.m_Fwd);
	if (!m_Bic.m_Temporary)
	{

		NodeDB::ContractLog::Entry x;
		x.m_Pos.m_Height = m_Bic.m_Height;
		x.m_Pos.m_Pos = m_Bic.m_ContractLogs;
		x.m_Key = key;
		x.m_Val = val;
		m_Proc.m_DB.ContractLogInsert(x);
	}

	BlockInterpretCtx::Ser ser(m_Bic);
	RecoveryTag::Type nTag = RecoveryTag::Log;
	ser & nTag;
	ser & m_Bic.m_ContractLogs;

	if (!m_Bic.m_SkipDefinition)
	{
		bool bMmr = IsContractVarStoredInMmr(key);
		ser & bMmr;

		if (bMmr)
			Block::get_HashContractLog(m_Bic.m_vLogs.emplace_back(), key, val, m_Bic.m_ContractLogs);
	}

	return m_Bic.m_ContractLogs++;
}

bool NodeProcessor::BlockInterpretCtx::BvmProcessor::get_AssetInfo(Asset::Full& ai)
{
	return m_Proc.get_DB().AssetGetSafe(ai);
}

Height NodeProcessor::BlockInterpretCtx::BvmProcessor::get_Height()
{
	return m_Bic.m_Height - 1;
}

bool NodeProcessor::BlockInterpretCtx::BvmProcessor::get_HdrAt(Block::SystemState::Full& s, Height h)
{
	if (h > m_Bic.m_Height - 1)
		return false;

	return m_Proc.get_HdrAt(s, h);
}

bool NodeProcessor::get_HdrAt(Block::SystemState::Full& s, Height h)
{
	assert(h <= m_Cursor.m_hh.m_Height); // must be checked earlier

	if (h == m_Cursor.m_hh.m_Height)
		s = m_Cursor.m_Full;
	else
	{
		if (h < Rules::HeightGenesis)
			return false;

		NodeDB::StateID sid;
		FindActiveAtStrict(sid, h);
		m_DB.get_State(sid.m_Row, s);
	}

	return true;
}

Asset::ID NodeProcessor::BlockInterpretCtx::BvmProcessor::AssetCreate(const Asset::Metadata& md, const PeerID& pidOwner, Amount& valDeposit)
{
	Asset::ID aid = 0;
	if (!m_Proc.HandleAssetCreate(pidOwner, &m_FarCalls.m_Stack.back().m_Cid, md, m_Bic, aid, valDeposit, m_AssetEvtSubIdx))
		return 0;

	BlockInterpretCtx::Ser ser(m_Bic);
	RecoveryTag::Type nTag = RecoveryTag::AssetCreate;
	ser & nTag;

	m_AssetEvtSubIdx++;

	assert(aid);
	return aid;
}

bool NodeProcessor::BlockInterpretCtx::BvmProcessor::AssetEmit(Asset::ID aid, const PeerID& pidOwner, AmountSigned val)
{
	if (!m_Proc.HandleAssetEmit(pidOwner, m_Bic, aid, val, m_AssetEvtSubIdx))
		return false;

	if (m_Bic.m_pvC)
	{
		auto& vec = *m_Bic.m_pvC; // alias

		assert(m_iCurrentInvokeExtraInfo <= vec.size());
		auto& x = vec[m_iCurrentInvokeExtraInfo - 1];

		bool bAdd;
		auto valUns = SplitAmountSigned(val, bAdd);
		x.m_Emission.Add(valUns, aid, bAdd);
	}

	BlockInterpretCtx::Ser ser(m_Bic);
	RecoveryTag::Type nTag = RecoveryTag::AssetEmit;
	ser & nTag;
	ser & aid;
	ser & val;

	if (Rules::get().CA.IsForeign(aid))
		ser & pidOwner;

	m_AssetEvtSubIdx++;
	return true;
}

bool NodeProcessor::BlockInterpretCtx::BvmProcessor::AssetDestroy(Asset::ID aid, const PeerID& pidOwner, Amount& valDeposit)
{
	const ContractID& cid = m_FarCalls.m_Stack.back().m_Cid;

	if (!m_Proc.HandleAssetDestroy(pidOwner, &cid, m_Bic, aid, valDeposit, false, m_AssetEvtSubIdx))
		return false;

	BlockInterpretCtx::Ser ser(m_Bic);
	RecoveryTag::Type nTag = RecoveryTag::AssetDestroy;
	ser & nTag;
	ser & aid;
	ser & cid;

	m_AssetEvtSubIdx++;
	return true;
}

void NodeProcessor::BlockInterpretCtx::VmProcessorBase::UndoVars()
{
	ByteBuffer key;
	for (RecoveryTag::Type nTag = 0; ; )
	{
		BlockInterpretCtx::Der der(m_Bic);
		der & nTag;

		switch (nTag)
		{
		case RecoveryTag::Terminator:
			return;

		case RecoveryTag::AssetCreate:
			{
				bool bFwd = false;
				TemporarySwap swp(bFwd, m_Bic.m_Fwd);

				Asset::ID aid = 0;
				PeerID pidOwner;
				Asset::Metadata md;
				Amount valDeposit;

				if (!m_Proc.HandleAssetCreate(pidOwner, nullptr, md, m_Bic, aid, valDeposit))
					return OnCorrupted();
			}
			break;

		case RecoveryTag::AssetEmit:
		{
			bool bFwd = false;
			TemporarySwap swp(bFwd, m_Bic.m_Fwd);

			Asset::ID aid = 0;
			PeerID pidOwner;
			AmountSigned val;
			der & aid;
			der & val;

			if (Rules::get().CA.IsForeign(aid))
				der & pidOwner;

			if (!m_Proc.HandleAssetEmit(pidOwner, m_Bic, aid, val))
				return OnCorrupted();
		}
		break;

		case RecoveryTag::AssetDestroy:
		{
			bool bFwd = false;
			TemporarySwap swp(bFwd, m_Bic.m_Fwd);

			Asset::ID aid = 0;
			PeerID pidOwner;
			ContractID cid;
			der & aid;
			der & cid;

			Amount valDeposit;
			if (!m_Proc.HandleAssetDestroy(pidOwner, &cid, m_Bic, aid, valDeposit, false))
				return OnCorrupted();
		}
		break;

		case RecoveryTag::Log:
		{
			der & m_Bic.m_ContractLogs;

			if (!m_Bic.m_Temporary)
			{
				HeightPos pos(m_Bic.m_Height, m_Bic.m_ContractLogs);
				m_Proc.m_DB.ContractLogDel(pos, pos);
				m_Proc.m_DB.TestChanged1Row();
			}

			if (!m_Bic.m_SkipDefinition)
			{
				bool bMmr = false;
				der & bMmr;

				// Note: during reorg (i.e. proper rollback, not just tx undo) the logs array will be empty. Ignore this.
				if (bMmr && !m_Bic.m_vLogs.empty())
					m_Bic.m_vLogs.pop_back();
			}
		}
		break;

		case RecoveryTag::Recharge:
		{
			assert(m_Bic.m_Temporary);
			der & m_Bic.m_ChargePerBlock;
		}
		break;

		default:
			{
				der & key;
				auto& e = m_Bic.get_ContractVar(key, m_Proc.m_DB);

				if (RecoveryTag::Delete == nTag)
				{
					ContractDataDel(key, e.m_Data);
					e.m_Data.clear();
				}
				else
				{
					ByteBuffer data;
					der & data;

					if (RecoveryTag::Insert == nTag)
						ContractDataInsert(key, data);
					else
					{
						if (RecoveryTag::Update != nTag)
							OnCorrupted();
						ContractDataUpdate(key, data, e.m_Data);
					}

					e.m_Data.swap(data);
				}

			}
		}
	}
}

void NodeProcessor::BlockInterpretCtx::BvmProcessor::CopyExtraInfoSigs()
{
	assert(m_Bic.m_pvC);
	auto& vec = *m_Bic.m_pvC; // alias

	assert(m_iCurrentInvokeExtraInfo <= vec.size());
	ContractInvokeExtraInfo& x = vec[m_iCurrentInvokeExtraInfo - 1];

	assert(m_iSig0 <= m_vSigs.size());
	if (m_iSig0 < m_vSigs.size())
	{
		x.m_vSigs.reserve(x.m_vSigs.size() + m_vSigs.size() - m_iSig0);
		while (m_iSig0 < m_vSigs.size())
			x.m_vSigs.push_back(m_vSigs[m_iSig0++]);
	}
}

void NodeProcessor::BlockInterpretCtx::BvmProcessor::CallFar(const bvm2::ContractID& cid, uint32_t iMethod, Wasm::Word pArgs, uint32_t nArgs, uint32_t nFlags)
{
	bvm2::ProcessorContract::CallFar(cid, iMethod, pArgs, nArgs, nFlags);

	if (m_Bic.m_pvC)
	{
		auto& vec = *m_Bic.m_pvC; // alias

		if (!vec.empty())
			CopyExtraInfoSigs();

		ContractInvokeExtraInfo& x = vec.emplace_back();

		x.m_iParent = m_iCurrentInvokeExtraInfo;
		m_iCurrentInvokeExtraInfo = static_cast<uint32_t>(vec.size());
		x.m_NumNested = 0;

		assert(m_pFundsIO);
		m_pFundsIO->m_Map.swap(x.m_FundsIO.m_Map);

		x.m_Cid = m_FarCalls.m_Stack.back().m_Cid; // may be different from passed cid, if inheriting context

		// estimate the args size (roughly). Currently support only stack memory type (for in-context calls theoretically this may be any memory type)
		Wasm::Word nArgsOffs = pArgs ^ Wasm::MemoryType::Stack;
		
		Blob args;
		if (nArgsOffs < m_Stack.m_BytesMax)
		{
			args.n = (bvm2::CallFarFlags::InheritContext & nFlags) ?  (m_Stack.m_BytesMax - nArgsOffs) : nArgs;
			args.p = reinterpret_cast<uint8_t*>(m_Stack.m_pPtr) + nArgsOffs;
		}
		else
			ZeroObject(args);
	
		bvm2::ShaderID sid;
		bvm2::get_ShaderID(sid, m_Code); // code should be intact, contract didn't get control yet

		ParseExtraInfo(x, sid, iMethod, args);

		// skip args for inherited-context calls, we don't know the size, only the high bound. No need to save all this.
		if (bvm2::CallFarFlags::InheritContext & nFlags)
			args.n = 0;

		x.SetUnk(iMethod, args, &sid);
	}
}

void NodeProcessor::ContractInvokeExtraInfoBase::SetUnk(uint32_t iMethod, const Blob& args, const ECC::uintBig* pSid)
{
	m_iMethod = iMethod;
	if (m_sParsed.empty())
	{
		args.Export(m_Args);
		if (pSid)
			m_Sid.reset(*pSid);
	}
}

void NodeProcessor::BlockInterpretCtx::BvmProcessor::OnRet(Wasm::Word nRetAddr)
{
	bvm2::ProcessorContract::OnRet(nRetAddr);

	if (m_Bic.m_pvC && !nRetAddr)
	{
		auto& vec = *m_Bic.m_pvC; // alias
		CopyExtraInfoSigs();

		assert(m_iCurrentInvokeExtraInfo <= vec.size());
		auto& x = vec[m_iCurrentInvokeExtraInfo - 1];

		m_iCurrentInvokeExtraInfo = x.m_iParent;
		ContractInvokeExtraInfo* pParent = x.m_iParent ? &vec[x.m_iParent - 1] : nullptr;
		if (pParent)
			pParent->m_NumNested += x.m_NumNested + 1;

		if (x.m_FundsIO.m_Map.empty())
			x.m_FundsIO = *m_pFundsIO; // save it
		else
		{
			m_pFundsIO->m_Map.swap(x.m_FundsIO.m_Map); // our + nested

			// merge
			for (auto it = x.m_FundsIO.m_Map.begin(); x.m_FundsIO.m_Map.end() != it; it++)
				m_pFundsIO->Add(it->second, it->first);
		}
	}
}

Height NodeProcessor::GetInputMaturity(TxoID id)
{
	// awkward, but this is not used frequently.
	// NodeDB::StateInput doesn't contain the maturity of the spent UTXO. Hence we reconstruct it
	// We find the original UTXO height, and then decode the UTXO body, and check its additional maturity factors (coinbase, incubation)

	NodeDB::WalkerTxo wlk;
	m_DB.TxoGetValue(wlk, id);

	uint8_t pNaked[s_TxoNakedMax];
	Blob val = wlk.m_Value;

	TxoToNaked(pNaked, val);

	Deserializer der;
	der.reset(val.p, val.n);

	Output outp;
	der & outp;

	Height hCreate = 0;
	FindHeightByTxoID(hCreate, id); // relatively heavy operation: search for the original txo height

	return outp.get_MinMaturity(hCreate);
}

void NodeProcessor::RollbackTo(Block::Number num)
{
	assert(num.v <= m_Cursor.m_Full.m_Number.v);
	if (num.v == m_Cursor.m_Full.m_Number.v)
		return;

	assert(num.v >= m_Extra.m_Fossil.v);

	TxoID id0 = get_TxosBefore(Block::Number(num.v + 1));

	// undo outputs
	struct MyWalker
		:public ITxoWalker_UnspentNaked
	{
		NodeProcessor* m_pThis;

		virtual bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp) override
		{
			BlockInterpretCtx bic(hCreate, false);
			if (!m_pThis->HandleBlockElement(outp, bic))
				OnCorrupted();
			return true;
		}
	};

	MyWalker wlk2;
	wlk2.m_pThis = this;

	Block::NumberRange nr;
	nr.m_Min = Block::Number(num.v + 1);
	nr.m_Max = m_Cursor.m_Full.m_Number;
	EnumTxos(wlk2, nr);

	m_DB.TxoDelFrom(id0);

	// undo inputs, Kernels, shielded elements, and cursor
	ByteBuffer bbE, bbR;
	TxVectors::Eternal txve;

	BlockInterpretCtx::ChangesFlushGlobal cf(*this);

	NodeDB::StateID sid = m_Cursor.get_Sid();
	while (sid.m_Number.v > num.v)
	{
		// inputs
		std::vector<NodeDB::StateInput> v;
		m_DB.get_StateInputs(sid.m_Row, v);

		for (size_t i = 0; i < v.size(); i++)
		{
			const auto& src = v[i];
			TxoID id = src.get_ID();
			if (id >= id0)
				continue; // created and spent within this range - skip it (already deleted actually)

			Input inp;
			src.Get(inp.m_Commitment);

			InputAux inpAux;
			inpAux.m_ID = id;
			inpAux.m_Maturity = GetInputMaturity(id);

			UndoInput(inp, inpAux);

			m_DB.TxoSetSpent(id, MaxHeight);
		}

		m_DB.set_StateInputs(sid.m_Row, nullptr, 0);

		// kernels
		txve.m_vKernels.clear();
		bbE.clear();
		bbR.clear();
		m_DB.GetStateBlock(sid.m_Row, nullptr, &bbE, &bbR);

		Deserializer der;
		der.reset(bbE);
		der& Cast::Down<TxVectors::Eternal>(txve);

		Height h = Num2Height(sid);

		BlockInterpretCtx bic(h, false);
		bic.m_Rollback.swap(bbR);
		bic.m_ShieldedIns = static_cast<uint32_t>(-1); // suppress assertion
		bic.m_ShieldedOuts = static_cast<uint32_t>(-1);
		bic.m_nKrnIdx = static_cast<uint32_t>(-1);
		HandleElementVecBwd(txve.m_vKernels, bic, txve.m_vKernels.size());

		bic.m_Rollback.swap(bbR);
		assert(bbR.empty());

		m_DB.MoveBack(sid);
	}

	// other stuff

	cf.Do(*this);

	m_RecentStates.RollbackTo(num);
	m_ValCache.OnShLo(m_Extra.m_ShieldedOutputs);

	m_Mmr.m_States.ShrinkTo(m_Mmr.m_States.N2I(sid.m_Number));

	m_Extra.m_Txos = id0;

	InitCursor(false, sid);

	// height-dependent events
	Height h = m_Cursor.m_hh.m_Height;

	for (const auto& acc : m_vAccounts)
		m_DB.DeleteEventsFrom(acc.m_iAccount, h + 1);

	m_DB.AssetEvtsDeleteFrom(h + 1);
	m_DB.ShieldedOutpDelFrom(h + 1);
	m_DB.KrnInfoDelFrom(h + 1);

	if (!TestDefinition())
		OnCorrupted();

	OnRolledBack();
}

void NodeProcessor::AdjustManualRollbackNumber(Block::Number& num)
{
	Block::Number numMin = get_LowestManualReturnNumber();
	if (num.v < numMin.v)
	{
		BEAM_LOG_INFO() << "Can't go below Number " << numMin.v;
		num = numMin;
	}
}

void NodeProcessor::ManualRollbackInternal(Block::Number num)
{
	bool bChanged = false;

	if (IsFastSync() && (m_SyncData.m_Target.m_Number.v > num.v))
	{
		BEAM_LOG_INFO() << "Fast-sync abort...";

		RollbackTo(m_SyncData.m_n0);
		DeleteBlocksInRange(m_SyncData.m_Target, m_SyncData.m_n0);

		ZeroObject(m_SyncData);
		SaveSyncData();

		bChanged = true;
	}

	if (m_Cursor.m_Full.m_Number.v > num.v)
	{
		RollbackTo(num);
		bChanged = true;
	}

	if (bChanged)
		OnNewState();
}

void NodeProcessor::ManualRollbackTo(Block::Number num)
{
	BEAM_LOG_INFO() << "Manual rollback to " << num.v << "...";

	AdjustManualRollbackNumber(num);

	if (m_Cursor.m_Full.m_Number.v > num.v)
	{
		m_ManualSelection.m_Sid.m_Number.v = num.v + 1;
		m_DB.get_StateHash(FindActiveAtStrict(m_ManualSelection.m_Sid.m_Number), m_ManualSelection.m_Sid.m_Hash);

		m_ManualSelection.m_Forbidden = true;

		m_ManualSelection.Save();
		m_ManualSelection.Log();
	}

	ManualRollbackInternal(num);
}

void NodeProcessor::ManualSelect(const Block::SystemState::ID& sid)
{
	if ((MaxHeight == sid.m_Number.v) || !sid.m_Number.v)
		return; // ignore

	m_ManualSelection.m_Sid = sid;
	m_ManualSelection.m_Forbidden = false;
	m_ManualSelection.Save();
	m_ManualSelection.Log();

	if (m_Cursor.m_Full.m_Number.v >= sid.m_Number.v)
	{
		Merkle::Hash hv;
		m_DB.get_StateHash(FindActiveAtStrict(sid.m_Number), hv);

		if (hv == sid.m_Hash) {
			BEAM_LOG_INFO() << "Already at correct branch";
		}
		else
		{
			Block::Number num(sid.m_Number.v - 1);
			AdjustManualRollbackNumber(num);

			if (num.v == sid.m_Number.v - 1) {
				BEAM_LOG_INFO() << "Rolling back to " << num.v;
				ManualRollbackInternal(num);
			}
			else {
				BEAM_LOG_INFO() << "Unable to rollback below incorrect branch. Please resync from the beginning";
			}
		}
	}
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnStateInternal(const Block::SystemState::Full& s, Block::SystemState::ID& id, bool bAlreadyChecked)
{
	s.get_ID(id);

	if (!(bAlreadyChecked || s.IsValid()))
	{
		BEAM_LOG_WARNING() << id << " header invalid!";
		return DataStatus::Invalid;
	}

	Timestamp ts = getTimestamp();
	if (s.m_TimeStamp > ts)
	{
		ts = s.m_TimeStamp - ts; // dt
		if (ts > Rules::get().DA.MaxAhead_s)
		{
			BEAM_LOG_WARNING() << id << " Timestamp ahead by " << ts;
			return DataStatus::Invalid;
		}
	}

	if (s.m_Number.v < get_LowestReturnNumber().v)
	{
		m_UnreachableLog.Log(id);
		return DataStatus::Unreachable;
	}

	if (m_DB.StateFindSafe(id))
		return DataStatus::Rejected;

	return DataStatus::Accepted;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnState(const Block::SystemState::Full& s, const PeerID& peer)
{
	Block::SystemState::ID id;

	DataStatus::Enum ret = OnStateSilent(s, peer, id, false);
	if (DataStatus::Accepted == ret)
	{
		BEAM_LOG_INFO() << id << " Header accepted";
	}
	
	return ret;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnStateSilent(const Block::SystemState::Full& s, const PeerID& peer, Block::SystemState::ID& id, bool bAlreadyChecked)
{
	DataStatus::Enum ret = OnStateInternal(s, id, bAlreadyChecked);
	if (DataStatus::Accepted == ret)
		m_DB.InsertState(s, peer);

	return ret;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnBlock(const Block::SystemState::ID& id, const Blob& bbP, const Blob& bbE, const PeerID& peer)
{
	NodeDB::StateID sid;
	sid.m_Row = m_DB.StateFindSafe(id);
	if (!sid.m_Row)
	{
		BEAM_LOG_WARNING() << id << " Block unexpected";
		return DataStatus::Rejected;
	}

	sid.m_Number = id.m_Number;
	return OnBlock(sid, bbP, bbE, peer);
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnBlock(const NodeDB::StateID& sid, const Blob& bbP, const Blob& bbE, const PeerID& peer)
{
	size_t nSize = size_t(bbP.n) + size_t(bbE.n);
	if (nSize > Rules::get().MaxBodySize)
	{
		// TODO: don't account for QC and metadata
		BEAM_LOG_WARNING() << LogSid(m_DB, sid) << " Block too large: " << nSize;
		return DataStatus::Invalid;
	}

	if (NodeDB::StateFlags::Functional & m_DB.GetStateFlags(sid.m_Row))
	{
		BEAM_LOG_WARNING() << LogSid(m_DB, sid) << " Block already received";
		return DataStatus::Rejected;
	}

	if (sid.m_Number.v < get_LowestReturnNumber().v)
		return DataStatus::Unreachable;

	m_DB.SetStateBlock(sid.m_Row, bbP, bbE, peer);
	m_DB.SetStateFunctional(sid.m_Row);

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

	if (IsTreasuryHandled())
		return DataStatus::Rejected;

	if (!HandleTreasury(blob))
		return DataStatus::Invalid;

	m_Extra.m_Txos++;
	m_Extra.m_TxosTreasury = m_Extra.m_Txos;
	m_DB.ParamSet(NodeDB::ParamID::Treasury, &m_Extra.m_TxosTreasury, &blob);

	BEAM_LOG_INFO() << "Treasury verified";

	RescanAccounts(static_cast<uint32_t>(m_vAccounts.size()));

	OnNewState();
	TryGoUp();

	return DataStatus::Accepted;
}

bool NodeProcessor::Tip::IsRemoteNeeded(const Tip& tRemote) const
{
	int n = m_Full.m_ChainWork.cmp(tRemote.m_Full.m_ChainWork);
	if (n > 0)
		return false;
	if (n < 0)
		return true;

	return m_hh != tRemote.m_hh;
}

uint64_t NodeProcessor::FindActiveAtStrict(Block::Number num)
{
	const RecentStates::Entry* pE = m_RecentStates.Get(num);
	if (pE)
		return pE->m_RowID;

	return m_DB.FindActiveStateStrict(num);
}

void NodeProcessor::FindActiveAtStrict(NodeDB::StateID& sid, Height h)
{
	const Rules& r = Rules::get();
	if (r.IsConstantSpan())
	{
		sid.m_Number.v = h;
		sid.m_Row = FindActiveAtStrict(sid.m_Number);
	}
	else
	{
		if (h)
		{
			// Search w.r.t. chainwork
			Difficulty::Raw d;
			r.Height2Difficulty(d, h);

			m_DB.FindActiveStateStrict(sid, d);
		}
		else
			sid.SetNull();
	}
}

Height NodeProcessor::Num2Height(const NodeDB::StateID& sid)
{
	if (Rules::get().IsConstantSpan())
		return sid.m_Number.v;

	if (!sid.m_Number.v)
		return 0;

	Block::SystemState::Full s;
	m_DB.get_State(sid.m_Row, s);
	return s.get_Height();
}

Height NodeProcessor::Num2Height(Block::Number num)
{
	if (Rules::get().IsConstantSpan())
		return num.v;

	if (!num.v)
		return 0;

	auto row = FindActiveAtStrict(num);

	Block::SystemState::Full s;
	m_DB.get_State(row, s);
	return s.get_Height();
}

Block::Number NodeProcessor::FindAtivePastHeight(Height h)
{
	assert(h <= m_Cursor.m_hh.m_Height);

	if (Rules::get().IsConstantSpan())
		return Block::Number(h);

	NodeDB::StateID sid;
	FindAtivePastHeight(sid, h);
	return sid.m_Number;
}

void NodeProcessor::FindAtivePastHeight(NodeDB::StateID& sid, Height h)
{
	assert(h <= m_Cursor.m_hh.m_Height);

	if (h)
	{
		if (h == m_Cursor.m_hh.m_Height)
			sid = m_Cursor.get_Sid();
		else
		{
			const Rules& r = Rules::get();
			if (r.IsConstantSpan())
			{
				sid.m_Number.v = h;
				sid.m_Row = FindActiveAtStrict(sid.m_Number);
			}
			else
			{
				// Search w.r.t. chainwork
				Difficulty::Raw d;
				r.Height2Difficulty(d, h);
				m_DB.FindActiveStateStrictLowBound(sid, d);
			}
		}
	}
	else
		sid.SetNull();

}


/////////////////////////////
// Block generation
Difficulty NodeProcessor::get_NextDifficulty()
{
	const Rules& r = Rules::get(); // alias

	if (!m_Cursor.m_Full.m_Number.v || (Rules::Consensus::PoW != r.m_Consensus))
		return r.DA.Difficulty0; // 1st block

	THW thw0, thw1;

	get_MovingMedianEx(m_Cursor.m_Full.m_Number, r.DA.WindowMedian1, thw1);

	if (m_Cursor.m_Full.m_Number.v - 1 >= r.DA.WindowWork)
	{
		get_MovingMedianEx(Block::Number(m_Cursor.m_Full.m_Number.v - r.DA.WindowWork), r.DA.WindowMedian1, thw0);
	}
	else
	{
		get_MovingMedianEx(Block::Number(1), r.DA.WindowMedian1, thw0); // awkward to look for median, since they're immaginary. But makes sure we stick to the same median search and rounding (in case window is even).

		// how many immaginary prehistoric blocks should be offset
		uint32_t nDelta = r.DA.WindowWork - static_cast<uint32_t>(m_Cursor.m_Full.m_Number.v - 1);

		thw0.first -= static_cast<int64_t>(r.DA.get_Target_s()) * nDelta;
		thw0.second.first -= nDelta;

		Difficulty::Number wrk, wrk2;
		r.DA.Difficulty0.Unpack(wrk);
		wrk2.get_Slice().SetMul(wrk.get_ConstSlice(), MultiWord::ConstSlice{ &nDelta, 1 });

		thw0.second.second.ToNumber(wrk);
		wrk -= wrk2;
		thw0.second.second.FromNumber(wrk);
	}

	assert(r.DA.WindowWork > r.DA.WindowMedian1); // when getting median - the target height can be shifted by some value, ensure it's smaller than the window
	// means, the height diff should always be positive
	assert(thw1.second.first > thw0.second.first);

	uint32_t dh = static_cast<uint32_t>(thw1.second.first - thw0.second.first);

	uint32_t dtTrg_s = r.DA.get_Target_s() * dh;

	// actual dt, only making sure it's non-negative
	uint32_t dtSrc_s = (thw1.first > thw0.first) ? static_cast<uint32_t>(thw1.first - thw0.first) : 0;

	if (r.IsPastFork_<1>(m_Cursor.m_hh.m_Height))
	{
		// Apply dampening. Recalculate dtSrc_s := dtSrc_s * M/N + dtTrg_s * (N-M)/N
		// Use 64-bit arithmetic to avoid overflow

		uint64_t nVal =
			static_cast<uint64_t>(dtSrc_s) * r.DA.Damp.M +
			static_cast<uint64_t>(dtTrg_s) * (r.DA.Damp.N - r.DA.Damp.M);

		uint32_t dt_s = static_cast<uint32_t>(nVal / r.DA.Damp.N);

		if ((dt_s > dtSrc_s) != (dt_s > dtTrg_s)) // another overflow verification. The result normally must sit between src and trg (assuming valid damp parameters, i.e. M < N).
			dtSrc_s = dt_s;
	}

	// apply "emergency" threshold
	std::setmin(dtSrc_s, dtTrg_s * 2);
	std::setmax(dtSrc_s, dtTrg_s / 2);


	Difficulty::Raw& dWrk = thw0.second.second;
	dWrk.Negate();
	dWrk += thw1.second.second;

	Difficulty res;
	res.Calculate(dWrk, dh, dtTrg_s, dtSrc_s);

	return res;
}

void NodeProcessor::get_MovingMedianEx(Block::Number numLast, uint32_t nWindow, THW& res)
{
	std::vector<THW> v;
	v.reserve(nWindow);

	assert(numLast.v >= 1);
	uint64_t rowLast = 0;

	while (v.size() < nWindow)
	{
		v.emplace_back();
		THW& thw = v.back();

		if (numLast.v)
		{
			const RecentStates::Entry* pE = m_RecentStates.Get(numLast);

			Block::SystemState::Full sDb;
			if (!pE)
			{
				if (rowLast)
				{
					if (!m_DB.get_Prev(rowLast))
						OnCorrupted();
				}
				else
					rowLast = FindActiveAtStrict(numLast);

				m_DB.get_State(rowLast, sDb);
			}

			const Block::SystemState::Full& s = pE ? pE->m_State : sDb;

			thw.first = s.m_TimeStamp;
			thw.second.first = s.m_Number.v;
			thw.second.second = s.m_ChainWork;

			numLast.v--;
		}
		else
		{
			// append "prehistoric" blocks of starting difficulty and perfect timing
			const THW& thwSrc = v[v.size() - 2];

			thw.first = thwSrc.first - Rules::get().DA.get_Target_s();
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
	if (!m_Cursor.m_Full.m_Number.v)
		return 0;

	THW thw;
	get_MovingMedianEx(m_Cursor.m_Full.m_Number, Rules::get().DA.WindowMedian0, thw);
	return thw.first;
}

uint8_t NodeProcessor::ValidateTxContextEx(const Transaction& tx, const HeightRange& hr, bool bShieldedTested, uint32_t& nBvmCharge, TxPool::Dependent::Element* pParent, std::ostream* pExtraInfo, Merkle::Hash* pCtxNew)
{
	Height h = m_Cursor.m_hh.m_Height + 1;

	if (!hr.IsInRange(h))
	{
		if (pExtraInfo)
			*pExtraInfo << "Height range fail";
		return proto::TxStatus::InvalidContext;
	}

	BlockInterpretCtx bic(h, true);
	bic.SetAidMax(*this);

	bic.m_Temporary = true;
	bic.m_TxValidation = true;
	bic.m_SkipDefinition = true;
	bic.m_pTxErrorInfo = pExtraInfo;
	bic.m_AlreadyValidated = true;

	DependentContextSwitch dcs(*this, bic);
	if (!dcs.Apply(pParent))
	{
		BEAM_LOG_WARNING() << "can't switch dependent context"; // normally should not happen
		return proto::TxStatus::DependentNoParent;
	}

	bool bNewVal = false;
	TemporarySwap ts(bic.m_AlreadyValidated, bNewVal);

	// Cheap tx verification. No need to update the internal structure, recalculate definition, or etc.

	// Ensure input UTXOs are present
	for (size_t i = 0; i < tx.m_vInputs.size(); i++)
	{
		Input::Count nCount = 1;
		const Input& v = *tx.m_vInputs[i];

		for (; i + 1 < tx.m_vInputs.size(); i++, nCount++)
			if (tx.m_vInputs[i + 1]->m_Commitment != v.m_Commitment)
				break;

		if (!ValidateInputs(v.m_Commitment, nCount))
		{
			if (pExtraInfo)
				*pExtraInfo << "Inputs missing";

			return proto::TxStatus::InvalidInput; // some input UTXOs are missing
		}
	}

	nBvmCharge = bic.m_ChargePerBlock;

	size_t n = 0;
	bool bOk = HandleElementVecFwd(tx.m_vKernels, bic, n);

	if (bOk && pCtxNew)
		*pCtxNew = bic.m_DependentCtxSet ? bic.m_hvDependentCtx : m_Cursor.m_Full.m_Prev;

	nBvmCharge -= bic.m_ChargePerBlock;

	if (!bic.m_ShieldedIns)
		bShieldedTested = true;

	bic.m_Fwd = false;
	HandleElementVecBwd(tx.m_vKernels, bic, n);

	if (!bOk)
	{
		if (proto::TxStatus::Unspecified != bic.m_TxStatus)
			return bic.m_TxStatus;

		if (bic.m_LimitExceeded)
			return proto::TxStatus::LimitExceeded;

		return proto::TxStatus::InvalidContext;
	}

	// Ensure output assets are in range
	for (size_t i = 0; i < tx.m_vOutputs.size(); i++)
		if (!bic.ValidateAssetRange(tx.m_vOutputs[i]->m_pAsset))
			return proto::TxStatus::InvalidContext;

	if (!bShieldedTested)
	{
		ECC::InnerProduct::BatchContextEx<4> bc;
		MultiShieldedContext msc;

		msc.Prepare(tx, *this, h);

		bool bValid = msc.IsValid(tx, h, bc, 0, 1, m_ValCache);
		if (bValid)
		{
			msc.Calculate(bc.m_Sum, *this);
			bValid = bc.Flush();
		}

		if (!bValid)
		{
			if (pExtraInfo)
				*pExtraInfo << "bad shielded input";
			return proto::TxStatus::InvalidInput;
		}

		msc.MoveToGlobalCache(m_ValCache);
	}

	return proto::TxStatus::Ok;
}

bool NodeProcessor::ValidateInputs(const ECC::Point& comm, Input::Count nCount /* = 1 */)
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
	t.m_Count = nCount;


	UtxoTree::Key kMin, kMax;

	UtxoTree::Key::Data d;
	d.m_Commitment = comm;
	d.m_Maturity = 0;
	kMin = d;
	d.m_Maturity = m_Cursor.m_hh.m_Height;
	kMax = d;

	UtxoTree::Cursor cu;
	t.m_pCu = &cu;
	t.m_pBound[0] = kMin.V.m_pData;
	t.m_pBound[1] = kMax.V.m_pData;

	return !m_Mapped.m_Utxo.Traverse(t);
}

size_t NodeProcessor::GenerateNewBlockInternal(BlockContext& bc, BlockInterpretCtx& bic)
{
	Height h = m_Cursor.m_hh.m_Height + 1;
	const auto& r = Rules::get();

	// Generate the block up to the allowed size.
	// All block elements are serialized independently, their binary size can just be added to the size of the "empty" block.

	SerializerSizeCounter ssc;
	ssc & bc.m_Block;

	Block::Builder bb(bc.m_SubIdx, bc.m_Coin, bc.m_Tag, h);

	Output::Ptr pOutp;
	TxKernel::Ptr pKrn;

	if (Rules::Consensus::Pbft != r.m_Consensus)
	{
		bb.AddCoinbaseAndKrn(pOutp, pKrn);
		if (pOutp)
			ssc & *pOutp;
		yas::detail::SaveKrn(ssc, *pKrn, false); // pessimistic
	}

	ECC::Scalar::Native offset = bc.m_Block.m_Offset;

	if (BlockContext::Mode::Assemble != bc.m_Mode)
	{
		if (pOutp)
		{
			if (!HandleBlockElement(*pOutp, bic))
				return 0;

			bc.m_Block.m_vOutputs.push_back(std::move(pOutp));
		}

		if (pKrn)
		{
			if (!HandleBlockElement(*pKrn, bic))
				return 0;

			bc.m_Block.m_vKernels.push_back(std::move(pKrn));
		}
	}

	if (Rules::Consensus::Pbft != r.m_Consensus)
	{
		// estimate the size of the fees UTXO
		if (!m_nSizeUtxoComissionUpperLimit)
		{
			Output outp;
			outp.m_pConfidential.reset(new ECC::RangeProof::Confidential);
			ZeroObject(*outp.m_pConfidential);
			outp.m_pAsset = std::make_unique<Asset::Proof>();
			outp.m_pAsset->InitArrays(r.CA.m_ProofCfg);
			outp.m_pAsset->m_Begin = static_cast<Asset::ID>(-1);

			SerializerSizeCounter ssc2;
			ssc2 & outp;
			m_nSizeUtxoComissionUpperLimit = ssc2.m_Counter.m_Value;
		}

		if (bc.m_Fees)
			ssc.m_Counter.m_Value += m_nSizeUtxoComissionUpperLimit;
	}

	const size_t nSizeMax = r.MaxBodySize;
	if (ssc.m_Counter.m_Value > nSizeMax)
	{
		// the block may be non-empty (i.e. contain treasury)
		BEAM_LOG_WARNING() << "Block too large.";
		return 0; //
	}

	size_t nTxNum = 0;

	DependentContextSwitch::Vec vDependent;
	DependentContextSwitch::Convert(vDependent, bc.m_pParent);

	for (size_t i = 0; i < vDependent.size(); i++)
	{
		// Theoretically for dependent txs can set m_AlreadyValidated flag. But it's not good to mix validated and non-validated in the same pass (ManageKrnID would be confused).
		// For now - ignore this optimization

		const auto& x = *vDependent[i];
		Amount txFee = x.m_Fee;
		auto nSize = x.m_Size;
		if (x.m_pParent)
		{
			txFee -= x.m_pParent->m_Fee;
			nSize -= x.m_pParent->m_Size;
		}

		Amount feesNext = bc.m_Fees + txFee;
		if (feesNext < bc.m_Fees)
			break; // huge fees are unsupported


		size_t nSizeNext = ssc.m_Counter.m_Value + nSize;
		if (!bc.m_Fees && feesNext && (Rules::Consensus::Pbft != r.m_Consensus))
			nSizeNext += m_nSizeUtxoComissionUpperLimit;

		if (nSizeNext > nSizeMax)
			break;

		Transaction& tx = *x.m_pValue;

		assert(!bic.m_LimitExceeded);
		if (!HandleValidatedTx(tx, bic))
		{
			bic.m_LimitExceeded = false;
			break;
		}

		TxVectors::Writer(bc.m_Block, bc.m_Block).Dump(tx.get_Reader());

		bc.m_Fees = feesNext;
		ssc.m_Counter.m_Value = nSizeNext;
		offset += ECC::Scalar::Native(tx.m_Offset);
		++nTxNum;
	}

	for (TxPool::Fluff::ProfitSet::iterator it = bc.m_TxPool.m_setProfit.begin(); bc.m_TxPool.m_setProfit.end() != it; )
	{
		TxPool::Fluff::Element& x = (it++)->get_ParentObj();

		Amount feesNext = bc.m_Fees + x.m_Profit.m_Stats.m_Fee;
		if (feesNext < bc.m_Fees)
			continue; // huge fees are unsupported

		size_t nSizeNext = ssc.m_Counter.m_Value + x.m_Profit.m_Stats.m_Size;
		if (!bc.m_Fees && feesNext && (Rules::Consensus::Pbft != r.m_Consensus))
			nSizeNext += m_nSizeUtxoComissionUpperLimit;

		if (nSizeNext > nSizeMax)
		{
			if (bc.m_Block.m_vInputs.empty() &&
				(bc.m_Block.m_vOutputs.size() == 1) &&
				(bc.m_Block.m_vKernels.size() == 1))
			{
				// won't fit in empty block
				BEAM_LOG_INFO() << "Tx is too big.";
				bc.m_TxPool.Delete(x);
			}
			continue;
		}

		Transaction& tx = *x.m_pValue;

		bool bDelete = !x.m_Profit.m_Stats.m_Hr.IsInRange(bic.m_Height);
		if (!bDelete)
		{
			assert(!bic.m_LimitExceeded);
			if (HandleValidatedTx(tx, bic))
			{
				TxVectors::Writer(bc.m_Block, bc.m_Block).Dump(tx.get_Reader());

				bc.m_Fees = feesNext;
				ssc.m_Counter.m_Value = nSizeNext;
				offset += ECC::Scalar::Native(tx.m_Offset);
				++nTxNum;
			}
			else
			{
				if (bic.m_LimitExceeded)
					bic.m_LimitExceeded = false; // don't delete it, leave it for the next block
				else
					bDelete = true;
			}
		}

		if (bDelete)
		{
			x.m_Hist.m_Height = m_Cursor.m_hh.m_Height;
			bc.m_TxPool.SetState(x, TxPool::Fluff::State::Outdated); // isn't available in this context
		}
	}

	BEAM_LOG_INFO() << "GenerateNewBlock: size of block = " << ssc.m_Counter.m_Value << "; amount of tx = " << nTxNum;

	if (BlockContext::Mode::Assemble != bc.m_Mode)
	{
		if (bc.m_Fees && (Rules::Consensus::Pbft != r.m_Consensus))
		{
			{
				Asset::Proof::Params::Override po(get_AidMax());
				bb.AddFees(bc.m_Fees, pOutp);
			}

			if (!HandleBlockElement(*pOutp, bic))
				return 0;

			// make size estimation more precise
			size_t n0 = ssc.m_Counter.m_Value;
			ssc.m_Counter.m_Value -= m_nSizeUtxoComissionUpperLimit;
			ssc& (*pOutp);
			assert(ssc.m_Counter.m_Value <= n0);
			(n0); // suppress 'unused' warning in release build

			bc.m_Block.m_vOutputs.push_back(std::move(pOutp));
		}

		bb.m_Offset = -bb.m_Offset;
		offset += bb.m_Offset;
	}

	bc.m_Block.m_Offset = offset;

	return ssc.m_Counter.m_Value;
}

void NodeProcessor::GenerateNewHdr(BlockContext& bc, BlockInterpretCtx& bic)
{
#ifndef NDEBUG
	// kernels must be sorted already
	for (size_t i = 1; i < bc.m_Block.m_vKernels.size(); i++)
	{
		const TxKernel& krn0 = *bc.m_Block.m_vKernels[i - 1];
		const TxKernel& krn1 = *bc.m_Block.m_vKernels[i];
		assert(krn0 <= krn1);
	}
#endif // NDEBUG

	EvaluatorEx ev(*this);
	ev.m_Number = bc.m_Hdr.m_Number;
	ev.m_Height = bc.m_Hdr.get_Height();
	ev.set_Kernels(bc.m_Block);
	ev.set_Logs(bic.m_vLogs);

	ev.get_Definition(bc.m_Hdr.m_Definition);

	if (Rules::get().IsPastFork_<3>(ev.m_Height))
		get_Utxos().get_Hash(bc.m_Hdr.m_Kernels);
	else
		bc.m_Hdr.m_Kernels = ev.m_hvKernels;
}

NodeProcessor::BlockContext::BlockContext(TxPool::Fluff& txp, Key::Index nSubKey, Key::IKdf& coin, Key::IPKdf& tag)
	:m_TxPool(txp)
	,m_pParent(nullptr)
	,m_SubIdx(nSubKey)
	,m_Coin(coin)
	,m_Tag(tag)
{
	m_Fees = 0;
	m_Block.ZeroInit();
}

bool NodeProcessor::GenerateNewBlock(BlockContext& bc)
{
	bc.m_Hdr.m_Number.v = m_Cursor.m_Full.m_Number.v + 1;

	BlockInterpretCtx bic(m_Cursor.m_hh.m_Height + 1, true);

	const auto& r = Rules::get();
	if ((Rules::Consensus::Pbft == r.m_Consensus) && m_Cursor.m_Full.m_Number.v)
	{
		auto iSlot0 = r.m_Pbft.T2S(m_Cursor.m_Full.get_Timestamp_ms());
		auto iSlot1 = r.m_Pbft.T2S_strict(bc.m_Hdr.get_Timestamp_ms());
		assert(iSlot1 > iSlot0);

		// next height in pbft is determined w.r.t. timestamp
		bic.m_Height = m_Cursor.m_hh.m_Height + iSlot1 - iSlot0;
	}


	bic.m_Temporary = true;
	bic.m_SkipDefinition = true;
	bic.SetAidMax(*this);

	size_t nSizeEstimated = 1;

	if (BlockContext::Mode::Finalize == bc.m_Mode)
	{
		if (!HandleValidatedTx(bc.m_Block, bic))
			return false;
	}
	else
		nSizeEstimated = GenerateNewBlockInternal(bc, bic);

	bic.m_Fwd = false;
    BEAM_VERIFY(HandleValidatedTx(bc.m_Block, bic)); // undo changes
	assert(bic.m_Rollback.empty());
	assert(bic.m_vInpAux.empty());

	if (!nSizeEstimated)
		return false;

	if (BlockContext::Mode::Assemble == bc.m_Mode)
		return true;

	size_t nCutThrough = bc.m_Block.Normalize(); // right before serialization
	nCutThrough; // remove "unused var" warning

	// The effect of the cut-through block may be different than it was during block construction, because the consumed and created UTXOs (removed by cut-through) could have different maturities.
	// Hence - we need to re-apply the block after the cut-throught, evaluate the definition, and undo the changes (once again).
	//
	// In addition to this, kernels reorder may also have effect: shielded outputs may get different IDs
	bic.m_Fwd = true;
	bic.m_AlreadyValidated = true;
	bic.m_SkipDefinition = false;

	if (Rules::Consensus::Pbft == r.m_Consensus)
	{
		auto& d = Cast::Reinterpret<Block::Pbft::HdrData>(bc.m_Hdr.m_PoW);

		m_PbftState.m_Hash.Update();
		d.m_hvVsPrev = m_PbftState.m_Hash.m_hv;
	}

	bool bOk = HandleValidatedTx(bc.m_Block, bic);
	if (!bOk)
	{
		BEAM_LOG_WARNING() << "couldn't apply block after cut-through!";
		ZeroObject(bc.m_Hdr);
		OnInvalidBlock(bc.m_Hdr, bc.m_Block);
		return false; // ?!
	}

	bc.m_Hdr.m_Prev = m_Cursor.m_hh.m_Hash;
	bc.m_Hdr.m_ChainWork = m_Cursor.m_Full.m_ChainWork;

	if (Rules::Consensus::Pbft == r.m_Consensus)
	{
		auto& d = Cast::Reinterpret<Block::Pbft::HdrData>(bc.m_Hdr.m_PoW);
		ZeroObject(d.m_pPad0);
		d.m_hvMetadata = Zero;
		d.m_nSizeMetadata = Zero;

		m_PbftState.m_Hash.Update();
		d.m_hvVsNext = m_PbftState.m_Hash.m_hv;

		// Assume d.m_Time_ms and the hdr timestamp are already assigned by the caller

		if (m_Cursor.m_hh.m_Height)
		{
			d.m_Flags1 = bc.m_Block.IsEmpty() ? Block::Pbft::HdrData::Flags::Empty : 0;
			auto dh = static_cast<uint32_t>(bic.m_Height - m_Cursor.m_hh.m_Height);

			auto& d0 = Cast::Reinterpret<Block::Pbft::HdrData>(m_Cursor.m_Full.m_PoW);
			if (Block::Pbft::HdrData::Flags::Empty & d0.m_Flags1)
			{
				// cannibalize the previous empty block
				bc.m_Hdr.m_Prev = m_Cursor.m_Full.m_Prev;
				bc.m_Hdr.m_Number = m_Cursor.m_Full.m_Number;
				bc.m_Hdr.m_ChainWork -= m_Cursor.m_Full.m_PoW.m_Difficulty;

				dh += r.Difficulty2Span(m_Cursor.m_Full.m_PoW.m_Difficulty);
			}

			bc.m_Hdr.m_PoW.m_Difficulty = r.Span2Difficulty(dh);

		}
		else
		{
			d.m_Flags1 = 0; // very 1st block, defines the slot-height relation
			bc.m_Hdr.m_PoW.m_Difficulty = r.DA.Difficulty0;
		}
	}
	else
	{
		bc.m_Hdr.m_TimeStamp = getTimestamp();
		// Adjust the timestamp to be no less than the moving median (otherwise the block'll be invalid)
		Timestamp tm = get_MovingMedian() + 1;
		std::setmax(bc.m_Hdr.m_TimeStamp, tm);

		bc.m_Hdr.m_PoW.m_Difficulty = m_Cursor.m_DifficultyNext;
	}

	bc.m_Hdr.m_ChainWork += bc.m_Hdr.m_PoW.m_Difficulty; // always, in either mode

	GenerateNewHdr(bc, bic);

	bic.m_Fwd = false;
    BEAM_VERIFY(HandleValidatedTx(bc.m_Block, bic)); // undo changes
	assert(bic.m_Rollback.empty());

	Serializer ser;

	ser.reset();
	ser & Cast::Down<Block::BodyBase>(bc.m_Block);
	ser & Cast::Down<TxVectors::Perishable>(bc.m_Block);
	ser.swap_buf(bc.m_Body.m_Perishable);

	ser.reset();
	ser & Cast::Down<TxVectors::Eternal>(bc.m_Block);
	ser.swap_buf(bc.m_Body.m_Eternal);

	size_t nSize = bc.m_Body.m_Perishable.size() + bc.m_Body.m_Eternal.size();

	if (BlockContext::Mode::SinglePass == bc.m_Mode)
	{
		// the actual block size may be less because of:
		// 1. Cut-through removed some data
		// 2. our size estimation is a little pessimistic because of extension of kernels. If all kernels are standard, then 1 bytes per kernel is saved
		assert(nCutThrough ?
			(nSize < nSizeEstimated) :
			(
				(nSize == nSizeEstimated) ||
				(nSize == nSizeEstimated - bc.m_Block.m_vKernels.size())
			)
		);
	}

	return nSize <= Rules::get().MaxBodySize;
}

Executor& NodeProcessor::get_Executor()
{
	if (!m_pExecSync)
	{
		m_pExecSync = std::make_unique<MyExecutor>();
		m_pExecSync->m_Ctx.m_pThis = m_pExecSync.get();
		m_pExecSync->m_Ctx.m_iThread = 0;
	}

	return *m_pExecSync;
}

uint32_t NodeProcessor::MyExecutor::get_Threads()
{
	return 1;
}

void NodeProcessor::MyExecutor::Push(TaskAsync::Ptr&& pTask)
{
	ExecAll(*pTask);
}

uint32_t NodeProcessor::MyExecutor::Flush(uint32_t)
{
	return 0;
}

void NodeProcessor::MyExecutor::ExecAll(TaskSync& t)
{
	ECC::InnerProduct::BatchContext::Scope scope(m_Ctx.m_BatchCtx);
	t.Exec(m_Ctx);
}

bool NodeProcessor::ValidateAndSummarize(TxBase::Context& ctx, const TxBase& txb, TxBase::IReader&& r, std::string& sErr)
{
	struct MyShared
		:public MultiblockContext::MyTask::Shared
	{
		TxBase::Context* m_pCtx;
		const TxBase* m_pTx;
		TxBase::IReader* m_pR;

		MyShared(MultiblockContext& mbc)
			:MultiblockContext::MyTask::Shared(mbc)
		{
		}

		virtual ~MyShared() {} // auto

		virtual void Exec(uint32_t iThread) override
		{
			TxBase::Context ctx;
			ctx.m_Params = m_pCtx->m_Params;
			ctx.m_Height = m_pCtx->m_Height;
			ctx.m_iVerifier = iThread;

			TxBase::IReader::Ptr pR;
			m_pR->Clone(pR);

			bool bValid = true;
			std::string sErr;

			try {
				ctx.ValidateAndSummarizeStrict(*m_pTx, std::move(*pR));
			} catch (const std::exception& e) {
				bValid = false;
				sErr = e.what();
			}

			std::unique_lock<std::mutex> scope(m_Mbc.m_Mutex);

			if (!m_Mbc.m_bFail)
			{
				if (bValid)
				{
					try {
						m_pCtx->MergeStrict(ctx);
					} catch (const std::exception& e) {
						bValid = false;
						sErr = e.what();
					}
				}

				if (!bValid)
				{
					m_Mbc.m_bFail = true;
					m_Mbc.m_sErr = std::move(sErr);
				}

			}
		}
	};

	MultiblockContext mbc(*this);

	std::shared_ptr<MyShared> pShared = std::make_shared<MyShared>(mbc);

	pShared->m_pCtx = &ctx;
	pShared->m_pTx = &txb;
	pShared->m_pR = &r;

	// No need to realize all the kernels, since this is invoked synchronously (we wait till everything finishes before exiting this func).
	// hence no risk of race w.r.t. kernel realization

	mbc.m_InProgress.m_Max.v++; // dummy, just to emulate ongoing progress
	mbc.PushTasks(pShared, ctx.m_Params);

	if (mbc.Flush())
		return true;

	sErr = std::move(mbc.m_sErr);
	return false;
}

void NodeProcessor::ExtractBlockWithExtra(const NodeDB::StateID& sid, Height h, std::vector<TxoInfo>& vIns, std::vector<TxoInfo>& vOuts, TxVectors::Eternal& txe, std::vector<ContractInvokeExtraInfo>& vC)
{
	{
		// kernels
		ByteBuffer bbE;
		m_DB.GetStateBlock(sid.m_Row, nullptr, &bbE, nullptr);

		Deserializer der;
		der.reset(bbE);
		der & txe;

		NodeDB::KrnInfo::Walker wlk;
		for (m_DB.KrnInfoEnum(wlk, h); wlk.MoveNext(); )
		{
			auto& info = vC.emplace_back();
			info.m_Cid = wlk.m_Entry.m_Cid;

			der.reset(wlk.m_Entry.m_Val.p, wlk.m_Entry.m_Val.n);
			der & info;
		}

	}

	{
		// inputs
		std::vector<NodeDB::StateInput> v;
		m_DB.get_StateInputs(sid.m_Row, v);

		vIns.resize(v.size());

		for (uint32_t i = 0; i < v.size(); i++)
		{
			TxoID txoID = v[i].get_ID();
			auto& dst = vIns[i];

			NodeDB::WalkerTxo wlk;
			m_DB.TxoGetValue(wlk, txoID);

			Deserializer der;
			der.reset(wlk.m_Value.p, wlk.m_Value.n);
			der & dst.m_Outp;

			dst.m_hSpent = h;

			FindHeightByTxoID(dst.m_hCreate, txoID);
		}
	}

	{
		// outputs
		TxoID id1 = get_TxosBefore(Block::Number(sid.m_Number.v + 1));
		TxoID id0 = get_TxosBefore(sid.m_Number);
		vOuts.reserve(id1 - id0);

		NodeDB::WalkerTxo wlk;
		for (m_DB.EnumTxos(wlk, id0); wlk.MoveNext(); )
		{
			if (wlk.m_ID >= id1)
				break;

			auto& dst = vOuts.emplace_back();

			Deserializer der;
			der.reset(wlk.m_Value.p, wlk.m_Value.n);
			der & dst.m_Outp;

			dst.m_hCreate = h;
			dst.m_hSpent = wlk.m_SpendHeight;
		}
	}
}

void NodeProcessor::ExtractTreasurykWithExtra(std::vector<TxoInfo>& vOuts)
{
	NodeDB::WalkerTxo wlk;
	for (m_DB.EnumTxos(wlk, 0); wlk.MoveNext(); )
	{
		if (wlk.m_ID >= m_Extra.m_TxosTreasury)
			break;

		auto& dst = vOuts.emplace_back();

		Deserializer der;
		der.reset(wlk.m_Value.p, wlk.m_Value.n);
		der& dst.m_Outp;

		dst.m_hCreate = 0;
		dst.m_hSpent = wlk.m_SpendHeight;
	}
}

TxoID NodeProcessor::get_TxosBefore(Block::Number num)
{
	if (!num.v)
		return 0;

	if (num.v == 1u)
		return m_Extra.m_TxosTreasury;

	TxoID id = m_DB.get_StateTxos(FindActiveAtStrict(Block::Number(num.v - 1)));
	if (MaxHeight == id)
		OnCorrupted();

	return id;
}

TxoID NodeProcessor::FindBlockByTxoID(NodeDB::StateID& sid, TxoID id0)
{
	if (id0 < m_Extra.m_TxosTreasury)
	{
		sid.SetNull();
		return m_Extra.m_TxosTreasury;
	}

	return m_DB.FindStateByTxoID(sid, id0);
}

TxoID NodeProcessor::FindHeightByTxoID(Height& h, TxoID id0)
{
	NodeDB::StateID sid;
	auto ret = FindBlockByTxoID(sid, id0);

	h = Num2Height(sid);
	return ret;
}

bool NodeProcessor::EnumTxos(ITxoWalker& wlk)
{
	Block::NumberRange nr;
	nr.m_Min.v = 0; // treasury included
	nr.m_Max = m_Cursor.m_Full.m_Number;
	return EnumTxos(wlk, nr);
}

bool NodeProcessor::EnumTxos(ITxoWalker& wlkTxo, const Block::NumberRange& nr)
{
	if (nr.IsEmpty())
		return true;
	assert(nr.m_Max.v <= m_Cursor.m_Full.m_Number.v);

	TxoID idBeg = get_TxosBefore(nr.m_Min);
	TxoID idEnd = get_TxosBefore(Block::Number(nr.m_Max.v + 1));
	assert(idBeg <= idEnd);

	if (idBeg == idEnd)
	{
		// there's an artificail gap in TxoID in each block, so this basically shouldn't happen
		// the only exception: Node state is empty, even Treasury wasn't processed yet
		assert(!nr.m_Min.v && !nr.m_Max.v && !IsTreasuryHandled());
		// ignore
	}

	TxoID id1 = idBeg;
	Height hLast = 0;

	if (wlkTxo.m_pLa)
		wlkTxo.m_pLa->SetTotal(idEnd - idBeg);

	NodeDB::WalkerTxo wlk;
	for (m_DB.EnumTxos(wlk, id1);  wlk.MoveNext(); )
	{
		if (wlk.m_ID >= id1)
		{
			if (wlk.m_ID >= idEnd)
				break;

			// update height and next boundary
			id1 = FindHeightByTxoID(hLast, wlk.m_ID);
			assert(wlk.m_ID < id1);

			if (wlkTxo.m_pLa &&
				!wlkTxo.m_pLa->OnProgress(wlk.m_ID - idBeg + 1))
				throw std::runtime_error("EnumTxos interrupted");
		}

		if (!wlkTxo.OnTxo(wlk, hLast))
			return false;
	}

	return true;
}

bool NodeProcessor::EnumKernels(IKrnWalker& wlkKrn, Block::NumberRange nr)
{
	if (nr.IsEmpty() || !IsTreasuryHandled())
		return true;
	assert(nr.m_Max.v <= m_Cursor.m_Full.m_Number.v);

	if (wlkKrn.m_pLa)
		wlkKrn.m_pLa->SetTotal(nr.m_Max.v - nr.m_Min.v + 1);

	if (!nr.m_Min.v)
	{
		// treasury
		if (Rules::get().TreasuryChecksum != Zero)
		{
			Treasury::Data td;
			{
				ByteBuffer buf;
				m_DB.ParamGet(NodeDB::ParamID::Treasury, nullptr, nullptr, &buf);

				Deserializer der;
				der.reset(buf);
				der & td;
			}

			// squash the treasury into a single tx
			TxVectors::Full txv;

			for (auto& g : td.m_vGroups)
			{
				if (txv.m_vKernels.empty())
					txv.m_vKernels.swap(g.m_Data.m_vKernels);
				else
				{
					g.m_Data.MoveInto(txv);
					txv.m_vInputs.clear();
					txv.m_vOutputs.clear();
				}
			}

			txv.NormalizeE();

			wlkKrn.m_Height = 0; // treasury
			wlkKrn.m_nKrnIdx = 0;

			NodeDB::StateID sid;
			sid.SetNull();
			if (!wlkKrn.ProcessBlock(sid, txv.m_vKernels))
				return false;

		}

		wlkKrn.m_Height = Rules::HeightGenesis;
		nr.m_Min.v++;
	}

	TxVectors::Eternal txve;

	NodeDB::StateID sid;

	for (sid.m_Number = nr.m_Min; sid.m_Number.v <= nr.m_Max.v; sid.m_Number.v++)
	{
		sid.m_Row = FindActiveAtStrict(sid.m_Number);
		wlkKrn.m_Height = Num2Height(sid);

		txve.m_vKernels.clear();
		ReadKrns(sid.m_Row, txve);

		wlkKrn.m_nKrnIdx = 0;
		if (!wlkKrn.ProcessBlock(sid, txve.m_vKernels))
			return false;

		if (wlkKrn.m_pLa &&
			!wlkKrn.m_pLa->OnProgress(sid.m_Number.v - nr.m_Min.v + 1))
			throw std::runtime_error("EnumKernels interrupted");
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

bool NodeProcessor::ITxoRecover::OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate)
{
	if (TxoIsNaked(wlk.m_Value))
		return true;

	return ITxoWalker::OnTxo(wlk, hCreate);
}

bool NodeProcessor::ITxoRecover::OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp)
{
	assert(m_pKey);
	CoinID cid;
	Output::User user;
	if (!outp.Recover(hCreate, *m_pKey, cid, &user))
		return true;

	return OnTxo(wlk, hCreate, outp, cid, user);
}

bool NodeProcessor::ITxoWalker_UnspentNaked::OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate)
{
	if (wlk.m_SpendHeight != MaxHeight)
		return true;

	uint8_t pNaked[s_TxoNakedMax];
	TxoToNaked(pNaked, Cast::NotConst(wlk).m_Value); // save allocation and deserialization of sig

	return ITxoWalker::OnTxo(wlk, hCreate);
}

bool NodeProcessor::ITxoWalker_Unspent::OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate)
{
	if (wlk.m_SpendHeight != MaxHeight)
		return true;

	return ITxoWalker::OnTxo(wlk, hCreate);
}

void NodeProcessor::InitializeUtxos()
{
	struct Walker
		:public ITxoWalker_UnspentNaked
	{
		NodeProcessor& m_This;
		Walker(NodeProcessor& x) :m_This(x) {}

		virtual bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate) override
		{
			m_This.InitializeUtxosProgress(wlk.m_ID, m_pLa->m_Total);
			return ITxoWalker_UnspentNaked::OnTxo(wlk, hCreate);
		}

		virtual bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp) override
		{
			m_This.m_Extra.m_Txos = wlk.m_ID;
			BlockInterpretCtx bic(hCreate, true);
			if (!m_This.HandleBlockElement(outp, bic))
				OnCorrupted();

			return true;
		}
	};

	LongAction la("Rebuilding mapped image...", 0, m_pExternalHandler);

	Walker wlk(*this);
	wlk.m_pLa = &la;

	EnumTxos(wlk);
}

bool NodeProcessor::GetBlock(const NodeDB::StateID& sid, ByteBuffer* pEthernal, ByteBuffer* pPerishable, Block::Number n0, Block::Number nLo1, Block::Number nHi1, bool bActive)
{
	// h0 - current peer Height
	// hLo1 - HorizonLo that peer needs after the sync
	// hHi1 - HorizonL1 that peer needs after the sync
	if ((nLo1.v > nHi1.v) || (n0.v >= sid.m_Number.v))
		return false;

	// For every output:
	//	if SpendHeight > hHi1 (or null) then fully transfer
	//	if SpendHeight > hLo1 then transfer naked (remove Confidential, Public, Asset::ID)
	//	Otherwise - don't transfer

	// For every input (commitment only):
	//	if SpendHeight > hLo1 then transfer
	//	if CreateHeight <= h0 then transfer
	//	Otherwise - don't transfer

	std::setmax(nHi1.v, sid.m_Number.v); // valid block can't spend its own output. Hence this means full block should be transferred
	std::setmax(nLo1.v, sid.m_Number.v - 1);

	if (m_Extra.m_TxoHi.v > nHi1.v)
		return false;

	if (m_Extra.m_TxoLo.v > nLo1.v)
		return false;

	if (n0.v && (m_Extra.m_TxoLo.v > sid.m_Number.v))
		return false; // we don't have any info for the range [1, n0].

	// in case we're during sync - make sure we don't return non-full blocks as-is
	if (IsFastSync() && (sid.m_Number.v > m_Cursor.m_Full.m_Number.v))
		return false;

	bool bFullBlock = (sid.m_Number.v >= nHi1.v) && (sid.m_Number.v > nLo1.v);
	m_DB.GetStateBlock(sid.m_Row, bFullBlock ? pPerishable : nullptr, pEthernal, nullptr);

	if (!(pPerishable && pPerishable->empty()))
		return true;

	// re-create it from Txos
	if (!bActive && !(m_DB.GetStateFlags(sid.m_Row) & NodeDB::StateFlags::Active))
		return false; // only active states are supported

	TxoID idInpCut = get_TxosBefore(Block::Number(n0.v + 1));
	TxoID id0;

	TxoID id1 = m_DB.get_StateTxos(sid.m_Row);

	ByteBuffer bbBlob;
	TxBase txb;
	ReadOffset(txb.m_Offset, sid.m_Row);

	uint64_t rowid = sid.m_Row;
	if (m_DB.get_Prev(rowid))
	{
		ECC::Scalar offsPrev;
		ReadOffset(offsPrev, rowid);
		AdjustOffset(txb.m_Offset, offsPrev, false);
		id0 = m_DB.get_StateTxos(rowid);
	}
	else
		id0 = m_Extra.m_TxosTreasury;

	Serializer ser;
	ser & txb;

	uint32_t nCount = 0;

	// inputs
	std::vector<NodeDB::StateInput> v;
	m_DB.get_StateInputs(sid.m_Row, v);

	for (uint32_t iCycle = 0; ; iCycle++)
	{
		for (size_t i = 0; i < v.size(); i++)
		{
			TxoID id = v[i].get_ID();

			//	if SpendHeight > hLo1 then transfer
			//	if CreateHeight <= h0 then transfer
			//	Otherwise - don't transfer
			if ((sid.m_Number.v > nLo1.v) || (id < idInpCut))
			{
				if (iCycle)
				{
					const NodeDB::StateInput& si = v[i];

					// write
					Input inp;
					si.Get(inp.m_Commitment);
					ser & inp;
				}
				else
					nCount++;
			}
		}

		if (iCycle)
			break;

		ser & uintBigFrom(nCount);
	}

	nCount = 0;

	// outputs
	Height hLo1 = Num2Height(nLo1);
	Height hHi1 = Num2Height(nHi1);

	NodeDB::WalkerTxo wlk;
	for (m_DB.EnumTxos(wlk, id0); wlk.MoveNext(); )
	{
		if (wlk.m_ID >= id1)
			break;

		//	if SpendHeight > hHi1 (or null) then fully transfer
		//	if SpendHeight > hLo1 then transfer naked (remove Confidential, Public, Asset::ID)
		//	Otherwise - don't transfer

		if (wlk.m_SpendHeight <= hLo1)
			continue;

		uint8_t pNaked[s_TxoNakedMax];

		if (wlk.m_SpendHeight <= hHi1)
			TxoToNaked(pNaked, wlk.m_Value);

		nCount++;

		const uint8_t* p = reinterpret_cast<const uint8_t*>(wlk.m_Value.p);
		bbBlob.insert(bbBlob.end(), p, p + wlk.m_Value.n);
	}

	ser & uintBigFrom(nCount);
	ser.swap_buf(*pPerishable);
	pPerishable->insert(pPerishable->end(), bbBlob.begin(), bbBlob.end());
		
	return true;
}

NodeProcessor::RecentStates::Entry& NodeProcessor::RecentStates::get_FromTail(size_t x) const
{
	assert((x < m_Count) && (m_Count <= m_vec.size()));
	return Cast::NotConst(m_vec[(m_i0 + m_Count - x - 1) % m_vec.size()]);
}

const NodeProcessor::RecentStates::Entry* NodeProcessor::RecentStates::Get(Block::Number num) const
{
	if (!m_Count)
		return nullptr;

	const Entry& e = get_FromTail(0);
	if (num.v > e.m_State.m_Number.v)
		return nullptr;

	auto dn = e.m_State.m_Number.v - num.v;
	if (dn >= m_Count)
		return nullptr;

	const Entry& e2 = get_FromTail(static_cast<size_t>(dn));
	assert(e2.m_State.m_Number.v == num.v);
	return &e2;
}

void NodeProcessor::RecentStates::RollbackTo(Block::Number num)
{
	for (; m_Count; m_Count--)
	{
		const Entry& e = get_FromTail(0);
		if (e.m_State.m_Number.v == num.v)
			break;
	}
}

void NodeProcessor::RecentStates::Push(uint64_t rowID, const Block::SystemState::Full& s)
{
	if (m_vec.empty())
	{
		// we use this cache mainly to improve difficulty calculation. Hence the cache size is appropriate
		const Rules& r = Rules::get();
	
		const size_t n = std::max(r.DA.WindowWork + r.DA.WindowMedian1, r.DA.WindowMedian0) + 5;
		m_vec.resize(n);
	}
	else
	{
		// ensure we don't have out-of-order entries
		RollbackTo(Block::Number(s.m_Number.v - 1));
	}

	if (m_Count < m_vec.size())
		m_Count++;
	else
		m_i0++;

	Entry& e = get_FromTail(0);
	e.m_RowID = rowID;
	e.m_State = s;
}

void NodeProcessor::RebuildNonStd()
{
	Height h0 = Rules::get().pForks[2].m_Height;
	if (m_Cursor.m_hh.m_Height < h0)
		return; // no non-std data

	LongAction la("Rebuilding non-std data...", m_Cursor.m_Full.m_Number.v, m_pExternalHandler);

	// Delete all asset info, contracts, shielded, and replay everything
	m_Mapped.m_Contract.Clear();
	m_DB.ContractDataDelAll();
	m_DB.ContractLogDel(HeightPos(0), HeightPos(MaxHeight));
	m_DB.ShieldedOutpDelFrom(0);
	m_DB.ParamDelSafe(NodeDB::ParamID::ShieldedInputs);
	m_DB.AssetsDelAll(Rules::get().CA.ForeignEnd);
	m_DB.AssetEvtsDeleteFrom(0);
	m_DB.UniqueDeleteAll();
	m_DB.KrnInfoDelFrom(0);

	m_Mmr.m_Assets.ResizeTo(0);
	m_Mmr.m_Shielded.ResizeTo(0);
	m_Extra.m_ShieldedOutputs = 0;

	static_assert(NodeDB::StreamType::StatesMmr == 0);
	m_DB.StreamsDelAll(static_cast<NodeDB::StreamType::Enum>(1), NodeDB::StreamType::count);

	struct KrnWalkerRebuild
		:public IKrnWalker
	{
		NodeProcessor& m_This;
		BlockInterpretCtx* m_pBic = nullptr;
		std::vector<ContractInvokeExtraInfo>* m_pvC = nullptr;
		KrnWalkerRebuild(NodeProcessor& p) :m_This(p) {}

		ByteBuffer m_Rollback;

		virtual bool ProcessBlock(const NodeDB::StateID& sid, const std::vector<TxKernel::Ptr>& v) override
		{
			BlockInterpretCtx bic(m_Height, true);
			m_pBic = &bic;
			BlockInterpretCtx::ChangesFlush cf(m_This);

			m_pBic->m_pvC = m_pvC;
			bic.m_AlreadyValidated = true;
			bic.SetAidMax(m_This);
			bic.m_Rollback.swap(m_Rollback); // optimization
			bic.m_DontUpdatePbft = true;

			Process(v);

			bic.m_Rollback.swap(m_Rollback);
			
			if (sid.m_Number.v > m_This.m_Extra.m_Fossil.v)
			{
				assert(sid.m_Row); // can't be treasury height, since it's above the fossil height
				// replace rollback data
				m_This.m_DB.set_StateRB(sid.m_Row, m_Rollback);
			}

			m_Rollback.clear();

			cf.Do(m_This, m_Height);

			if (m_pvC)
			{
				Serializer ser;
				ser.swap_buf(m_Rollback);

				bic.AddKrnInfo(ser, m_This.m_DB);

				ser.swap_buf(m_Rollback);

				m_Rollback.clear();
				m_pvC->clear();
			}

			return true;
		}

		virtual bool OnKrn(const TxKernel& krn) override
		{
			m_pBic->m_nKrnIdx = m_nKrnIdx;

			if (!m_This.HandleKernelTypeAny(krn, *m_pBic))
				OnCorrupted();

			return true;
		}

	} wlk(*this);

	std::vector<ContractInvokeExtraInfo> vC;
	wlk.m_pvC = m_DB.ParamIntGetDef(NodeDB::ParamID::RichContractInfo) ? &vC : nullptr;

	wlk.m_pLa = &la;

	Block::NumberRange nr;
	nr.m_Min = FindAtivePastHeight(h0);
	nr.m_Max = m_Cursor.m_Full.m_Number;

	EnumKernels(wlk, nr);
}

int NodeProcessor::get_AssetAt(Asset::Full& ai, Height h, bool bFindAid)
{
	assert(h <= m_Cursor.m_hh.m_Height);

	// search for create/destroy
	NodeDB::WalkerAssetEvt wlk;
	if (bFindAid)
		m_DB.AssetEvtsEnumBwd2(wlk, ai.m_ID + Asset::s_MaxCount, h);
	else
		m_DB.AssetEvtsEnumBwd(wlk, ai.m_ID + Asset::s_MaxCount, h);

	if (!wlk.MoveNext())
		return 0; // never existed

	if (!wlk.m_Body.n) // last was destroy
		return -1;

	get_AssetCreateInfo(ai, wlk);

	typedef std::pair<Height, uint64_t> HeightAndIndex;
	HeightAndIndex hiCreate(wlk.m_Height, wlk.m_Index);

	m_DB.AssetEvtsEnumBwd(wlk, ai.m_ID, h);
	if (wlk.MoveNext() && (HeightAndIndex(wlk.m_Height, wlk.m_Index) > hiCreate))
	{
		AssetDataPacked adp;
		adp.set_Strict(wlk.m_Body);

		ai.m_Value = adp.m_Amount;
		adp.m_LockHeight.Export(ai.m_LockHeight);

	}
	else
	{
		// wasn't ever emitted
		ai.m_LockHeight = wlk.m_Height;
		ai.m_Value = Zero;
	}

	return 1;
}

void NodeProcessor::get_AssetCreateInfo(Asset::CreateInfo& ai, const NodeDB::WalkerAssetEvt& wlk)
{
	if (wlk.m_Body.n < sizeof(AssetCreateInfoPacked))
		OnCorrupted();

	auto* pAcip = reinterpret_cast<const AssetCreateInfoPacked*>(wlk.m_Body.p);

	ai.m_Metadata.m_Value.resize(wlk.m_Body.n - sizeof(AssetCreateInfoPacked));
	if (!ai.m_Metadata.m_Value.empty())
		memcpy(&ai.m_Metadata.m_Value.front(), pAcip + 1, ai.m_Metadata.m_Value.size());
	ai.m_Metadata.UpdateHash();

	if (pAcip->m_OwnedByContract)
	{
		ai.SetCid(&pAcip->m_Owner);
		ai.m_Metadata.get_Owner(ai.m_Owner, ai.m_Cid);
	}
	else
	{
		ai.SetCid(nullptr);
		ai.m_Owner = pAcip->m_Owner;

	}

	ai.m_Deposit = Rules::get().get_DepositForCA(wlk.m_Height);
}

void NodeProcessor::ValidatedCache::ShrinkTo(uint32_t n)
{
	while (m_Mru.size() > n)
		Delete(m_Mru.back().get_ParentObj());
}

void NodeProcessor::ValidatedCache::OnShLo(const Entry::ShLo::Type& nShLo)
{
	while (true)
	{
		ShLoSet::reverse_iterator it = m_ShLo.rbegin();
		if (m_ShLo.rend() == it)
			break;
		Entry::ShLo& x = *it;

		if (x.m_End <= nShLo)
			break;

		Delete(x.get_ParentObj());
	}
}

void NodeProcessor::ValidatedCache::RemoveRaw(Entry& x)
{
	m_Keys.erase(KeySet::s_iterator_to(x.m_Key));
	m_ShLo.erase(ShLoSet::s_iterator_to(x.m_ShLo));
	m_Mru.erase(MruList::s_iterator_to(x.m_Mru));
}

void NodeProcessor::ValidatedCache::Delete(Entry& x)
{
	RemoveRaw(x);
	delete &x;
}

void NodeProcessor::ValidatedCache::MoveToFront(Entry& x)
{
	m_Mru.erase(MruList::s_iterator_to(x.m_Mru));
	m_Mru.push_front(x.m_Mru);
}

bool NodeProcessor::ValidatedCache::Find(const Entry::Key::Type& val)
{
	Entry::Key key;
	key.m_Value = val;

	KeySet::iterator it = m_Keys.find(key);
	if (m_Keys.end() == it)
		return false;

	MoveToFront(it->get_ParentObj());
	return true;
}

void NodeProcessor::ValidatedCache::Insert(const Entry::Key::Type& val, const Entry::ShLo::Type& nShLo)
{
	Entry* pEntry(new Entry);
	pEntry->m_Key.m_Value = val;
	pEntry->m_ShLo.m_End = nShLo;

	InsertRaw(*pEntry);
}

void NodeProcessor::ValidatedCache::InsertRaw(Entry& x)
{
	m_Keys.insert(x.m_Key);
	m_ShLo.insert(x.m_ShLo);
	m_Mru.push_front(x.m_Mru);
}

void NodeProcessor::ValidatedCache::MoveInto(ValidatedCache& dst)
{
	while (!m_Mru.empty())
	{
		Entry& x = m_Mru.back().get_ParentObj();
		RemoveRaw(x);
		dst.InsertRaw(x);
	}
}

/////////////////////////////
// Mapped
struct NodeProcessor::Mapped::Type {
	enum Enum {

		UtxoLeaf,
		HashJoint,
		UtxoQueue,
		UtxoNode,
		HashLeaf,

		count
	};
};

bool NodeProcessor::Mapped::Open(const char* sz, const Stamp& s)
{
	// change this when format changes
	static const uint8_t s_pSig[] = {
		0xFB, 0x6A, 0x15, 0x54,
		0x41, 0x7C, 0x4C, 0x3D,
		0x81, 0xD5, 0x9C, 0xD9,
		0x17, 0xCE, 0xA4, 0x92
	};

	MappedFile::Defs d;
	d.m_pSig = s_pSig;
	d.m_nSizeSig = sizeof(s_pSig);
	d.m_nBanks = Type::count;
	d.m_nFixedHdr = sizeof(Hdr);

	m_Mapping.Open(sz, d);

	Hdr& h = get_Hdr();
	if (!h.m_Dirty && (h.m_Stamp == s))
	{
		m_Utxo.m_RootOffset = h.m_RootUtxo;
		m_Contract.m_RootOffset = h.m_RootContract;
		return true;
	}

	m_Mapping.Open(sz, d, true); // reset
	return false;
}

void NodeProcessor::Mapped::Close()
{
	m_Utxo.m_RootOffset = 0; // prevent cleanup
	m_Contract.m_RootOffset = 0;
	m_Mapping.Close();
}

NodeProcessor::Mapped::Hdr& NodeProcessor::Mapped::get_Hdr()
{
	return *static_cast<Hdr*>(m_Mapping.get_FixedHdr());
}

void NodeProcessor::Mapped::FlushStrict(const Stamp& s)
{
	Hdr& h = get_Hdr();
	assert(h.m_Dirty);

	h.m_Dirty = 0;
	h.m_RootUtxo = m_Utxo.m_RootOffset;
	h.m_RootContract = m_Contract.m_RootOffset;
	// TODO: flush

	h.m_Stamp = s;
}

void NodeProcessor::Mapped::Utxo::EnsureReserve()
{
	try
	{
		get_ParentObj().m_Mapping.EnsureReserve(Type::UtxoLeaf, sizeof(MyLeaf), 1);
		get_ParentObj().m_Mapping.EnsureReserve(Type::HashJoint, sizeof(MyJoint), 1);
		get_ParentObj().m_Mapping.EnsureReserve(Type::UtxoQueue, sizeof(MyLeaf::IDQueue), 1);
		get_ParentObj().m_Mapping.EnsureReserve(Type::UtxoNode, sizeof(MyLeaf::IDNode), 1);
	}
	catch (const std::exception& e)
	{
		// promote it
		CorruptionException exc;
		exc.m_sErr = e.what();
		throw exc;
	}
}

void NodeProcessor::Mapped::OnDirty()
{
	get_Hdr().m_Dirty = 1;
}

intptr_t NodeProcessor::Mapped::Utxo::get_Base() const
{
	return reinterpret_cast<intptr_t>(get_ParentObj().m_Mapping.get_Base());
}

RadixTree::Leaf* NodeProcessor::Mapped::Utxo::CreateLeaf()
{
	return get_ParentObj().Allocate<MyLeaf>(Type::UtxoLeaf);
}

void NodeProcessor::Mapped::Utxo::DeleteEmptyLeaf(Leaf* p)
{
	get_ParentObj().m_Mapping.Free(Type::UtxoLeaf, p);
}

RadixTree::Joint* NodeProcessor::Mapped::Utxo::CreateJoint()
{
	return get_ParentObj().Allocate<MyJoint>(Type::HashJoint);
}

void NodeProcessor::Mapped::Utxo::DeleteJoint(Joint* p)
{
	get_ParentObj().m_Mapping.Free(Type::HashJoint, p);
}

UtxoTree::MyLeaf::IDQueue* NodeProcessor::Mapped::Utxo::CreateIDQueue()
{
	return get_ParentObj().Allocate<MyLeaf::IDQueue>(Type::UtxoQueue);
}

void NodeProcessor::Mapped::Utxo::DeleteIDQueue(MyLeaf::IDQueue* p)
{
	get_ParentObj().m_Mapping.Free(Type::UtxoQueue, p);
}

UtxoTree::MyLeaf::IDNode* NodeProcessor::Mapped::Utxo::CreateIDNode()
{
	return get_ParentObj().Allocate<MyLeaf::IDNode>(Type::UtxoNode);
}

void NodeProcessor::Mapped::Utxo::DeleteIDNode(MyLeaf::IDNode* p)
{
	get_ParentObj().m_Mapping.Free(Type::UtxoNode, p);
}

intptr_t NodeProcessor::Mapped::Contract::get_Base() const
{
	return reinterpret_cast<intptr_t>(get_ParentObj().m_Mapping.get_Base());
}

void NodeProcessor::Mapped::Contract::EnsureReserve()
{
	try
	{
		get_ParentObj().m_Mapping.EnsureReserve(Type::HashJoint, sizeof(MyJoint), 1);
		get_ParentObj().m_Mapping.EnsureReserve(Type::HashLeaf, sizeof(MyLeaf), 1);
	}
	catch (const std::exception& e)
	{
		// promote it
		CorruptionException exc;
		exc.m_sErr = e.what();
		throw exc;
	}
}

RadixTree::Leaf* NodeProcessor::Mapped::Contract::CreateLeaf()
{
	return get_ParentObj().Allocate<MyLeaf>(Type::HashLeaf);
}

void NodeProcessor::Mapped::Contract::DeleteLeaf(Leaf* p)
{
	get_ParentObj().m_Mapping.Free(Type::HashLeaf, p);
}

RadixTree::Joint* NodeProcessor::Mapped::Contract::CreateJoint()
{
    static_assert(sizeof(MyJoint) == sizeof(UtxoTree::MyJoint));
	return get_ParentObj().Allocate<MyJoint>(Type::HashJoint);
}

void NodeProcessor::Mapped::Contract::DeleteJoint(Joint* p)
{
	get_ParentObj().m_Mapping.Free(Type::HashJoint, p);
}


} // namespace beam
