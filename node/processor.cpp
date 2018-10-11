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
#include "../utility/serialize.h"
#include "../core/serialization_adapters.h"
#include "../utility/logger.h"
#include "../utility/logger_checkpoints.h"

namespace beam {

void NodeProcessor::OnCorrupted()
{
	throw std::runtime_error("node data corrupted");
}

NodeProcessor::Horizon::Horizon()
	:m_Branching(Height(-1))
	,m_Schwarzschild(Height(-1))
{
}

void NodeProcessor::Initialize(const char* szPath)
{
	m_DB.Open(szPath);

	Merkle::Hash hv;
	NodeDB::Blob blob(hv);

	if (!m_DB.ParamGet(NodeDB::ParamID::CfgChecksum, NULL, &blob))
	{
		blob = NodeDB::Blob(Rules::get().Checksum);
		m_DB.ParamSet(NodeDB::ParamID::CfgChecksum, NULL, &blob);
	}
	else
		if (hv != Rules::get().Checksum)
		{
			std::ostringstream os;
			os << "Data configuration is incompatible: " << hv << ". Current configuration: " << Rules::get().Checksum;
			throw std::runtime_error(os.str());
		}

	InitCursor();

	InitializeFromBlocks();

	m_Horizon.m_Schwarzschild = std::max(m_Horizon.m_Schwarzschild, m_Horizon.m_Branching);
	m_Horizon.m_Schwarzschild = std::max(m_Horizon.m_Schwarzschild, (Height) Rules::get().MaxRollbackHeight);

	NodeDB::Transaction t(m_DB);
	TryGoUp();
	t.Commit();
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

		m_Cursor.m_LoHorizon = m_DB.ParamIntGetDef(NodeDB::ParamID::LoHorizon);
	}
	else
		ZeroObject(m_Cursor);

	m_Cursor.m_DifficultyNext = get_NextDifficulty();
	m_Cursor.m_SubsidyOpen = 0 != (m_DB.ParamIntGetDef(NodeDB::ParamID::SubsidyOpen, 1));
}

void NodeProcessor::EnumCongestions()
{
	// request all potentially missing data
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

		bool bBlock = true;

		while (sid.m_Height > Rules::HeightGenesis)
		{
			NodeDB::StateID sidThis = sid;
			if (!m_DB.get_Prev(sid))
			{
				bBlock = false;
				break;
			}

			if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(sid.m_Row))
			{
				sid = sidThis;
				break;
			}
		}

		Block::SystemState::ID id;

		if (bBlock)
			m_DB.get_StateID(sid, id);
		else
		{
			Block::SystemState::Full s;
			m_DB.get_State(sid.m_Row, s);

			id.m_Height = s.m_Height - 1;
			id.m_Hash = s.m_Prev;
		}

		if (id.m_Height >= m_Cursor.m_LoHorizon)
		{
			PeerID peer;
			bool bPeer = m_DB.get_Peer(sid.m_Row, peer);

			RequestData(id, bBlock, bPeer ? &peer : NULL);
		}
		else
		{
			LOG_WARNING() << id << " State unreachable!"; // probably will pollute the log, but it's a critical situation anyway
		}
	}
}

void NodeProcessor::TryGoUp()
{
	bool bDirty = false;

	while (true)
	{
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

		// Calculate the path
		std::vector<uint64_t> vPath;
		while (sidTrg.m_Row != m_Cursor.m_Sid.m_Row)
		{
			if (m_Cursor.m_Full.m_ChainWork > wrkTrg)
			{
				Rollback();
				bDirty = true;
			}
			else
			{
				assert(sidTrg.m_Row);
				vPath.push_back(sidTrg.m_Row);

				if (m_DB.get_Prev(sidTrg))
					m_DB.get_ChainWork(sidTrg.m_Row, wrkTrg);
				else
				{
					sidTrg.SetNull();
					wrkTrg = Zero;
				}
			}
		}

		bool bPathOk = true;

		for (size_t i = vPath.size(); i--; )
		{
			bDirty = true;
			if (!GoForward(vPath[i]))
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
		OnNewState();
	}
}

void NodeProcessor::PruneOld()
{
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
			} while (rowid);
		}
	}

	if (m_Cursor.m_Sid.m_Height > m_Horizon.m_Schwarzschild + Rules::HeightGenesis - 1)
	{
		Height h = m_Cursor.m_Sid.m_Height - m_Horizon.m_Schwarzschild;

		if (h > m_Cursor.m_LoHorizon)
			h = m_Cursor.m_LoHorizon;

		AdjustFossilEnd(h);

		for (Height hFossil = m_DB.ParamIntGetDef(NodeDB::ParamID::FossilHeight, Rules::HeightGenesis - 1); ; )
		{
			if (++hFossil >= h)
				break;

			PruneAt(hFossil, true);
			m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &hFossil, NULL);
		}
	}
}

void NodeProcessor::PruneAt(Height h, bool bDeleteBody)
{
	NodeDB::WalkerState ws(m_DB);
	;
	for (m_DB.EnumStatesAt(ws, h); ws.MoveNext(); )
	{
		if (!(NodeDB::StateFlags::Active & m_DB.GetStateFlags(ws.m_Sid.m_Row)))
			m_DB.SetStateNotFunctional(ws.m_Sid.m_Row);

		if (bDeleteBody)
		{
			m_DB.DelStateBlock(ws.m_Sid.m_Row);
			m_DB.set_Peer(ws.m_Sid.m_Row, NULL);
		}
	}
}

void NodeProcessor::get_CurrentLive(Merkle::Hash& hv)
{
	m_Utxos.get_Hash(hv);

	Merkle::Hash hv2;
	m_Kernels.get_Hash(hv2);

	Merkle::Interpret(hv, hv2, true);
}

void NodeProcessor::get_Definition(Merkle::Hash& hv, const Merkle::Hash& hvHist)
{
	get_CurrentLive(hv);
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
	};

	ByteBuffer m_Buf;
	uint32_t m_Inputs;

	RollbackData() :m_Inputs(0) {}

	Utxo& NextInput(bool bWrite)
	{
		size_t nSize = (m_Inputs + 1) * sizeof(Utxo);
		if (nSize > m_Buf.size())
		{
			if (!bWrite)
				OnCorrupted();
			m_Buf.resize(nSize);
		}

		return ((Utxo*) &m_Buf.at(0))[m_Inputs++];
	}
};

bool NodeProcessor::HandleBlock(const NodeDB::StateID& sid, bool bFwd)
{
	ByteBuffer bb;
	RollbackData rbData;
	m_DB.GetStateBlock(sid.m_Row, bb, rbData.m_Buf);

	Block::SystemState::Full s;
	m_DB.get_State(sid.m_Row, s); // need it for logging anyway

	Block::SystemState::ID id;
	s.get_ID(id);

	Block::Body block;
	try {

		Deserializer der;
		der.reset(bb.empty() ? NULL : &bb.at(0), bb.size());
		der & block;
	}
	catch (const std::exception&) {
		LOG_WARNING() << id << " Block deserialization failed";
		return false;
	}

	bb.clear();

	bool bFirstTime = false;

	if (bFwd)
	{
		if (rbData.m_Buf.empty())
		{
			bFirstTime = true;

			Difficulty::Raw wrk;
			s.m_PoW.m_Difficulty.Inc(wrk, m_Cursor.m_Full.m_ChainWork);

			if (wrk != s.m_ChainWork)
			{
				LOG_WARNING() << id << " Chainwork expected=" << wrk <<", actual=" << s.m_ChainWork;
				return false;
			}

			if (!m_Cursor.m_SubsidyOpen && block.m_SubsidyClosing)
			{
				LOG_WARNING() << id << " illegal subsidy-close flag";
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

			if (!VerifyBlock(block, block.get_Reader(), sid.m_Height))
			{
				LOG_WARNING() << id << " context-free verification failed";
				return false;
			}
		}
	} else
		assert(!rbData.m_Buf.empty());


	bool bOk = HandleValidatedTx(block.get_Reader(), sid.m_Height, bFwd, rbData);
	if (!bOk)
		LOG_WARNING() << id << " invalid in its context";

	if (block.m_SubsidyClosing)
		OnSubsidyOptionChanged(!bFwd);

	if (bFirstTime && bOk)
	{
		// check the validity of state description.
		Merkle::Hash hvDef;
		get_Definition(hvDef, true);

		if (s.m_Definition != hvDef)
		{
			LOG_WARNING() << id << " Header Definition mismatch";
			bOk = false;
		}

		if (bOk)
		{
			if (rbData.m_Inputs)
				m_DB.SetStateRollback(sid.m_Row, NodeDB::Blob(&rbData.m_Buf.at(0), sizeof(RollbackData::Utxo) * rbData.m_Inputs));
			else
			{
				// make sure it's not empty, even if there were no inputs, this is how we distinguish processed blocks.
				uint8_t zero = 0;
				m_DB.SetStateRollback(sid.m_Row, NodeDB::Blob(&zero, 1));

			}

			assert(m_Cursor.m_LoHorizon <= m_Cursor.m_Sid.m_Height);
			if (m_Cursor.m_Sid.m_Height - m_Cursor.m_LoHorizon > Rules::get().MaxRollbackHeight)
			{
				m_Cursor.m_LoHorizon = m_Cursor.m_Sid.m_Height - Rules::get().MaxRollbackHeight;
				m_DB.ParamSet(NodeDB::ParamID::LoHorizon, &m_Cursor.m_LoHorizon, NULL);
			}

		}
		else
		{
			if (block.m_SubsidyClosing)
			{
				assert(bFwd);
				OnSubsidyOptionChanged(bFwd);
			}

			rbData.m_Inputs = 0;
			verify(HandleValidatedTx(block.get_Reader(), sid.m_Height, false, rbData));
		}
	}

	if (bOk)
	{
		AdjustCumulativeParams(block, bFwd);
		LOG_INFO() << id << " Block interpreted. Fwd=" << bFwd;
	}

	return bOk;
}

void NodeProcessor::AdjustCumulativeParams(const Block::BodyBase& block, bool bFwd)
{
	ECC::Scalar kOffset;
	NodeDB::Blob blob(kOffset.m_Value);

	if (!m_DB.ParamGet(NodeDB::ParamID::StateExtra, NULL, &blob))
		kOffset.m_Value = Zero;

	ECC::Scalar::Native k(kOffset), k2(block.m_Offset);
	if (!bFwd)
		k2 = -k2;

	k += k2;
	kOffset = k;

	m_DB.ParamSet(NodeDB::ParamID::StateExtra, NULL, &blob);

	AmountBig subsidy;
	subsidy.Lo = m_DB.ParamIntGetDef(NodeDB::ParamID::SubsidyLo);
	subsidy.Hi = m_DB.ParamIntGetDef(NodeDB::ParamID::SubsidyHi);

	if (bFwd)
		subsidy += block.m_Subsidy;
	else
		subsidy -= block.m_Subsidy;

	m_DB.ParamSet(NodeDB::ParamID::SubsidyLo, &subsidy.Lo, NULL);
	m_DB.ParamSet(NodeDB::ParamID::SubsidyHi, &subsidy.Hi, NULL);

	if (block.m_SubsidyClosing)
	{
		uint64_t nVal = !bFwd;
		m_DB.ParamSet(NodeDB::ParamID::SubsidyOpen, &nVal, NULL);
	}
}

bool NodeProcessor::HandleValidatedTx(TxBase::IReader&& r, Height h, bool bFwd, RollbackData& rbData, const Height* pHMax)
{
	uint32_t nInp = 0, nOut = 0, nKrnInp = 0, nKrnOut = 0;
	r.Reset();

	bool bOk = true;
	for (; r.m_pUtxoIn; r.NextUtxoIn(), nInp++)
		if (!HandleBlockElement(*r.m_pUtxoIn, h, pHMax, bFwd, rbData))
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
		for (; r.m_pKernelIn; r.NextKernelIn(), nKrnInp++)
			if (!HandleBlockElement(*r.m_pKernelIn, bFwd, true))
			{
				bOk = false;
				break;
			}

	if (bOk)
		for (; r.m_pKernelOut; r.NextKernelOut(), nKrnOut++)
			if (!HandleBlockElement(*r.m_pKernelOut, bFwd, false))
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

	for (; nKrnOut--; r.NextKernelOut())
		HandleBlockElement(*r.m_pKernelOut, false, false);

	for (; nKrnInp--; r.NextKernelIn())
		HandleBlockElement(*r.m_pKernelIn, false, true);

	for (; nOut--; r.NextUtxoOut())
		HandleBlockElement(*r.m_pUtxoOut, h, pHMax, false);

	rbData.m_Inputs -= nInp;
	uint32_t n = rbData.m_Inputs;

	for (; nInp--; r.NextUtxoIn())
		HandleBlockElement(*r.m_pUtxoIn, h, pHMax, false, rbData);

	rbData.m_Inputs = n;

	return false;
}

bool NodeProcessor::HandleBlockElement(const Input& v, Height h, const Height* pHMax, bool bFwd, RollbackData& rbData)
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

		if (v.m_Maturity >= Rules::HeightGenesis)
		{
			if (!pHMax)
				return false; // explicit maturity allowed only in macroblocks

			if (v.m_Maturity > *pHMax)
				return false;
		}

		d.m_Maturity = v.m_Maturity;
		kMin = d;
		d.m_Maturity = pHMax ? *pHMax : h;
		kMax = d;

		t.m_pCu = &cu;
		t.m_pBound[0] = kMin.m_pArr;
		t.m_pBound[1] = kMax.m_pArr;

		if (m_Utxos.Traverse(t))
			return false;

		p = &(UtxoTree::MyLeaf&) cu.get_Leaf();

		d = p->m_Key;
		assert(d.m_Commitment == v.m_Commitment);
		assert(d.m_Maturity <= (pHMax ? *pHMax : h));

		assert(p->m_Value.m_Count); // we don't store zeroes

		if (!--p->m_Value.m_Count)
			m_Utxos.Delete(cu);
		else
			cu.Invalidate();

		rbData.NextInput(true).m_Maturity = d.m_Maturity;

	} else
	{
		d.m_Maturity = rbData.NextInput(false).m_Maturity;

		bool bCreate = true;
		UtxoTree::Key key;
		key = d;

		p = m_Utxos.Find(cu, key, bCreate);

		if (bCreate)
			p->m_Value.m_Count = 1;
		else
		{
			p->m_Value.m_Count++;
			cu.Invalidate();
		}
	}

	return true;
}

bool NodeProcessor::HandleBlockElement(const Output& v, Height h, const Height* pHMax, bool bFwd)
{
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;
	d.m_Maturity = v.get_MinMaturity(h);

	if (v.m_Maturity >= Rules::HeightGenesis)
	{
		if (!pHMax)
			return false; // maturity forgery isn't allowed
		if (v.m_Maturity < d.m_Maturity)
			return false; // decrease not allowed

		d.m_Maturity = v.m_Maturity;
	}

	UtxoTree::Key key;
	key = d;

	UtxoTree::Cursor cu;
	bool bCreate = true;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, key, bCreate);

	cu.Invalidate();

	if (bFwd)
	{
		if (bCreate)
			p->m_Value.m_Count = 1;
		else
		{
			// protect again overflow attacks, though it's highly unlikely (Input::Count is currently limited to 32 bits, it'd take millions of blocks)
			Input::Count nCountInc = p->m_Value.m_Count + 1;
			if (!nCountInc)
				return false;

			p->m_Value.m_Count = nCountInc;
		}
	} else
	{
		if (1 == p->m_Value.m_Count)
			m_Utxos.Delete(cu);
		else
			p->m_Value.m_Count--;
	}

	return true;
}

void NodeProcessor::OnSubsidyOptionChanged(bool bOpen)
{
	bool bAdd = !bOpen;

	Merkle::Hash hv(Zero);

	RadixHashOnlyTree::Cursor cu;
	bool bCreate = true;
	m_Kernels.Find(cu, hv, bCreate);

	assert(bAdd == bCreate);
	if (!bAdd)
		m_Kernels.Delete(cu);
}

bool NodeProcessor::HandleBlockElement(const TxKernel& v, bool bFwd, bool bIsInput)
{
	bool bAdd = (bFwd != bIsInput);

	Merkle::Hash key;
	v.get_ID(key);

	RadixHashOnlyTree::Cursor cu;
	bool bCreate = bAdd;
	RadixHashOnlyTree::MyLeaf* p = m_Kernels.Find(cu, key, bCreate);

	if (bAdd)
	{
		if (!bCreate)
			return false; // attempt to use the same exactly kernel twice. This should be banned!
	} else
	{
		if (!p)
			return false; // no such a kernel

		m_Kernels.Delete(cu);
	}

	return true;
}

bool NodeProcessor::GoForward(uint64_t row)
{
	NodeDB::StateID sid;
	sid.m_Height = m_Cursor.m_Sid.m_Height + 1;
	sid.m_Row = row;

	if (HandleBlock(sid, true))
	{
		m_DB.MoveFwd(sid);
		InitCursor();
		return true;
	}

	m_DB.DelStateBlock(row);
	m_DB.SetStateNotFunctional(row);

	PeerID peer;
	if (m_DB.get_Peer(row, peer))
	{
		m_DB.set_Peer(row, NULL);
		OnPeerInsane(peer);
	}

	return false;
}

void NodeProcessor::Rollback()
{
	NodeDB::StateID sid = m_Cursor.m_Sid;
	m_DB.MoveBack(m_Cursor.m_Sid);
	InitCursor();

	if (!HandleBlock(sid, false))
		OnCorrupted();

	InitCursor(); // needed to refresh subsidy-open flag. Otherwise isn't necessary

	OnRolledBack();
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnStateInternal(const Block::SystemState::Full& s, Block::SystemState::ID& id)
{
	s.get_ID(id);

	if (!s.IsSane())
	{
		LOG_WARNING() << id << " header insane!";
		return DataStatus::Invalid;
	}

	if (!s.IsValidPoW())
	{
		LOG_WARNING() << id << " PoW invalid";
		return DataStatus::Invalid;
	}

	Timestamp ts = getTimestamp();
	if (s.m_TimeStamp > ts)
	{
		ts = s.m_TimeStamp - ts; // dt
		if (ts > Rules::get().TimestampAheadThreshold_s)
		{
			LOG_WARNING() << id << " Timestamp ahead by " << ts;
			return DataStatus::Invalid;
		}
	}

	if (!ApproveState(id))
	{
		LOG_WARNING() << "State " << id << " not approved";
		return DataStatus::Invalid;
	}

	if (s.m_Height < m_Cursor.m_LoHorizon)
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
		NodeDB::Transaction t(m_DB);
		uint64_t rowid = m_DB.InsertState(s);
		m_DB.set_Peer(rowid, &peer);
		t.Commit();

		LOG_INFO() << id << " Header accepted";
	}
	OnStateData();
	return ret;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnBlock(const Block::SystemState::ID& id, const NodeDB::Blob& block, const PeerID& peer)
{
	OnBlockData();
	if (block.n > Rules::get().MaxBodySize)
	{
		LOG_WARNING() << id << " Block too large: " << block.n;
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

	if (id.m_Height < m_Cursor.m_LoHorizon)
		return DataStatus::Unreachable;

	LOG_INFO() << id << " Block received";

	NodeDB::Transaction t(m_DB);

	m_DB.SetStateBlock(rowid, block);
	m_DB.SetStateFunctional(rowid);
	m_DB.set_Peer(rowid, &peer);

	if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(rowid))
		TryGoUp();

	t.Commit();

	return DataStatus::Accepted;
}

bool NodeProcessor::IsRemoteTipNeeded(const Block::SystemState::Full& sTipRemote, const Block::SystemState::Full& sTipMy)
{
	int n = sTipMy.m_ChainWork.cmp(sTipRemote.m_ChainWork);
	if (n > 0)
		return false;
	if (n < 0)
		return true;

	return sTipMy.m_Definition != sTipRemote.m_Definition;
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
	if (!m_Cursor.m_Sid.m_Row)
		return Rules::get().StartDifficulty; // 1st block difficulty 0

	Height dh = m_Cursor.m_Full.m_Height - Rules::HeightGenesis;

	if (!dh || (dh % Rules::get().DifficultyReviewCycle))
		return m_Cursor.m_Full.m_PoW.m_Difficulty; // no change

	// review the difficulty
	uint64_t rowid = FindActiveAtStrict(m_Cursor.m_Full.m_Height - Rules::get().DifficultyReviewCycle);

	Block::SystemState::Full s2;
	m_DB.get_State(rowid, s2);

	Difficulty ret = m_Cursor.m_Full.m_PoW.m_Difficulty;
	Rules::get().AdjustDifficulty(ret, s2.m_TimeStamp, m_Cursor.m_Full.m_TimeStamp);
	return ret;
}

Timestamp NodeProcessor::get_MovingMedian()
{
	if (!m_Cursor.m_Sid.m_Row)
		return 0;

	std::vector<Timestamp> vTs;

	for (uint64_t row = m_Cursor.m_Sid.m_Row; ; )
	{
		Block::SystemState::Full s;
		m_DB.get_State(row, s);
		vTs.push_back(s.m_TimeStamp);

		if (vTs.size() >= Rules::get().WindowForMedian)
			break;

		if (!m_DB.get_Prev(row))
			break;
	}

	std::sort(vTs.begin(), vTs.end()); // there's a better algorithm to find a median (or whatever order), however our array isn't too big, so it's ok.

	return vTs[vTs.size() >> 1];
}

void NodeProcessor::DeriveKeys(const ECC::Kdf& kdf, Height h, Amount fees, ECC::Scalar::Native& kCoinbase, ECC::Scalar::Native& kFee, ECC::Scalar::Native& kKernel, ECC::Scalar::Native& kOffset)
{
	DeriveKey(kCoinbase, kdf, h, KeyType::Coinbase);

	kKernel = kCoinbase;

	if (fees)
	{
		DeriveKey(kFee, kdf, h, KeyType::Comission);
		kKernel += kFee;
	}
	else
		kFee = Zero;

	kKernel = -kKernel;

	ECC::Scalar::Native k2;
	ExtractOffset(kKernel, k2, h);

	kOffset += k2;
}

bool NodeProcessor::ValidateTxWrtHeight(const Transaction& tx, Height h)
{
	for (size_t i = 0; i < tx.m_vKernelsOutput.size(); i++)
		if (!tx.m_vKernelsOutput[i]->m_Height.IsInRange(h))
			return false;

	return true;
}

bool NodeProcessor::ValidateTxContextKernels(const std::vector<TxKernel::Ptr>& vec, bool bInp)
{
	Merkle::Hash phv[2];
	phv[1] = Zero; // forbidden value for kernel ID

	for (size_t i = 0; i < vec.size(); i++)
	{
		const TxKernel& v = *vec[i];
		v.get_ID(phv[1 & i]);

		if (phv[0] == phv[1])
			return false; // consequent kernels have the same ID
		// We don't check if non-consequent kernels have the same ID. Too low probability, and this is supposed to be a fast verification

		RadixHashOnlyTree::Cursor cu;
		bool bCreate = false;
		RadixHashOnlyTree::MyLeaf* p = m_Kernels.Find(cu, phv[1 & i], bCreate);

		if (bInp != (NULL != p))
			return false;
	}

	return true;
}

bool NodeProcessor::ValidateTxContext(const Transaction& tx)
{
	Height h = m_Cursor.m_Sid.m_Height + 1;
	if (!ValidateTxWrtHeight(tx, h))
		return false;

	// Cheap tx verification. No need to update the internal structure, recalculate definition, or etc.
	// Ensure input UTXOs are present
	for (size_t i = 0; i < tx.m_vInputs.size(); i++)
	{
		struct Traveler :public UtxoTree::ITraveler
		{
			uint32_t m_Count;
			virtual bool OnLeaf(const RadixTree::Leaf& x) override
			{
				const UtxoTree::MyLeaf& n = (UtxoTree::MyLeaf&) x;
				assert(m_Count && n.m_Value.m_Count);
				if (m_Count <= n.m_Value.m_Count)
					return false; // stop iteration

				m_Count -= n.m_Value.m_Count;
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

	// kernels
	return
		ValidateTxContextKernels(tx.m_vKernelsOutput, false) &&
		ValidateTxContextKernels(tx.m_vKernelsInput, false);
}

bool NodeProcessor::GenerateNewBlock(TxPool::Fluff& txp, Block::SystemState::Full& s, Block::Body& res, Amount& fees, Height h, RollbackData& rbData)
{
	fees = 0;
	size_t nBlockSize = 0;
	size_t nAmount = 0;

	// due to (potential) inaccuracy in the block size estimation, our rough estimate - take no more than 95% of allowed block size, minus potential UTXOs to consume fees and coinbase.
	const size_t nRoughExtra = sizeof(ECC::Point) * 2 + sizeof(ECC::RangeProof::Confidential) + sizeof(ECC::RangeProof::Public) + 300;
	const size_t nSizeThreshold = Rules::get().MaxBodySize * 95 / 100 - nRoughExtra;

	ECC::Scalar::Native offset = res.m_Offset;

	for (TxPool::Fluff::ProfitSet::iterator it = txp.m_setProfit.begin(); txp.m_setProfit.end() != it; )
	{
		TxPool::Fluff::Element& x = (it++)->get_ParentObj();

		if (x.m_Profit.m_nSize > nSizeThreshold)
		{
			LOG_INFO() << "Tx is very big. It's deleted.";
			txp.Delete(x);
			continue;
		}

		if (nBlockSize + x.m_Profit.m_nSize > nSizeThreshold)
			continue;
			//break;

		Transaction& tx = *x.m_pValue;

		if (ValidateTxWrtHeight(tx, h) && HandleValidatedTx(tx.get_Reader(), h, true, rbData))
		{
			Block::Body::Writer(res).Dump(tx.get_Reader());

			fees += x.m_Profit.m_Fee;
			offset += ECC::Scalar::Native(tx.m_Offset);
			nBlockSize += x.m_Profit.m_nSize;
			++nAmount;
		}
		else
			txp.Delete(x); // isn't available in this context
	}

	LOG_INFO() << "GenerateNewBlock: size of block = " << nBlockSize << "; amount of tx = " << nAmount;

	ECC::Scalar::Native kCoinbase, kFee, kKernel;
	DeriveKeys(m_Kdf, h, fees, kCoinbase, kFee, kKernel, offset);

	if (fees)
	{
		Output::Ptr pOutp(new Output);
		pOutp->Create(kFee, fees);

		if (!HandleBlockElement(*pOutp, h, NULL, true))
			return false; // though should not happen!

		res.m_vOutputs.push_back(std::move(pOutp));
	}

	{
		TxKernel::Ptr pKrn(new TxKernel);
		pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * kKernel);

		ECC::Hash::Value hv;
		pKrn->get_Hash(hv);
		pKrn->m_Signature.Sign(hv, kKernel);

		if (!HandleBlockElement(*pKrn, true, false))
			return false; // Will fail if kernel key duplicated!

		res.m_vKernelsOutput.push_back(std::move(pKrn));
	}

	{
		Output::Ptr pOutp(new Output);
		pOutp->m_Coinbase = true;
		pOutp->Create(kCoinbase, Rules::get().CoinbaseEmission, true);

		if (!HandleBlockElement(*pOutp, h, NULL, true))
			return false;

		res.m_vOutputs.push_back(std::move(pOutp));
	}

	res.m_Subsidy += Rules::get().CoinbaseEmission;

	// Finalize block construction.
	if (m_Cursor.m_Sid.m_Row)
		s.m_Prev = m_Cursor.m_ID.m_Hash;
	else
		ZeroObject(s.m_Prev);

	if (!m_Cursor.m_SubsidyOpen)
		res.m_SubsidyClosing = false;

	if (res.m_SubsidyClosing)
		OnSubsidyOptionChanged(false);

	get_Definition(s.m_Definition, true);

	if (res.m_SubsidyClosing)
		OnSubsidyOptionChanged(true);

	s.m_Height = h;
	s.m_PoW.m_Difficulty = m_Cursor.m_DifficultyNext;
	s.m_TimeStamp = getTimestamp();

	s.m_PoW.m_Difficulty.Inc(s.m_ChainWork, m_Cursor.m_Full.m_ChainWork);

	// Adjust the timestamp to be no less than the moving median (otherwise the block'll be invalid)
	Timestamp tm = get_MovingMedian() + 1;
	s.m_TimeStamp = std::max(s.m_TimeStamp, tm);

	res.m_Offset = offset;

	return true;
}

bool NodeProcessor::GenerateNewBlock(TxPool::Fluff& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees)
{
	Block::Body block;
	block.ZeroInit();
	block.m_SubsidyClosing = true; // by default insist on it. If already closed - this flag will automatically be turned OFF
	return GenerateNewBlock(txp, s, bbBlock, fees, block, true);
}

bool NodeProcessor::GenerateNewBlock(TxPool::Fluff& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees, Block::Body& res)
{
	return GenerateNewBlock(txp, s, bbBlock, fees, res, false);
}

bool NodeProcessor::GenerateNewBlock(TxPool::Fluff& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees, Block::Body& res, bool bInitiallyEmpty)
{
	Height h = m_Cursor.m_Sid.m_Height + 1;

	if (!bInitiallyEmpty && !VerifyBlock(res, res.get_Reader(), h))
		return false;

	{
		NodeDB::Transaction t(m_DB);

		RollbackData rbData;

		if (!bInitiallyEmpty)
		{
			if (!HandleValidatedTx(res.get_Reader(), h, true, rbData))
				return false;
		}

		bool bRes = GenerateNewBlock(txp, s, res, fees, h, rbData);

		rbData.m_Inputs = 0;
		verify(HandleValidatedTx(res.get_Reader(), h, false, rbData)); // undo changes

		if (!bRes)
			return false;

		res.Sort(); // can sort only after the changes are undone.
		res.DeleteIntermediateOutputs();
	}

	Serializer ser;

	ser.reset();
	ser & res;
	ser.swap_buf(bbBlock);

	return bbBlock.size() <= Rules::get().MaxBodySize;
}

bool NodeProcessor::VerifyBlock(const Block::BodyBase& block, TxBase::IReader&& r, const HeightRange& hr)
{
	return block.IsValid(hr, m_Cursor.m_SubsidyOpen, std::move(r));
}

void NodeProcessor::ExtractBlockWithExtra(Block::Body& block, const NodeDB::StateID& sid)
{
	ByteBuffer bb;
	RollbackData rbData;
	m_DB.GetStateBlock(sid.m_Row, bb, rbData.m_Buf);

	Deserializer der;
	der.reset(bb.empty() ? NULL : &bb.at(0), bb.size());
	der & block;

	for (size_t i = 0; i < block.m_vInputs.size(); i++)
		block.m_vInputs[i]->m_Maturity = rbData.NextInput(false).m_Maturity;

	for (size_t i = 0; i < block.m_vOutputs.size(); i++)
	{
		Output& v = *block.m_vOutputs[i];
		v.m_Maturity = v.get_MinMaturity(sid.m_Height);
	}
}

void NodeProcessor::SquashOnce(std::vector<Block::Body>& v)
{
	assert(v.size() >= 2);

	Block::Body& trg = v[v.size() - 2];
	const Block::Body& src0 = v.back();
	Block::Body src1 = std::move(trg);

	trg.Merge(src0);

	bool bStop = false;
	Block::Body::Writer(trg).Combine(src0.get_Reader(), src1.get_Reader(), bStop);

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
	NodeDB::Transaction t(m_DB);

	bool b = ImportMacroBlockInternal(r);

	t.Commit(); // regardless to if succeeded or not
	if (!b)
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

	if (!m_Cursor.m_SubsidyOpen && body.m_SubsidyClosing)
	{
		LOG_WARNING() << "Invald subsidy-close flag";
		return false;
	}

	Merkle::CompactMmr cmmr;
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

			Difficulty::Raw wrk;
			s.m_PoW.m_Difficulty.Inc(wrk, m_Cursor.m_Full.m_ChainWork);

			if (wrk != s.m_ChainWork)
			{
				LOG_WARNING() << id << " Chainwork expected=" << wrk << ", actual=" << s.m_ChainWork;
				return false;
			}
		}
		else
			s.m_PoW.m_Difficulty.Inc(s.m_ChainWork);

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
	}

	LOG_INFO() << "Context-free validation...";

	if (!VerifyBlock(body, std::move(r), HeightRange(m_Cursor.m_ID.m_Height + 1, id.m_Height)))
	{
		LOG_WARNING() << "Context-free verification failed";
		return false;
	}

	LOG_INFO() << "Applying macroblock...";

	RollbackData rbData;
	if (!HandleValidatedTx(std::move(r), m_Cursor.m_ID.m_Height + 1, true, rbData, &id.m_Height))
	{
		LOG_WARNING() << "Invalid in its context";
		return false;
	}

	if (body.m_SubsidyClosing)
		OnSubsidyOptionChanged(false);

	// evaluate the Definition
	Merkle::Hash hvDef, hv;
	cmmr.get_Hash(hv);
	get_Definition(hvDef, hv);

	if (s.m_Definition != hvDef)
	{
		LOG_WARNING() << "Definition mismatch";

		if (body.m_SubsidyClosing)
			OnSubsidyOptionChanged(true);

		rbData.m_Inputs = 0;
		verify(HandleValidatedTx(std::move(r), m_Cursor.m_ID.m_Height + 1, false, rbData, &id.m_Height));

		return false;
	}

	// Update DB state flags and cursor. This will also buils the MMR for prev states
	LOG_INFO() << "Building auxilliary datas...";

	r.Reset();
	r.get_Start(body, s);
	for (bool bFirstTime = true; r.get_NextHdr(s); s.NextPrefix())
	{
		if (bFirstTime)
			bFirstTime = false;
		else
			s.m_PoW.m_Difficulty.Inc(s.m_ChainWork);

		s.get_ID(id);

		NodeDB::StateID sid;
		sid.m_Row = m_DB.StateFindSafe(id);
		if (!sid.m_Row)
			OnCorrupted();

		m_DB.SetStateFunctional(sid.m_Row);

		m_DB.DelStateBlock(sid.m_Row); // if somehow it was downloaded
		m_DB.set_Peer(sid.m_Row, NULL);

		sid.m_Height = id.m_Height;
		m_DB.MoveFwd(sid);
	}

	m_DB.ParamSet(NodeDB::ParamID::LoHorizon, &id.m_Height, NULL);
	m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &id.m_Height, NULL);
	AdjustCumulativeParams(body, true);

	InitCursor();

	LOG_INFO() << "Macroblock import succeeded";

	return true;
}

void NodeProcessor::InitializeFromBlocks()
{
	if (m_Cursor.m_ID.m_Height < Rules::HeightGenesis)
		return;

	NodeDB::WalkerState ws(m_DB);
	Height h = 0;
	bool bSubsidyOpen = true;

	for (m_DB.EnumMacroblocks(ws); ws.MoveNext(); )
	{
		Block::Body::RW rw;
		if (!OpenMacroblock(rw, ws.m_Sid))
			continue;

		if (ws.m_Sid.m_Height > m_Cursor.m_ID.m_Height)

		LOG_INFO() << "Interpreting MB up to " << ws.m_Sid.m_Height << "...";

		Block::BodyBase body;
		Block::SystemState::Sequence::Prefix prefix;

		rw.Reset();
		rw.get_Start(body, prefix);

		if (body.m_SubsidyClosing)
		{
			bSubsidyOpen = false;
			OnSubsidyOptionChanged(false);
		}

		RollbackData rbData;
		if (!HandleValidatedTx(std::move(rw), Rules::HeightGenesis, true, rbData, &ws.m_Sid.m_Height))
			OnCorrupted();

		h = ws.m_Sid.m_Height;
		break;
	}

	LOG_INFO() << "Interpreting blocks up to " << m_Cursor.m_ID.m_Height << "...";

	std::vector<uint64_t> vPath;
	vPath.reserve(m_Cursor.m_ID.m_Height - h);

	for (Height h1 = h; h1 < m_Cursor.m_ID.m_Height; h1++)
	{
		uint64_t rowid;
		if (vPath.empty())
			rowid = FindActiveAtStrict(m_Cursor.m_ID.m_Height);
		else
		{
			rowid = vPath.back();
			if (!m_DB.get_Prev(rowid))
				OnCorrupted();
		}

		vPath.push_back(rowid);
	}

	ByteBuffer bb;
	RollbackData rbData;
	for (; !vPath.empty(); vPath.pop_back())
	{
		bb.clear();
		rbData.m_Buf.clear();
		rbData.m_Inputs = 0;

		m_DB.GetStateBlock(vPath.back(), bb, rbData.m_Buf);

		if (bb.empty())
			OnCorrupted();

		Block::Body block;

		Deserializer der;
		der.reset(&bb.at(0), bb.size());
		der & block;

		if (block.m_SubsidyClosing)
		{
			if (!bSubsidyOpen)
				OnCorrupted();
			OnSubsidyOptionChanged(false);
		}

		if (!HandleValidatedTx(block.get_Reader(), ++h, true, rbData))
			OnCorrupted();
	}

	// final check
	Merkle::Hash hv;
	get_Definition(hv, false);
	if (m_Cursor.m_Full.m_Definition != hv)
		OnCorrupted();
}

} // namespace beam
