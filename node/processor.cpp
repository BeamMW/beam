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
	m_Branching = std::max(m_Branching, Height(1));

	Height r = Rules::get().MaxRollback;

	m_Sync.Hi = std::max(m_Sync.Hi, std::max(r, m_Branching));
	m_Sync.Lo = std::max(m_Sync.Lo, m_Sync.Hi);

	// Some nodes in production have a bug: if (Sync.Lo == Sync.Hi) - the last generated block that they send may be incorrect
	// Workaround: make sure (Sync.Lo > Sync.Hi), at least by 1
	//
	// After HF2 the workaround can be removed
	if ((m_Sync.Lo == m_Sync.Hi) && (m_Sync.Hi < MaxHeight))
		m_Sync.Lo++;

	// though not required, we prefer m_Local to be no less than m_Sync
	m_Local.Hi = std::max(m_Local.Hi, m_Sync.Hi);
	m_Local.Lo = std::max(m_Local.Lo, std::max(m_Local.Hi, m_Sync.Lo));
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
	m_Extra.m_Fossil = m_DB.ParamIntGetDef(NodeDB::ParamID::FossilHeight, Rules::HeightGenesis - 1);
	m_Extra.m_TxoLo = m_DB.ParamIntGetDef(NodeDB::ParamID::HeightTxoLo, Rules::HeightGenesis - 1);
	m_Extra.m_TxoHi = m_DB.ParamIntGetDef(NodeDB::ParamID::HeightTxoHi, Rules::HeightGenesis - 1);

	bool bUpdateChecksum = !m_DB.ParamGet(NodeDB::ParamID::CfgChecksum, NULL, &blob);
	if (!bUpdateChecksum)
	{
		const HeightHash* pFork = Rules::get().FindFork(hv);
		if (&Rules::get().get_LastFork() != pFork)
		{
			if (!pFork)
			{
				std::ostringstream os;
				os << "Data configuration is incompatible: " << hv;
				throw std::runtime_error(os.str());
			}

			NodeDB::StateID sid;
			m_DB.get_Cursor(sid);

			if (sid.m_Height >= pFork[1].m_Height)
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
		LOG_INFO() << "Settings configuration";

		blob = Blob(Rules::get().get_LastFork().m_Hash);
		m_DB.ParamSet(NodeDB::ParamID::CfgChecksum, NULL, &blob);
	}

	ZeroObject(m_SyncData);

	if (sp.m_ResetCursor)
	{
		SaveSyncData();
	}
	else
	{
		blob.p = &m_SyncData;
		blob.n = sizeof(m_SyncData);
		m_DB.ParamGet(NodeDB::ParamID::SyncData, nullptr, &blob);

		LogSyncData();
	}

	m_nSizeUtxoComission = 0;

	if (Rules::get().TreasuryChecksum == Zero)
		m_Extra.m_TxosTreasury = 1; // artificial gap
	else
		m_DB.ParamGet(NodeDB::ParamID::Treasury, &m_Extra.m_TxosTreasury, nullptr, nullptr);

	InitCursor();

	if (InitUtxoMapping(szPath))
	{
		LOG_INFO() << "UTXO image found";
	}
	else
	{
		LOG_INFO() << "Rebuilding UTXO image...";
		InitializeUtxos();
	}

	// final check
	if ((m_Cursor.m_ID.m_Height >= Rules::HeightGenesis) && (m_Cursor.m_ID.m_Height >= m_SyncData.m_TxoLo))
	{
		get_Definition(hv, false);
		if (m_Cursor.m_Full.m_Definition != hv)
		{
			LOG_ERROR() << "Definition mismatch";
			OnCorrupted();
		}
	}

	m_Extra.m_Txos = get_TxosBefore(m_Cursor.m_ID.m_Height + 1);

	m_Horizon.Normalize();

	if (PruneOld())
		Vacuum();

	if (sp.m_ResetCursor)
	{
		RollbackTo(Rules::HeightGenesis - 1);

		m_Extra.m_TxoLo = 0;
		m_Extra.m_TxoHi = 0;
		m_Extra.m_Fossil = 0;
		m_DB.ParamSet(NodeDB::ParamID::HeightTxoLo, &m_Extra.m_TxoLo, nullptr);
		m_DB.ParamSet(NodeDB::ParamID::HeightTxoHi, &m_Extra.m_TxoHi, nullptr);
		m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &m_Extra.m_Fossil, nullptr);

	}
	else
	{
		TryGoUp();
	}
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

bool NodeProcessor::InitUtxoMapping(const char* sz)
{
	// derive UTXO path from db path
	std::string sPath(sz);

	static const char szSufix[] = ".db";
	const size_t nSufix = _countof(szSufix) - 1;

	if ((sPath.size() >= nSufix) && !My_strcmpi(sPath.c_str() + sPath.size() - nSufix, szSufix))
		sPath.resize(sPath.size() - nSufix);

	UtxoTreeMapped::Stamp us;
	Blob blob(us);

	// don't use the saved image if no height: we may contain treasury UTXOs, but no way to verify the contents
	if ((m_Cursor.m_ID.m_Height < Rules::HeightGenesis) || !m_DB.ParamGet(NodeDB::ParamID::UtxoStamp, nullptr, &blob))
	{
		us = 1U;
		us.Negate();
	}

	sPath += "-utxo-image.bin";
	return m_Utxos.Open(sPath.c_str(), us);
}

void NodeProcessor::LogSyncData()
{
	if (!IsFastSync())
		return;

	LOG_INFO() << "Fast-sync mode up to height " << m_SyncData.m_Target.m_Height;
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

NodeProcessor::~NodeProcessor()
{
	if (m_DbTx.IsInProgress())
	{
		try {
			CommitUtxosAndDB();
		} catch (const CorruptionException& e) {
			LOG_ERROR() << "DB Commit failed: %s" << e.m_sErr;
		}
	}
}

void NodeProcessor::CommitUtxosAndDB()
{
	UtxoTreeMapped::Stamp us;

	bool bFlushUtxos = (m_Utxos.IsOpen() && m_Utxos.get_Hdr().m_Dirty);

	if (bFlushUtxos)
	{
		Blob blob(us);

		if (m_DB.ParamGet(NodeDB::ParamID::UtxoStamp, nullptr, &blob)) {
			ECC::Hash::Processor() << us >> us;
		} else {
			ECC::GenRandom(us);
		}

		m_DB.ParamSet(NodeDB::ParamID::UtxoStamp, nullptr, &blob);
	}

	m_DbTx.Commit();

	if (bFlushUtxos)
		m_Utxos.FlushStrict(us);
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
		CommitUtxosAndDB();
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
	assert(IsTreasuryHandled());

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
			IsBigger3(pMaxTarget->m_Height, m_Cursor.m_ID.m_Height, m_Horizon.m_Sync.Hi, m_Horizon.m_Sync.Hi / 2);

		if (bFirstTime)
		{
			// first time target acquisition
			// TODO - verify the headers w.r.t. difficulty and Chainwork
			m_SyncData.m_h0 = pMaxTarget->m_Height - pMaxTarget->m_Rows.size();

			if (pMaxTarget->m_Height > m_Horizon.m_Sync.Lo)
				m_SyncData.m_TxoLo = pMaxTarget->m_Height - m_Horizon.m_Sync.Lo;

			m_SyncData.m_TxoLo = std::max(m_SyncData.m_TxoLo, m_Extra.m_TxoLo);
		}

		// check if the target should be moved fwd
		bool bTrgChange =
			(IsFastSync() || bFirstTime) &&
			IsBigger2(pMaxTarget->m_Height, m_SyncData.m_Target.m_Height, m_Horizon.m_Sync.Hi);

		if (bTrgChange)
		{
			Height hTargetPrev = bFirstTime ? (pMaxTarget->m_Height - pMaxTarget->m_Rows.size()) : m_SyncData.m_Target.m_Height;

			m_SyncData.m_Target.m_Height = pMaxTarget->m_Height - m_Horizon.m_Sync.Hi;
			m_SyncData.m_Target.m_Row = pMaxTarget->m_Rows.at(pMaxTarget->m_Height - m_SyncData.m_Target.m_Height);

			if (m_SyncData.m_TxoLo)
				// ensure no old blocks, which could be generated with incorrect TxLo
				DeleteBlocksInRange(m_SyncData.m_Target, hTargetPrev);

			SaveSyncData();
		}

		if (bFirstTime)
			LogSyncData();
	}

	// request missing data
	for (CongestionCache::TipList::iterator it = m_CongestionCache.m_lstTips.begin(); m_CongestionCache.m_lstTips.end() != it; it++)
	{
		CongestionCache::TipCongestion& x = *it;

		if (!(x.m_bNeedHdrs || (&x == pMaxTarget)))
			continue; // current policy - ask only for blocks with the largest proven (wrt headers) chainwork

		Block::SystemState::ID id;

		NodeDB::StateID sidTrg;
		sidTrg.m_Height = x.m_Height;
		sidTrg.m_Row = x.m_Rows.at(0);

		if (!x.m_bNeedHdrs)
		{
			if (IsFastSync() && !x.IsContained(m_SyncData.m_Target))
				continue; // ignore irrelevant branches

			NodeDB::StateID sid;
			sid.m_Height = x.m_Height - (x.m_Rows.size() - 1);
			sid.m_Row = x.m_Rows.at(x.m_Rows.size() - 1);

			m_DB.get_StateID(sid, id);
			RequestDataInternal(id, sid.m_Row, true, sidTrg);
		}
		else
		{
			uint64_t rowid = x.m_Rows.at(x.m_Rows.size() - 1);

			Block::SystemState::Full s;
			m_DB.get_State(rowid, s);

			id.m_Height = s.m_Height - 1;
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
		assert(pVal->m_Height >= sid.m_Height);
		Height dh = (pVal->m_Height - sid.m_Height);

		if (pVal->m_Rows.size() > nCountExtra + dh)
			return &pVal->m_Rows.at(dh);
	}
	return nullptr;
}

Height NodeProcessor::get_LowestReturnHeight() const
{
	Height hRet = m_Extra.m_TxoHi;

	Height h0 = IsFastSync() ? m_SyncData.m_h0 : m_Cursor.m_ID.m_Height;
	Height hMaxRollback = Rules::get().MaxRollback;

	if (h0 > hMaxRollback)
	{
		h0 -= hMaxRollback;
		hRet = std::max(hRet, h0);
	}

	return hRet;
}

void NodeProcessor::RequestDataInternal(const Block::SystemState::ID& id, uint64_t row, bool bBlock, const NodeDB::StateID& sidTrg)
{
	if (id.m_Height >= get_LowestReturnHeight())
	{
		RequestData(id, bBlock, sidTrg);
	}
	else
	{
		LOG_WARNING() << id << " State unreachable!"; // probably will pollute the log, but it's a critical situation anyway
	}
}

struct NodeProcessor::MultiblockContext
{
	NodeProcessor& m_This;

	std::mutex m_Mutex;

	TxoID m_id0;
	HeightRange m_InProgress;
	PeerID  m_pidLast;

	MultiblockContext(NodeProcessor& np)
		:m_This(np)
	{
		m_InProgress.m_Max = m_This.m_Cursor.m_ID.m_Height;
		m_InProgress.m_Min = m_InProgress.m_Max + 1;
		assert(m_InProgress.IsEmpty());

		m_id0 = m_This.get_TxosBefore(m_This.m_SyncData.m_h0 + 1); // inputs of blocks below TxLo must be before this

		if (m_This.IsFastSync())
			m_Sigma.Import(m_This.m_SyncData.m_Sigma);
	}

	~MultiblockContext()
	{
		m_This.get_TaskProcessor().Flush(0);

		if (m_bBatchDirty)
		{
			// make sure we don't leave batch context is an invalid state
			struct Task0 :public Task {
				virtual void Exec() override
				{
					ECC::InnerProduct::BatchContext* pBc = ECC::InnerProduct::BatchContext::s_pInstance;
					if (pBc)
						pBc->Reset();
				}
			};

			Task0 t;
			m_This.get_TaskProcessor().ExecAll(t);
		}
	}

	ECC::Scalar::Native m_Offset;
	ECC::Point::Native m_Sigma;

	size_t m_SizePending = 0;
	bool m_bFail = false;
	bool m_bBatchDirty = false;

	struct MyTask
		:public NodeProcessor::Task
	{
		virtual void Exec() override;
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
			TxBase::Context::Params m_Pars;
			TxBase::Context m_Ctx;

			SharedBlock(MultiblockContext& mbc)
				:Shared(mbc)
				,m_Ctx(m_Pars)
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

		Task::Processor& tp = m_This.get_TaskProcessor();
		tp.Flush(0);

		if (m_bFail)
			return;

		if (m_bBatchDirty)
		{
			struct Task1 :public Task
			{
				MultiblockContext* m_pMbc;
				virtual void Exec() override
				{
					ECC::InnerProduct::BatchContext* pBc = ECC::InnerProduct::BatchContext::s_pInstance;
					if (pBc && !pBc->Flush())
						m_pMbc->m_bFail = true;
				}
			};

			Task1 t;
			t.m_pMbc = this;
			m_This.get_TaskProcessor().ExecAll(t);

			if (m_bFail)
				return;

			m_bBatchDirty = false;
		}

		if (m_This.IsFastSync())
		{
			if (!(m_Offset == Zero))
			{
				ECC::Mode::Scope scopeFast(ECC::Mode::Fast);

				m_Sigma += ECC::Context::get().G * m_Offset;
				m_Offset = Zero;
			}

			if (m_InProgress.m_Max == m_This.m_SyncData.m_TxoLo)
			{
				// finalize multi-block arithmetics
				TxBase::Context::Params pars;
				pars.m_bBlockMode = true;
				pars.m_bAllowUnsignedOutputs = true; // ignore verification of locked coinbase

				TxBase::Context ctx(pars);
				ctx.m_Height.m_Min = m_This.m_SyncData.m_h0 + 1;
				ctx.m_Height.m_Max = m_This.m_SyncData.m_TxoLo;

				ctx.m_Sigma = m_Sigma;

				if (!ctx.IsValidBlock())
				{
					m_bFail = true;
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

		m_InProgress.m_Min = m_InProgress.m_Max + 1;
	}

	void OnBlock(const PeerID& pid, const MyTask::SharedBlock::Ptr& pShared)
	{
		assert(pShared->m_Ctx.m_Height.m_Min == pShared->m_Ctx.m_Height.m_Max);
		assert(pShared->m_Ctx.m_Height.m_Min == m_This.m_Cursor.m_ID.m_Height + 1);

		if (m_bFail)
			return;

		bool bMustFlush =
			!m_InProgress.IsEmpty() &&
			(
				(m_pidLast != pid) || // PeerID changed
				(m_InProgress.m_Max == m_This.m_SyncData.m_TxoLo) // range complete up to TxLo
			);

		if (bMustFlush && !Flush())
			return;

		m_pidLast = pid;

		const size_t nSizeMax = 1024 * 1024 * 10; // fair enough

		Task::Processor& tp = m_This.get_TaskProcessor();
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
			nTasks = tp.Flush(nTasks - 1);
		}

		m_InProgress.m_Max++;
		assert(m_InProgress.m_Max == pShared->m_Ctx.m_Height.m_Min);

		bool bFull = (pShared->m_Ctx.m_Height.m_Min > m_This.m_SyncData.m_Target.m_Height);

		pShared->m_Pars.m_bAllowUnsignedOutputs = !bFull;
		pShared->m_Pars.m_bVerifyOrder = bFull; // in case of unsigned outputs sometimes order of outputs may look incorrect (duplicated commitment, part of signatures removed)
		pShared->m_Pars.m_bBlockMode = true;
		pShared->m_Pars.m_pAbort = &m_bFail;
		pShared->m_Pars.m_nVerifiers = tp.get_Threads();

		PushTasks(pShared, pShared->m_Pars);
	}

	void PushTasks(const MyTask::Shared::Ptr& pShared, TxBase::Context::Params& pars)
	{
		Task::Processor& tp = m_This.get_TaskProcessor();
		m_bBatchDirty = true;

		pars.m_pAbort = &m_bFail;
		pars.m_nVerifiers = tp.get_Threads();

		for (uint32_t i = 0; i < pars.m_nVerifiers; i++)
		{
			std::unique_ptr<MyTask> pTask(new MyTask);
			pTask->m_pShared = pShared;
			pTask->m_iVerifier = i;
			tp.Push(std::move(pTask));
		}
	}

	void OnFastSyncFailed(bool bDeleteBlocks)
	{
		// rapid rollback
		m_This.RollbackTo(m_This.m_SyncData.m_h0);
		m_InProgress.m_Max = m_This.m_Cursor.m_ID.m_Height;
		m_InProgress.m_Min = m_InProgress.m_Max + 1;

		if (bDeleteBlocks)
			m_This.DeleteBlocksInRange(m_This.m_SyncData.m_Target, m_This.m_SyncData.m_h0);

		m_This.m_SyncData.m_Sigma = Zero;

		if (m_This.m_SyncData.m_TxoLo > m_This.m_SyncData.m_h0)
		{
			LOG_INFO() << "Retrying with lower TxLo";
			m_This.m_SyncData.m_TxoLo = m_This.m_SyncData.m_h0;
		}
		else {
			LOG_WARNING() << "TxLo already low";
		}

		m_This.SaveSyncData();

		m_pidLast = Zero; // don't blame the last peer for the failure!
	}

	void OnFastSyncFailedOnLo()
	{
		// probably problem in lower blocks
		LOG_WARNING() << "Fast-sync failed on first above-TxLo block.";
		m_pidLast = Zero; // don't blame the last peer
		OnFastSyncFailed(true);
	}
};

void NodeProcessor::MultiblockContext::MyTask::Exec()
{
	m_pShared->Exec(m_iVerifier);
}

void NodeProcessor::MultiblockContext::MyTask::SharedBlock::Exec(uint32_t iVerifier)
{
	TxBase::Context ctx(m_Ctx.m_Params);
	ctx.m_Height = m_Ctx.m_Height;
	ctx.m_iVerifier = iVerifier;

	bool bSparse = (m_Ctx.m_Height.m_Min <= m_Mbc.m_This.m_SyncData.m_TxoLo);

	beam::TxBase txbDummy;
	if (bSparse)
		txbDummy.m_Offset = Zero;

	bool bValid = ctx.ValidateAndSummarize(bSparse ? txbDummy : m_Body, m_Body.get_Reader());

	std::unique_lock<std::mutex> scope(m_Mbc.m_Mutex);

	if (bValid)
		bValid = m_Ctx.Merge(ctx);

	assert(m_Done < m_Pars.m_nVerifiers);
	if (++m_Done == m_Pars.m_nVerifiers)
	{
		assert(m_Mbc.m_SizePending >= m_Size);
		m_Mbc.m_SizePending -= m_Size;

		if (bValid && !bSparse)
			bValid = m_Ctx.IsValidBlock();

		if (bValid && bSparse)
		{
			m_Mbc.m_Offset += m_Body.m_Offset;
			m_Mbc.m_Sigma += m_Ctx.m_Sigma;
		}
	}

	if (!bValid)
		m_Mbc.m_bFail = true;
}

void NodeProcessor::TryGoUp()
{
	if (!IsTreasuryHandled())
		return;

	bool bDirty = false;
	uint64_t rowid = m_Cursor.m_Sid.m_Row;

	while (true)
	{
		NodeDB::StateID sidTrg;

		{
			NodeDB::WalkerState ws(m_DB);
			m_DB.EnumFunctionalTips(ws);

			if (!ws.MoveNext())
			{
				assert(!m_Cursor.m_Sid.m_Row);
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
		if (m_Cursor.m_Sid.m_Row != rowid)
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

	RollbackTo(sidTrg.m_Height);

	MultiblockContext mbc(*this);
	bool bContextFail = false, bKeepBlocks = false;

	NodeDB::StateID sidFwd = m_Cursor.m_Sid;

	size_t iPos = vPath.size();
	while (iPos)
	{
		sidFwd.m_Height = m_Cursor.m_Sid.m_Height + 1;
		sidFwd.m_Row = vPath[--iPos];

		if (!HandleBlock(sidFwd, mbc))
		{
			bContextFail = mbc.m_bFail = true;

			if (m_Cursor.m_ID.m_Height + 1 == m_SyncData.m_TxoLo)
				mbc.OnFastSyncFailedOnLo();

			break;
		}

		m_DB.MoveFwd(sidFwd);
		InitCursor();

		if (IsFastSync())
			m_DB.DelStateBlockPP(sidFwd.m_Row); // save space

		if (mbc.m_InProgress.m_Max == m_SyncData.m_Target.m_Height)
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
		LOG_WARNING() << "Context-free verification failed";

	RollbackTo(mbc.m_InProgress.m_Min - 1);

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
			sidFwd.m_Height++;
		}
	}

	LOG_INFO() << "Deleting blocks range: " << (m_Cursor.m_Sid.m_Height + 1) << "-" <<  sidFwd.m_Height;

	DeleteBlocksInRange(sidFwd, m_Cursor.m_Sid.m_Height);
}

void NodeProcessor::OnFastSyncOver(MultiblockContext& mbc, bool& bContextFail)
{
	assert(mbc.m_InProgress.m_Max == m_SyncData.m_Target.m_Height);

	mbc.m_pidLast = Zero; // don't blame the last peer if something goes wrong
	NodeDB::StateID sidFail;
	sidFail.SetNull(); // suppress warning

	{
		// ensure no reduced UTXOs are left
		NodeDB::WalkerTxo wlk(m_DB);
		for (m_DB.EnumTxos(wlk, mbc.m_id0); wlk.MoveNext(); )
		{
			if (wlk.m_SpendHeight != MaxHeight)
				continue;

			if (TxoIsNaked(wlk.m_Value))
			{
				bContextFail = mbc.m_bFail = true;
				m_DB.FindStateByTxoID(sidFail, wlk.m_ID);
				break;
			}
		}
	}

	if (mbc.m_bFail)
	{
		LOG_WARNING() << "Fast-sync failed";

		if (!m_DB.get_Peer(sidFail.m_Row, mbc.m_pidLast))
			mbc.m_pidLast = Zero;

		if (m_SyncData.m_TxoLo > m_SyncData.m_h0)
		{
			mbc.OnFastSyncFailed(true);
		}
		else
		{
			// try to preserve blocks, recover them from the TXOs.

			ByteBuffer bbP, bbE;
			while (m_Cursor.m_Sid.m_Height > m_SyncData.m_h0)
			{
				NodeDB::StateID sid = m_Cursor.m_Sid;

				bbP.clear();
				if (!GetBlock(sid, &bbE, &bbP, m_SyncData.m_h0, m_SyncData.m_TxoLo, m_SyncData.m_Target.m_Height, true))
					OnCorrupted();

				if (sidFail.m_Height == sid.m_Height)
				{
					bbP.clear();
					m_DB.SetStateNotFunctional(sid.m_Row);
				}

				RollbackTo(sid.m_Height - 1);

				m_DB.SetStateBlock(sid.m_Row, bbP, bbE);
				m_DB.set_StateExtra(sid.m_Row, nullptr);
				m_DB.set_StateTxos(sid.m_Row, nullptr);
			}

			mbc.OnFastSyncFailed(false);
		}
	}
	else
	{
		LOG_INFO() << "Fast-sync succeeded";

		// raise fossil height, hTxoLo, hTxoHi
		RaiseFossil(m_Cursor.m_ID.m_Height);
		RaiseTxoHi(m_Cursor.m_ID.m_Height);
		RaiseTxoLo(m_SyncData.m_TxoLo);

		ZeroObject(m_SyncData);
		SaveSyncData();
	}
}

void NodeProcessor::DeleteBlocksInRange(const NodeDB::StateID& sidTop, Height hStop)
{
	for (NodeDB::StateID sid = sidTop; sid.m_Height > hStop; )
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
	m_DB.set_StateExtra(row, nullptr);
	m_DB.set_StateTxos(row, nullptr);
}

Height NodeProcessor::PruneOld()
{
	if (IsFastSync())
		return 0; // don't remove anything while in fast-sync mode

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

	if (IsBigger2(m_Cursor.m_Sid.m_Height, m_Extra.m_Fossil, (Height) Rules::get().MaxRollback))
		hRet += RaiseFossil(m_Cursor.m_Sid.m_Height - Rules::get().MaxRollback);

	if (IsBigger2(m_Cursor.m_Sid.m_Height, m_Extra.m_TxoLo, m_Horizon.m_Local.Lo))
		hRet += RaiseTxoLo(m_Cursor.m_Sid.m_Height - m_Horizon.m_Local.Lo);

	if (IsBigger2(m_Cursor.m_Sid.m_Height, m_Extra.m_TxoHi, m_Horizon.m_Local.Hi))
		hRet += RaiseTxoHi(m_Cursor.m_Sid.m_Height - m_Horizon.m_Local.Hi);

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
				m_DB.DelStateBlockPP(ws.m_Sid.m_Row);
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

	Height hRet = 0;
	std::vector<NodeDB::StateInput> v;

	while (m_Extra.m_TxoLo < hTrg)
	{
		uint64_t rowid = FindActiveAtStrict(++m_Extra.m_TxoLo);
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
	}

	m_Extra.m_TxoLo = hTrg;
	m_DB.ParamSet(NodeDB::ParamID::HeightTxoLo, &m_Extra.m_TxoLo, NULL);

	return hRet;
}

Height NodeProcessor::RaiseTxoHi(Height hTrg)
{
	if (hTrg <= m_Extra.m_TxoHi)
		return 0;

	Height hRet = 0;
	std::vector<NodeDB::StateInput> v;

	NodeDB::WalkerTxo wlk(m_DB);

	while (m_Extra.m_TxoHi < hTrg)
	{
		uint64_t rowid = FindActiveAtStrict(++m_Extra.m_TxoHi);
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
	}

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

uint64_t NodeProcessor::ProcessKrnMmr(Merkle::Mmr& mmr, TxBase::IReader&& r, const Merkle::Hash& idKrn, TxKernel::Ptr* ppRes)
{
	uint64_t iRet = uint64_t (-1);

	for (uint64_t i = 0; r.m_pKernel; r.NextKernel(), i++)
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
	m_DB.GetStateBlock(rowid, nullptr, &bbE);

	TxVectors::Eternal txve;
	TxVectors::Perishable txvp; // dummy

	Deserializer der;
	der.reset(bbE);
	der & txve;

	TxVectors::Reader r(txvp, txve);
	r.Reset();

	Merkle::FixedMmmr mmr;
	mmr.Reset(txve.m_vKernels.size());
	size_t iTrg = ProcessKrnMmr(mmr, std::move(r), idKrn, ppRes);

	if (uint64_t(-1) == iTrg)
		OnCorrupted();

	mmr.get_Proof(proof, iTrg);
	return h;
}

bool NodeProcessor::HandleTreasury(const Blob& blob)
{
	assert(!IsTreasuryHandled());

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
		if (!HandleValidatedTx(td.m_vGroups[iG].m_Data.get_Reader(), 0, true))
		{
			// undo partial changes
			while (iG--)
			{
				if (!HandleValidatedTx(td.m_vGroups[iG].m_Data.get_Reader(), 0, false))
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

	return true;
}

std::ostream& operator << (std::ostream& s, const LogSid& sid)
{
	Block::SystemState::ID id;
	id.m_Height = sid.m_Sid.m_Height;
	sid.m_DB.get_StateHash(sid.m_Sid.m_Row, id.m_Hash);

	s << id;
	return s;
}

bool NodeProcessor::HandleBlock(const NodeDB::StateID& sid, MultiblockContext& mbc)
{
	ByteBuffer bbP, bbE;
	m_DB.GetStateBlock(sid.m_Row, &bbP, &bbE);

	Block::SystemState::Full s;
	m_DB.get_State(sid.m_Row, s); // need it for logging anyway

	MultiblockContext::MyTask::SharedBlock::Ptr pShared = std::make_shared<MultiblockContext::MyTask::SharedBlock>(mbc);
	Block::Body& block = pShared->m_Body;

	try {
		Deserializer der;
		der.reset(bbP);
		der & Cast::Down<Block::BodyBase>(block);
		der & Cast::Down<TxVectors::Perishable>(block);

		der.reset(bbE);
		der & Cast::Down<TxVectors::Eternal>(block);
	}
	catch (const std::exception&) {
		LOG_WARNING() << LogSid(m_DB, sid) << " Block deserialization failed";
		return false;
	}

	std::vector<Merkle::Hash> vKrnID(block.m_vKernels.size()); // allocate mem for all kernel IDs, we need them for initial verification vs header, and at the end - to add to the kernel index.
	// better to allocate the memory, then to calculate IDs twice
	for (size_t i = 0; i < vKrnID.size(); i++)
		block.m_vKernels[i]->get_ID(vKrnID[i]);

	bool bFirstTime = (m_DB.get_StateTxos(sid.m_Row) == MaxHeight);
	if (bFirstTime)
	{
		pShared->m_Size = bbP.size() + bbE.size();
		pShared->m_Ctx.m_Height = sid.m_Height;

		PeerID pid;
		if (!m_DB.get_Peer(sid.m_Row, pid))
			pid = Zero;

		mbc.OnBlock(pid, pShared);

		Difficulty::Raw wrk = m_Cursor.m_Full.m_ChainWork + s.m_PoW.m_Difficulty;

		if (wrk != s.m_ChainWork)
		{
			LOG_WARNING() << LogSid(m_DB, sid) << " Chainwork expected=" << wrk <<", actual=" << s.m_ChainWork;
			return false;
		}

		if (m_Cursor.m_DifficultyNext.m_Packed != s.m_PoW.m_Difficulty.m_Packed)
		{
			LOG_WARNING() << LogSid(m_DB, sid) << " Difficulty expected=" << m_Cursor.m_DifficultyNext << ", actual=" << s.m_PoW.m_Difficulty;
			return false;
		}

		if (s.m_TimeStamp <= get_MovingMedian())
		{
			LOG_WARNING() << LogSid(m_DB, sid) << " Timestamp inconsistent wrt median";
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
			LOG_WARNING() << LogSid(m_DB, sid) << " Kernel commitment mismatch";
			return false;
		}
	}

	bool bOk = HandleValidatedBlock(block.get_Reader(), block, sid.m_Height, true);
	if (!bOk)
		LOG_WARNING() << LogSid(m_DB, sid) << " invalid in its context";

	if (bFirstTime && bOk)
	{
		if (sid.m_Height >= m_SyncData.m_TxoLo)
		{
			// check the validity of state description.
			Merkle::Hash hvDef;
			get_Definition(hvDef, true);

			if (s.m_Definition != hvDef)
			{
				LOG_WARNING() << LogSid(m_DB, sid) << " Header Definition mismatch";
				bOk = false;
			}
		}

		if (sid.m_Height <= m_SyncData.m_TxoLo)
		{
			// make sure no spent txos above the requested h0
			for (size_t i = 0; i < block.m_vInputs.size(); i++)
			{
				if (block.m_vInputs[i]->m_Internal.m_ID >= mbc.m_id0)
				{
					LOG_WARNING() << LogSid(m_DB, sid) << " Invalid input in sparse block";
					bOk = false;
					break;
				}
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
		}
		else
            BEAM_VERIFY(HandleValidatedBlock(block.get_Reader(), block, sid.m_Height, false));
	}

	if (bOk)
	{
		for (size_t i = 0; i < vKrnID.size(); i++)
			m_DB.InsertKernel(vKrnID[i], sid.m_Height);

		std::vector<NodeDB::StateInput> v;
		v.resize(block.m_vInputs.size());

		for (size_t i = 0; i < block.m_vInputs.size(); i++)
		{
			const Input& x = *block.m_vInputs[i];
			m_DB.TxoSetSpent(x.m_Internal.m_ID, sid.m_Height);

			v[i].Set(x.m_Internal.m_ID, x.m_Commitment);
		}

		if (!v.empty())
			m_DB.set_StateInputs(sid.m_Row, &v.front(), v.size());


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

		m_RecentStates.Push(sid.m_Row, s);
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

void NodeProcessor::RecognizeUtxos(TxBase::IReader&& r, Height h)
{
	NodeDB::WalkerEvent wlk(m_DB);

	for ( ; r.m_pUtxoIn; r.NextUtxoIn())
	{
		const Input& x = *r.m_pUtxoIn;
		assert(x.m_Internal.m_Maturity); // must've already been validated

		const UtxoEvent::Key& key = x.m_Commitment;

		m_DB.FindEvents(wlk, Blob(&key, sizeof(key))); // raw find (each time from scratch) is suboptimal, because inputs are sorted, should be a way to utilize this
		if (wlk.MoveNext())
		{
			if (wlk.m_Body.n < sizeof(UtxoEvent::Value))
				OnCorrupted();

			UtxoEvent::Value evt = *reinterpret_cast<const UtxoEvent::Value*>(wlk.m_Body.p); // copy
			evt.m_Maturity = x.m_Internal.m_Maturity;
			evt.m_Added = 0;

			m_DB.InsertEvent(h, Blob(&evt, sizeof(evt)), Blob(&key, sizeof(key)));
			OnUtxoEvent(evt, h);
		}
	}

	for (; r.m_pUtxoOut; r.NextUtxoOut())
	{
		const Output& x = *r.m_pUtxoOut;

		Key::IDV kidv;
		if (Recover(kidv, x, h))
		{
			// filter-out dummies
			if (IsDummy(kidv))
			{
				OnDummy(kidv, h);
				continue;
			}

			// bingo!
			UtxoEvent::Value evt;
			evt.m_Kidv = kidv;
			evt.m_Added = 1;
			evt.m_AssetID = r.m_pUtxoOut->m_AssetID;

			evt.m_Maturity = x.get_MinMaturity(h);

			const UtxoEvent::Key& key = x.m_Commitment;
			m_DB.InsertEvent(h, Blob(&evt, sizeof(evt)), Blob(&key, sizeof(key)));
			OnUtxoEvent(evt, h);
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
			m_This.OnUtxoEvent(evt, hCreate);

			m_Total++;

			if (MaxHeight == wlk.m_SpendHeight)
				m_Unspent++;
			else
			{
				evt.m_Added = 0;
				m_This.get_DB().InsertEvent(wlk.m_SpendHeight, Blob(&evt, sizeof(evt)), Blob(&key, sizeof(key)));
				m_This.OnUtxoEvent(evt, wlk.m_SpendHeight);
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

bool NodeProcessor::HandleValidatedTx(TxBase::IReader&& r, Height h, bool bFwd)
{
	uint32_t nInp = 0, nOut = 0;
	r.Reset();

	for (; r.m_pKernel; r.NextKernel())
	{
		if (!r.m_pKernel->m_pRelativeLock)
			continue;
		const TxKernel::RelativeLock& x = *r.m_pKernel->m_pRelativeLock;

		Height h0 = m_DB.FindKernel(x.m_ID);
		if (h0 < Rules::HeightGenesis)
			return false;

		HeightAdd(h0, x.m_LockHeight);
		if (h0 > h)
			return false;
	}

	bool bOk = true;
	for (; r.m_pUtxoIn; r.NextUtxoIn(), nInp++)
		if (!HandleBlockElement(*r.m_pUtxoIn, h, bFwd))
		{
			bOk = false;
			break;
		}

	if (bOk)
		for (; r.m_pUtxoOut; r.NextUtxoOut(), nOut++)
			if (!HandleBlockElement(*r.m_pUtxoOut, h, bFwd))
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
		HandleBlockElement(*r.m_pUtxoOut, h, false);

	for (; nInp--; r.NextUtxoIn())
		HandleBlockElement(*r.m_pUtxoIn, h, false);

	return false;
}

bool NodeProcessor::HandleValidatedBlock(TxBase::IReader&& r, const Block::BodyBase& body, Height h, bool bFwd)
{
	// make sure we adjust txo count, to prevent the same Txos for consecutive blocks after cut-through
	if (!bFwd)
	{
		assert(m_Extra.m_Txos);
		m_Extra.m_Txos--;
	}

	if (!HandleValidatedTx(std::move(r), h, bFwd))
		return false;

	// currently there's no extra info in the block that's needed

	if (bFwd)
		m_Extra.m_Txos++;

	return true;
}

bool NodeProcessor::HandleBlockElement(const Input& v, Height h, bool bFwd)
{
	m_Utxos.EnsureReserve();

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

		d.m_Maturity = Rules::HeightGenesis - 1;
		kMin = d;
		d.m_Maturity = h - 1;
		kMax = d;

		t.m_pCu = &cu;
		t.m_pBound[0] = kMin.V.m_pData;
		t.m_pBound[1] = kMax.V.m_pData;

		if (m_Utxos.Traverse(t))
			return false;

		p = &Cast::Up<UtxoTree::MyLeaf>(cu.get_Leaf());

		d = p->m_Key;
		assert(d.m_Commitment == v.m_Commitment);
		assert(d.m_Maturity < h);

		TxoID nID = p->m_ID;

		if (!p->IsExt())
			m_Utxos.Delete(cu);
		else
		{
			nID = m_Utxos.PopID(*p);
			cu.InvalidateElement();
			m_Utxos.OnDirty();
		}

		Cast::NotConst(v).m_Internal.m_Maturity = d.m_Maturity;
		Cast::NotConst(v).m_Internal.m_ID = nID;

	} else
	{
		d.m_Maturity = v.m_Internal.m_Maturity;

		bool bCreate = true;
		UtxoTree::Key key;
		key = d;

		p = m_Utxos.Find(cu, key, bCreate);

		if (bCreate)
			p->m_ID = v.m_Internal.m_ID;
		else
		{
			m_Utxos.PushID(v.m_Internal.m_ID, *p);
			cu.InvalidateElement();
			m_Utxos.OnDirty();
		}
	}

	return true;
}

bool NodeProcessor::HandleBlockElement(const Output& v, Height h, bool bFwd)
{
	m_Utxos.EnsureReserve();

	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;
	d.m_Maturity = v.get_MinMaturity(h);

	UtxoTree::Key key;
	key = d;

	UtxoTree::Cursor cu;
	bool bCreate = true;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, key, bCreate);

	cu.InvalidateElement();
	m_Utxos.OnDirty();

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

			m_Utxos.PushID(nID, *p);
		}

		m_Extra.m_Txos++;

	} else
	{
		assert(m_Extra.m_Txos);
		m_Extra.m_Txos--;

		if (!p->IsExt())
			m_Utxos.Delete(cu);
		else
			m_Utxos.PopID(*p);
	}

	return true;
}

void NodeProcessor::ToInputWithMaturity(Input& inp, TxoID id)
{
	// awkward and relatively used, but this is not used frequently.
	// NodeDB::StateInput doesn't contain the maturity of the spent UTXO. Hence we reconstruct it
	// We find the original UTXO height, and then decode the UTXO body, and check its additional maturity factors (coinbase, incubation)

	NodeDB::WalkerTxo wlk(m_DB);
	m_DB.TxoGetValue(wlk, id);

	uint8_t pNaked[s_TxoNakedMax];
	Blob val = wlk.m_Value;
	TxoToNaked(pNaked, val);

	Deserializer der;
	der.reset(val.p, val.n);

	Output outp;
	der & outp;

	inp.m_Commitment = outp.m_Commitment;
	inp.m_Internal.m_ID = id;

	NodeDB::StateID sidPrev;
	m_DB.FindStateByTxoID(sidPrev, id); // relatively heavy operation: search for the original txo height

	inp.m_Internal.m_Maturity = outp.get_MinMaturity(sidPrev.m_Height);
}

void NodeProcessor::RollbackTo(Height h)
{
	assert(h <= m_Cursor.m_Sid.m_Height);
	if (h == m_Cursor.m_Sid.m_Height)
		return;

	TxoID id0 = get_TxosBefore(h + 1);

	// undo inputs
	for (NodeDB::StateID sid = m_Cursor.m_Sid; sid.m_Height > h; )
	{
		std::vector<NodeDB::StateInput> v;
		m_DB.get_StateInputs(sid.m_Row, v);

		for (size_t i = 0; i < v.size(); i++)
		{
			TxoID id = v[i].get_ID();
			if (id >= id0)
				continue; // created and spent within this range - skip it

			Input inp;
			ToInputWithMaturity(inp, id);

			if (!HandleBlockElement(inp, 0, false))
				OnCorrupted();

			m_DB.TxoSetSpent(id, MaxHeight);
		}

		m_DB.set_StateInputs(sid.m_Row, nullptr, 0);

		if (!m_DB.get_Prev(sid))
			ZeroObject(sid);
	}

	// undo outputs
	struct MyWalker
		:public ITxoWalker_UnspentNaked
	{
		NodeProcessor* m_pThis;

		virtual bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp) override
		{
			if (!m_pThis->HandleBlockElement(outp, hCreate, false))
				OnCorrupted();
			return true;
		}
	};

	MyWalker wlk2;
	wlk2.m_pThis = this;
	EnumTxos(wlk2, HeightRange(h + 1, m_Cursor.m_Sid.m_Height));

	m_DB.TxoDelFrom(id0);
	m_DB.DeleteEventsFrom(h + 1);


	// Kernels and cursor
	ByteBuffer bbE;
	TxVectors::Eternal txve;

	for (; m_Cursor.m_Sid.m_Height > h; m_DB.MoveBack(m_Cursor.m_Sid))
	{
		txve.m_vKernels.clear();
		bbE.clear();
		m_DB.GetStateBlock(m_Cursor.m_Sid.m_Row, nullptr, &bbE);

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

	m_RecentStates.RollbackTo(h);

	InitCursor();
	OnRolledBack();

	m_Extra.m_Txos = id0;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnStateInternal(const Block::SystemState::Full& s, Block::SystemState::ID& id, bool bAlreadyChecked)
{
	s.get_ID(id);

	if (!(bAlreadyChecked || s.IsValid()))
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

	if (s.m_Height < get_LowestReturnHeight())
		return DataStatus::Unreachable;

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
		LOG_INFO() << id << " Header accepted";
	}
	
	return ret;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnStateSilent(const Block::SystemState::Full& s, const PeerID& peer, Block::SystemState::ID& id, bool bAlreadyChecked)
{
	DataStatus::Enum ret = OnStateInternal(s, id, bAlreadyChecked);
	if (DataStatus::Accepted == ret)
	{
		uint64_t rowid = m_DB.InsertState(s);
		m_DB.set_Peer(rowid, &peer);
	}

	return ret;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnBlock(const Block::SystemState::ID& id, const Blob& bbP, const Blob& bbE, const PeerID& peer)
{
	NodeDB::StateID sid;
	sid.m_Row = m_DB.StateFindSafe(id);
	if (!sid.m_Row)
	{
		LOG_WARNING() << id << " Block unexpected";
		return DataStatus::Rejected;
	}

	sid.m_Height = id.m_Height;
	return OnBlock(sid, bbP, bbE, peer);
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnBlock(const NodeDB::StateID& sid, const Blob& bbP, const Blob& bbE, const PeerID& peer)
{
	size_t nSize = size_t(bbP.n) + size_t(bbE.n);
	if (nSize > Rules::get().MaxBodySize)
	{
		LOG_WARNING() << LogSid(m_DB, sid) << " Block too large: " << nSize;
		return DataStatus::Invalid;
	}

	if (NodeDB::StateFlags::Functional & m_DB.GetStateFlags(sid.m_Row))
	{
		LOG_WARNING() << LogSid(m_DB, sid) << " Block already received";
		return DataStatus::Rejected;
	}

	if (sid.m_Height < get_LowestReturnHeight())
		return DataStatus::Unreachable;

	m_DB.SetStateBlock(sid.m_Row, bbP, bbE);
	m_DB.SetStateFunctional(sid.m_Row);
	m_DB.set_Peer(sid.m_Row, &peer);

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
	const RecentStates::Entry* pE = m_RecentStates.Get(h);
	if (pE)
		return pE->m_RowID;

	return m_DB.FindActiveStateStrict(h);
}

/////////////////////////////
// Block generation
Difficulty NodeProcessor::get_NextDifficulty()
{
	const Rules& r = Rules::get(); // alias

	if (!m_Cursor.m_Sid.m_Row)
		return r.DA.Difficulty0; // 1st block

	THW thw0, thw1;

	get_MovingMedianEx(m_Cursor.m_Sid.m_Height, r.DA.WindowMedian1, thw1);

	if (m_Cursor.m_Full.m_Height - Rules::HeightGenesis >= r.DA.WindowWork)
	{
		get_MovingMedianEx(m_Cursor.m_Full.m_Height - r.DA.WindowWork, r.DA.WindowMedian1, thw0);
	}
	else
	{
		get_MovingMedianEx(Rules::HeightGenesis, r.DA.WindowMedian1, thw0); // awkward to look for median, since they're immaginary. But makes sure we stick to the same median search and rounding (in case window is even).

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

	// actual dt, only making sure it's non-negative
	uint32_t dtSrc_s = (thw1.first > thw0.first) ? static_cast<uint32_t>(thw1.first - thw0.first) : 0;

	if (m_Cursor.m_Full.m_Height >= r.pForks[1].m_Height)
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
	dtSrc_s = std::min(dtSrc_s, dtTrg_s * 2);
	dtSrc_s = std::max(dtSrc_s, dtTrg_s / 2);


	Difficulty::Raw& dWrk = thw0.second.second;
	dWrk.Negate();
	dWrk += thw1.second.second;

	Difficulty res;
	res.Calculate(dWrk, dh, dtTrg_s, dtSrc_s);

	return res;
}

void NodeProcessor::get_MovingMedianEx(Height hLast, uint32_t nWindow, THW& res)
{
	std::vector<THW> v;
	v.reserve(nWindow);

	assert(hLast >= Rules::HeightGenesis);
	uint64_t rowLast = 0;

	while (v.size() < nWindow)
	{
		v.emplace_back();
		THW& thw = v.back();

		if (hLast >= Rules::HeightGenesis)
		{
			const RecentStates::Entry* pE = m_RecentStates.Get(hLast);

			Block::SystemState::Full sDb;
			if (!pE)
			{
				if (rowLast)
				{
					if (!m_DB.get_Prev(rowLast))
						OnCorrupted();
				}
				else
					rowLast = FindActiveAtStrict(hLast);

				m_DB.get_State(rowLast, sDb);
			}

			const Block::SystemState::Full& s = pE ? pE->m_State : sDb;

			thw.first = s.m_TimeStamp;
			thw.second.first = s.m_Height;
			thw.second.second = s.m_ChainWork;

			hLast--;
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
	get_MovingMedianEx(m_Cursor.m_Sid.m_Height, Rules::get().DA.WindowMedian0, thw);
	return thw.first;
}

bool NodeProcessor::ValidateTxWrtHeight(const Transaction& tx, const HeightRange& hr)
{
	Height h = m_Cursor.m_Sid.m_Height + 1;
	if (!hr.IsInRange(h))
		return false;

	for (size_t i = 0; i < tx.m_vKernels.size(); i++)
	{
		const TxKernel& krn = *tx.m_vKernels[i];
		assert(krn.m_Height.IsInRange(h));

		if (krn.m_pRelativeLock)
		{
			const TxKernel::RelativeLock& x = *krn.m_pRelativeLock;

			Height h0 = m_DB.FindKernel(x.m_ID);
			if (h0 < Rules::HeightGenesis)
				return false;

			HeightAdd(h0, x.m_LockHeight);
			if (h0 > h)
				return false;
		}
	}

	return true;
}

bool NodeProcessor::ValidateTxContext(const Transaction& tx, const HeightRange& hr)
{
	if (!ValidateTxWrtHeight(tx, hr))
		return false;

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
			return false; // some input UTXOs are missing
	}

	return true;
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
	d.m_Maturity = m_Cursor.m_ID.m_Height;
	kMax = d;

	UtxoTree::Cursor cu;
	t.m_pCu = &cu;
	t.m_pBound[0] = kMin.V.m_pData;
	t.m_pBound[1] = kMax.V.m_pData;

	return !m_Utxos.Traverse(t);
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
			if (!HandleBlockElement(*pOutp, h, true))
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

		if (ValidateTxWrtHeight(tx, x.m_Threshold.m_Height) && HandleValidatedTx(tx.get_Reader(), h, true))
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
			if (!HandleBlockElement(*pOutp, h, true))
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

    BEAM_VERIFY(HandleValidatedTx(bc.m_Block.get_Reader(), h, false)); // undo changes

	// reset input maturities
	for (size_t i = 0; i < bc.m_Block.m_vInputs.size(); i++)
		bc.m_Block.m_vInputs[i]->m_Internal.m_Maturity = 0;

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
    BEAM_VERIFY(HandleValidatedTx(bc.m_Block.get_Reader(), h, false)); // undo changes


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

uint32_t NodeProcessor::Task::Processor::get_Threads()
{
	return 1;
}

void NodeProcessor::Task::Processor::Push(Task::Ptr&& pTask)
{
	pTask->Exec();
}

uint32_t NodeProcessor::Task::Processor::Flush(uint32_t)
{
	return 0;
}

void NodeProcessor::Task::Processor::ExecAll(Task& t)
{
	t.Exec();
}

bool NodeProcessor::ValidateAndSummarize(TxBase::Context& ctx, const TxBase& txb, TxBase::IReader&& r)
{
	struct MyShared
		:public MultiblockContext::MyTask::Shared
	{
		TxBase::Context::Params m_Pars;
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
			TxBase::Context ctx(m_Pars);
			ctx.m_Height = m_pCtx->m_Height;
			ctx.m_iVerifier = iThread;

			TxBase::IReader::Ptr pR;
			m_pR->Clone(pR);

			bool bValid = ctx.ValidateAndSummarize(*m_pTx, std::move(*pR));

			ECC::InnerProduct::BatchContext* pBc = ECC::InnerProduct::BatchContext::s_pInstance;
			if (pBc)
			{
				if (bValid)
					bValid = pBc->Flush();

				pBc->Reset();
			}

			std::unique_lock<std::mutex> scope(m_Mbc.m_Mutex);

			if (bValid && !m_Mbc.m_bFail)
				bValid = m_pCtx->Merge(ctx);

			if (!bValid)
				m_Mbc.m_bFail = true;
		}
	};

	MultiblockContext mbc(*this);

	std::shared_ptr<MyShared> pShared = std::make_shared<MyShared>(mbc);

	pShared->m_Pars = ctx.m_Params;
	pShared->m_pCtx = &ctx;
	pShared->m_pTx = &txb;
	pShared->m_pR = &r;

	mbc.m_InProgress.m_Max++; // dummy, just to emulate ongoing progress
	mbc.PushTasks(pShared, pShared->m_Pars);

	return mbc.Flush();
}

bool NodeProcessor::VerifyBlock(const Block::BodyBase& block, TxBase::IReader&& r, const HeightRange& hr)
{
	if ((hr.m_Min < Rules::HeightGenesis) || hr.IsEmpty())
		return false;

	TxBase::Context::Params pars;
	pars.m_bBlockMode = true;
	TxBase::Context ctx(pars);
	ctx.m_Height = hr;

	return
		ValidateAndSummarize(ctx, block, std::move(r)) &&
		ctx.IsValidBlock();
}

bool NodeProcessor::ExtractBlockWithExtra(Block::Body& block, const NodeDB::StateID& sid)
{
	ByteBuffer bbE;
	if (!GetBlockInternal(sid, &bbE, nullptr, 0, 0, 0, false, &block))
		return false;

	Deserializer der;
	der.reset(bbE);
	der & Cast::Down<TxVectors::Eternal>(block);

	// Set maturity to inputs
	for (size_t i = 0; i < block.m_vInputs.size(); i++)
	{
		Input& inp = *block.m_vInputs[i];
		ToInputWithMaturity(inp, inp.m_Internal.m_ID);
	}

	return true;
}

TxoID NodeProcessor::get_TxosBefore(Height h)
{
	if (h < Rules::HeightGenesis)
		return 0;

	if (Rules::HeightGenesis == h)
		return m_Extra.m_TxosTreasury;

	TxoID id = m_DB.get_StateTxos(FindActiveAtStrict(h - 1));
	if (MaxHeight == id)
		OnCorrupted();

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
				id1 = m_Extra.m_TxosTreasury;

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
	if (!m_This.Recover(kidv, outp, hCreate))
		return true;

	return OnTxo(wlk, hCreate, outp, kidv);
}

bool NodeProcessor::Recover(Key::IDV& kidv, const Output& outp, Height h)
{
	struct Walker :public IKeyWalker
	{
		Key::IDV& m_Kidv;
		const Output& m_Outp;
		Height m_Height;

		Walker(Key::IDV& kidv, const Output& outp)
			:m_Kidv(kidv)
			,m_Outp(outp)
		{
		}

		virtual bool OnKey(Key::IPKdf& tag, Key::Index) override
		{
			return !m_Outp.Recover(m_Height, tag, m_Kidv);
		}

	} wlk(kidv, outp);

	wlk.m_Height = h;

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
		TxoID m_TxosTotal;
		NodeProcessor& m_This;
		Walker(NodeProcessor& x) :m_This(x) {}

		virtual bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate) override
		{
			m_This.InitializeUtxosProgress(wlk.m_ID, m_TxosTotal);
			return ITxoWalker_UnspentNaked::OnTxo(wlk, hCreate);
		}

		virtual bool OnTxo(const NodeDB::WalkerTxo& wlk, Height hCreate, Output& outp) override
		{
			m_This.m_Extra.m_Txos = wlk.m_ID;
			if (!m_This.HandleBlockElement(outp, hCreate, true))
				OnCorrupted();

			return true;
		}
	};

	Walker wlk(*this);
	wlk.m_TxosTotal = get_TxosBefore(m_Cursor.m_ID.m_Height + 1);
	EnumTxos(wlk);
}

bool NodeProcessor::GetBlock(const NodeDB::StateID& sid, ByteBuffer* pEthernal, ByteBuffer* pPerishable, Height h0, Height hLo1, Height hHi1, bool bActive)
{
	return GetBlockInternal(sid, pEthernal, pPerishable, h0, hLo1, hHi1, bActive, nullptr);
}

bool NodeProcessor::GetBlockInternal(const NodeDB::StateID& sid, ByteBuffer* pEthernal, ByteBuffer* pPerishable, Height h0, Height hLo1, Height hHi1, bool bActive, Block::Body* pBody)
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

	if ((h0 >= Rules::HeightGenesis) && (m_Extra.m_TxoLo > sid.m_Height))
		return false; // we don't have any info for the range [Rules::HeightGenesis, h0].

	// in case we're during sync - make sure we don't return non-full blocks as-is
	if (IsFastSync() && (sid.m_Height > m_Cursor.m_ID.m_Height))
		return false;

	bool bFullBlock = (sid.m_Height >= hHi1) && (sid.m_Height > hLo1) && !pBody;
	m_DB.GetStateBlock(sid.m_Row, bFullBlock ? pPerishable : nullptr, pEthernal);

	if (!pBody && !(pPerishable && pPerishable->empty()))
		return true;

	// re-create it from Txos
	if (!bActive && !(m_DB.GetStateFlags(sid.m_Row) & NodeDB::StateFlags::Active))
		return false; // only active states are supported

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
		id0 = m_Extra.m_TxosTreasury;

	Serializer ser;
	if (pBody)
		Cast::Down<TxBase>(*pBody) = std::move(txb);
	else
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
			if ((sid.m_Height > hLo1) || (id < idInpCut))
			{
				if (iCycle)
				{
					const NodeDB::StateInput& si = v[i];

					if (pBody)
					{
						Input::Ptr& pInp = pBody->m_vInputs.emplace_back();
						pInp.reset(new Input);
						si.Get(pInp->m_Commitment);
						pInp->m_Internal.m_ID = si.get_ID();
					}
					else
					{
						// write
						Input inp;
						si.Get(inp.m_Commitment);
						ser & inp;
					}
				}
				else
					nCount++;
			}
		}

		if (iCycle)
			break;

		if (pBody)
			pBody->m_vInputs.reserve(nCount);
		else
			ser & uintBigFrom(nCount);
	}

	ByteBuffer bbBlob;
	nCount = 0;

	// outputs
	if (pBody)
		pBody->m_vOutputs.reserve(static_cast<size_t>(id1 - id0 - 1)); // num of original outputs

	NodeDB::WalkerTxo wlk(m_DB);
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

		if (pBody)
		{
			Deserializer der;
			der.reset(wlk.m_Value.p, wlk.m_Value.n);

			Output::Ptr& pOutp = pBody->m_vOutputs.emplace_back();
			pOutp.reset(new Output);
			der & *pOutp;
		}
		else
		{
			nCount++;

			const uint8_t* p = reinterpret_cast<const uint8_t*>(wlk.m_Value.p);
			bbBlob.insert(bbBlob.end(), p, p + wlk.m_Value.n);
		}
	}

	if (!pBody)
	{
		ser & uintBigFrom(nCount);
		ser.swap_buf(*pPerishable);
		pPerishable->insert(pPerishable->end(), bbBlob.begin(), bbBlob.end());
	}

	return true;
}

NodeProcessor::RecentStates::Entry& NodeProcessor::RecentStates::get_FromTail(size_t x) const
{
	assert((x < m_Count) && (m_Count <= m_vec.size()));
	return Cast::NotConst(m_vec[(m_i0 + m_Count - x - 1) % m_vec.size()]);
}

const NodeProcessor::RecentStates::Entry* NodeProcessor::RecentStates::Get(Height h) const
{
	if (!m_Count)
		return nullptr;

	const Entry& e = get_FromTail(0);
	if (h > e.m_State.m_Height)
		return nullptr;

	Height dh = e.m_State.m_Height - h;
	if (dh >= m_Count)
		return nullptr;

	const Entry& e2 = get_FromTail(static_cast<size_t>(dh));
	assert(e2.m_State.m_Height == h);
	return &e2;
}

void NodeProcessor::RecentStates::RollbackTo(Height h)
{
	for (; m_Count; m_Count--)
	{
		const Entry& e = get_FromTail(0);
		if (e.m_State.m_Height == h)
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
		RollbackTo(s.m_Height - 1);
	}

	if (m_Count < m_vec.size())
		m_Count++;
	else
		m_i0++;

	Entry& e = get_FromTail(0);
	e.m_RowID = rowID;
	e.m_State = s;
}

} // namespace beam
