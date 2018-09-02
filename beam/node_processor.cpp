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

#include "node_processor.h"
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
	:m_Branching(-1)
	,m_Schwarzschild(-1)
{
}

struct NodeProcessor::UnspentWalker
	:public NodeDB::WalkerSpendable
{
	NodeProcessor& m_This;

	UnspentWalker(NodeProcessor& me)
		:NodeDB::WalkerSpendable(me.m_DB)
		,m_This(me)
	{
	}

	bool Traverse();

	virtual bool OnUtxo(const UtxoTree::Key&) = 0;
	virtual bool OnKernel(const Merkle::Hash&) = 0;
};

bool NodeProcessor::UnspentWalker::Traverse()
{
	for (m_Rs.m_DB.EnumUnpsent(*this); MoveNext(); )
	{
		assert(m_nUnspentCount);
		if (!m_Key.n)
			OnCorrupted();

		uint8_t nType = *(uint8_t*) m_Key.p;
		((uint8_t*&) m_Key.p)++;
		m_Key.n--;

		switch (nType)
		{
		case DbType::Utxo:
		{
			if (UtxoTree::Key::s_Bytes != m_Key.n)
				OnCorrupted();

			static_assert(sizeof(UtxoTree::Key) == UtxoTree::Key::s_Bytes, "");

			if (!OnUtxo(*(UtxoTree::Key*) m_Key.p))
				return false;
		}
		break;

		case DbType::Kernel:
		{
			if (Merkle::Hash::nBytes != m_Key.n)
				OnCorrupted();

			if (!OnKernel(*(Merkle::Hash*) m_Key.p))
				return false;

		}
		break;

		default:
			OnCorrupted();
		}
	}

	return true;
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

	if (!m_Cursor.m_SubsidyOpen)
		OnSubsidyOptionChanged(m_Cursor.m_SubsidyOpen);

	// Load all the 'live' data
	{
		struct Walker
			:public UnspentWalker
		{
			Walker(NodeProcessor& me) :UnspentWalker(me) {}

			virtual bool OnUtxo(const UtxoTree::Key& key) override
			{
				UtxoTree::Cursor cu;
				bool bCreate = true;

				m_This.m_Utxos.Find(cu, key, bCreate)->m_Value.m_Count = m_nUnspentCount;
				assert(bCreate);

				return true;
			}

			virtual bool OnKernel(const Merkle::Hash& key) override
			{
				RadixHashOnlyTree::Cursor cu;
				bool bCreate = true;

				m_This.m_Kernels.Find(cu, key, bCreate);
				assert(bCreate);

				return true;
			}
		};

		Walker wlk(*this);
		wlk.Traverse();
	}

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

		if (sid.m_Height < m_Cursor.m_Sid.m_Height)
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

		Block::SystemState::Full s;
		m_DB.get_State(sid.m_Row, s);

		Block::SystemState::ID id;

		if (bBlock)
			s.get_ID(id);
		else
		{
			id.m_Height = s.m_Height - 1;
			id.m_Hash = s.m_Prev;
		}

		PeerID peer;
		bool bPeer = m_DB.get_Peer(sid.m_Row, peer);

		RequestData(id, bBlock, bPeer ? &peer : NULL);
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
	Height h = m_Cursor.m_Sid.m_Height;
	if (h <= m_Horizon.m_Branching)
		return;
	h -= m_Horizon.m_Branching;

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

	if (m_Horizon.m_Schwarzschild <= m_Horizon.m_Branching)
		return;

	Height hExtra = m_Horizon.m_Schwarzschild - m_Horizon.m_Branching;
	if (h <= hExtra)
		return;
	h -= hExtra;

	for (Height hFossil = m_DB.ParamIntGetDef(NodeDB::ParamID::FossilHeight); hFossil < h; )
	{
		uint64_t rowid = FindActiveAtStrict(hFossil + Rules::HeightGenesis);

		if (1 != m_DB.GetStateNextCount(rowid))
			break;

		DereferenceFossilBlock(rowid);

		m_DB.DelStateBlock(rowid);
		m_DB.set_Peer(rowid, NULL);

		++hFossil;
		m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &hFossil, NULL);
	}
}

void NodeProcessor::get_CurrentLive(Merkle::Hash& hv)
{
	m_Utxos.get_Hash(hv);

	Merkle::Hash hv2;
	m_Kernels.get_Hash(hv2);

	Merkle::Interpret(hv, hv2, true);
}

void NodeProcessor::get_Definition(Merkle::Hash& hv, bool bForNextState)
{
	get_CurrentLive(hv);
	Merkle::Interpret(hv, bForNextState ? m_Cursor.m_HistoryNext : m_Cursor.m_History, false);
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

template <typename T, uint8_t nType>
struct SpendableKey
{
	uint8_t m_Type;
	T m_Key;

	SpendableKey()
		:m_Type(nType)
	{
		static_assert(sizeof(*this) == sizeof(m_Type) + sizeof(m_Key), "");
	}
};

bool NodeProcessor::HandleBlockElement(const Input& v, Height h, const Height* pHMax, bool bFwd, RollbackData& rbData)
{
	SpendableKey<UtxoTree::Key, DbType::Utxo> skey;

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

		skey.m_Key = p->m_Key;
		d = skey.m_Key;
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
		skey.m_Key = d;

		bool bCreate = true;
		p = m_Utxos.Find(cu, skey.m_Key, bCreate);

		if (bCreate)
			p->m_Value.m_Count = 1;
		else
		{
			p->m_Value.m_Count++;
			cu.Invalidate();
		}
	}

	m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), 0, bFwd ? -1 : 1);
	return true;
}

struct NodeProcessor::UtxoSig
{
	bool	m_Coinbase;
	Height	m_Incubation; // # of blocks before it's mature

	std::unique_ptr<ECC::RangeProof::Confidential>	m_pConfidential;
	std::unique_ptr<ECC::RangeProof::Public>		m_pPublic;

	template <typename Archive>
	void serialize(Archive& ar)
	{
		ar
			& m_Coinbase
			& m_Incubation
			& m_pConfidential
			& m_pPublic;
	}
};

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

	SpendableKey<UtxoTree::Key, DbType::Utxo> skey;
	skey.m_Key = d;
	NodeDB::Blob blob(&skey, sizeof(skey));

	UtxoTree::Cursor cu;
	bool bCreate = true;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, skey.m_Key, bCreate);

	cu.Invalidate();

	if (bFwd)
	{
		if (bCreate)
		{
			p->m_Value.m_Count = 1;
			m_DB.AddSpendable(blob, NULL, 1, 1);
		}
		else
		{
			// protect again overflow attacks, though it's highly unlikely (Input::Count is currently limited to 32 bits, it'd take millions of blocks)
			Input::Count nCountInc = p->m_Value.m_Count + 1;
			if (!nCountInc)
				return false;

			p->m_Value.m_Count = nCountInc;
			m_DB.ModifySpendable(blob, 1, 1);
		}
	} else
	{
		if (1 == p->m_Value.m_Count)
			m_Utxos.Delete(cu);
		else
			p->m_Value.m_Count--;

		m_DB.ModifySpendable(blob, -1, -1);
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

	SpendableKey<Merkle::Hash, DbType::Kernel> skey;
	v.get_ID(skey.m_Key);

	RadixHashOnlyTree::Cursor cu;
	bool bCreate = bAdd;
	RadixHashOnlyTree::MyLeaf* p = m_Kernels.Find(cu, skey.m_Key, bCreate);

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

	NodeDB::Blob blob(&skey, sizeof(skey));

	if (bIsInput)
		m_DB.ModifySpendable(blob, 0, bFwd ? -1 : 1);
	else
		if (bFwd)
		{
			NodeDB::Blob body;
			if (v.m_pHashLock)
				body = NodeDB::Blob(v.m_pHashLock->m_Preimage);

			m_DB.AddSpendable(blob, v.m_pHashLock ? &body : NULL, 1, 1);
		} else
			m_DB.ModifySpendable(blob, -1, -1);

	return true;
}

void NodeProcessor::DereferenceFossilBlock(uint64_t rowid)
{
	ByteBuffer bbBlock;
	RollbackData rbData;

	m_DB.GetStateBlock(rowid, bbBlock, rbData.m_Buf);

	Block::Body block;

	Deserializer der;
	der.reset(&bbBlock.at(0), bbBlock.size());
	der & block;

	Block::Body::Reader r = block.get_Reader();
	r.Reset();

	for (; r.m_pUtxoIn; r.NextUtxoIn())
	{
		UtxoTree::Key::Data d;
		d.m_Commitment = r.m_pUtxoIn->m_Commitment;
		d.m_Maturity = rbData.NextInput(false).m_Maturity;

		SpendableKey<UtxoTree::Key, DbType::Utxo> skey;
		skey.m_Key = d;

		m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), -1, 0);
	}

	for (; r.m_pKernelIn; r.NextKernelIn())
	{
		SpendableKey<Merkle::Hash, DbType::Kernel> skey;
		r.m_pKernelIn->get_ID(skey.m_Key);

		m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), -1, 0);
	}
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

bool NodeProcessor::IsRelevantHeight(Height h)
{
	uint64_t hFossil = m_DB.ParamIntGetDef(NodeDB::ParamID::FossilHeight);
	return h >= hFossil + Rules::HeightGenesis;
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

	if (!IsRelevantHeight(s.m_Height))
		return DataStatus::Rejected;

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

	return ret;
}

NodeProcessor::DataStatus::Enum NodeProcessor::OnBlock(const Block::SystemState::ID& id, const NodeDB::Blob& block, const PeerID& peer)
{
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

bool NodeProcessor::IsStateNeeded(const Block::SystemState::ID& id)
{
	return IsRelevantHeight(id.m_Height) && !m_DB.StateFindSafe(id);
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
// TxPool
bool NodeProcessor::ValidateTx(const Transaction& tx, Transaction::Context& ctx)
{
	if (!tx.IsValid(ctx))
		return false;

	return ctx.m_Height.IsInRange(m_Cursor.m_Sid.m_Height + 1);
}

void NodeProcessor::TxPool::AddValidTx(Transaction::Ptr&& pValue, const Transaction::Context& ctx, const Transaction::KeyType& key)
{
	assert(pValue);

	SerializerSizeCounter ssc;
	ssc & pValue;

	Element* p = new Element;
	p->m_pValue = std::move(pValue);
	p->m_Threshold.m_Value	= ctx.m_Height.m_Max;
	p->m_Profit.m_Fee	= ctx.m_Fee.Hi ? Amount(-1) : ctx.m_Fee.Lo; // ignore huge fees (which are  highly unlikely), saturate.
	p->m_Profit.m_nSize	= (uint32_t) ssc.m_Counter.m_Value;
	p->m_Tx.m_Key = key;

	m_setThreshold.insert(p->m_Threshold);
	m_setProfit.insert(p->m_Profit);
	m_setTxs.insert(p->m_Tx);
}

void NodeProcessor::TxPool::Delete(Element& x)
{
	m_setThreshold.erase(ThresholdSet::s_iterator_to(x.m_Threshold));
	m_setProfit.erase(ProfitSet::s_iterator_to(x.m_Profit));
	m_setTxs.erase(TxSet::s_iterator_to(x.m_Tx));
	delete &x;
}

void NodeProcessor::TxPool::DeleteOutOfBound(Height h)
{
	while (!m_setThreshold.empty())
	{
		Element::Threshold& t = *m_setThreshold.begin();
		if (t.m_Value >= h)
			break;

		Delete(t.get_ParentObj());
	}
}

void NodeProcessor::TxPool::ShrinkUpTo(uint32_t nCount)
{
	while (m_setProfit.size() > nCount)
		Delete(m_setProfit.rbegin()->get_ParentObj());
}

void NodeProcessor::TxPool::Clear()
{
	while (!m_setThreshold.empty())
		Delete(m_setThreshold.begin()->get_ParentObj());
}

bool NodeProcessor::TxPool::Element::Profit::operator < (const Profit& t) const
{
	// handle overflow. To be precise need to use big-int (96-bit) arithmetics
	//	return m_Fee * t.m_nSize > t.m_Fee * m_nSize;

	return
		(uintBigFrom(m_Fee) * uintBigFrom(t.m_nSize)) >
		(uintBigFrom(t.m_Fee) * uintBigFrom(m_nSize));
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

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, Block::Body& res, Amount& fees, Height h, RollbackData& rbData)
{
	fees = 0;
	size_t nBlockSize = 0;
	size_t nAmount = 0;

	// due to (potential) inaccuracy in the block size estimation, our rough estimate - take no more than 95% of allowed block size, minus potential UTXOs to consume fees and coinbase.
	const size_t nRoughExtra = sizeof(ECC::Point) * 2 + sizeof(ECC::RangeProof::Confidential) + sizeof(ECC::RangeProof::Public) + 300;
	const size_t nSizeThreshold = Rules::get().MaxBodySize * 95 / 100 - nRoughExtra;

	ECC::Scalar::Native offset = res.m_Offset;

	for (TxPool::ProfitSet::iterator it = txp.m_setProfit.begin(); txp.m_setProfit.end() != it; )
	{
		TxPool::Element& x = (it++)->get_ParentObj();

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

		if (HandleValidatedTx(tx.get_Reader(), h, true, rbData))
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

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees)
{
	Block::Body block;
	block.ZeroInit();
	block.m_SubsidyClosing = true; // by default insist on it. If already closed - this flag will automatically be turned OFF
	return GenerateNewBlock(txp, s, bbBlock, fees, block, true);
}

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees, Block::Body& res)
{
	return GenerateNewBlock(txp, s, bbBlock, fees, res, false);
}

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees, Block::Body& res, bool bInitiallyEmpty)
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
	Block::BodyBase body;
	Block::SystemState::Full s;
	Block::SystemState::ID id;

	r.Reset();
	r.get_Start(body, s);

	Cursor cu = m_Cursor;
	if ((cu.m_ID.m_Height + 1 != s.m_Height) || (cu.m_ID.m_Hash != s.m_Prev))
	{
		id.m_Height = s.m_Height - 1;
		id.m_Hash = s.m_Prev;
		LOG_WARNING() << "Incompatible state for import. My Tip: " << cu.m_ID << ", Macroblock starts at " << id;
		return false; // incompatible beginning state
	}

	if (!cu.m_SubsidyOpen && body.m_SubsidyClosing)
	{
		LOG_WARNING() << "Invald subsidy-close flag";
		return false;
	}

	NodeDB::Transaction t(m_DB);

	LOG_INFO() << "Verifying headers...";

	for (bool bFirstTime = true ; r.get_NextHdr(s); s.NextPrefix())
	{
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

	uint64_t rowid = m_DB.StateFindSafe(id);
	if (!rowid)
		OnCorrupted();
	m_DB.get_State(rowid, s);

	LOG_INFO() << "Context-free validation...";

	if (!VerifyBlock(body, std::move(r), HeightRange(cu.m_ID.m_Height + 1, id.m_Height)))
	{
		LOG_WARNING() << "Context-free verification failed";
		return false;
	}

	LOG_INFO() << "Applying macroblock...";

	RollbackData rbData;
	if (!HandleValidatedTx(std::move(r), cu.m_ID.m_Height + 1, true, rbData, &id.m_Height))
	{
		LOG_WARNING() << "Invalid in its context";
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

	InitCursor();

	if (body.m_SubsidyClosing)
	{
		m_Cursor.m_SubsidyOpen = false;
		OnSubsidyOptionChanged(m_Cursor.m_SubsidyOpen);
	}

	Merkle::Hash hvDef;
	get_Definition(hvDef, false);

	if (s.m_Definition != hvDef)
	{
		LOG_WARNING() << "Definition mismatch";

		if (m_Cursor.m_SubsidyOpen != cu.m_SubsidyOpen)
			OnSubsidyOptionChanged(cu.m_SubsidyOpen);

		rbData.m_Inputs = 0;
		verify(HandleValidatedTx(std::move(r), cu.m_ID.m_Height + 1, false, rbData, &id.m_Height));

		// DB changes are not reverted explicitly, but they will be reverted by DB transaction rollback.

		m_Cursor = cu;

		return false;
	}

	AdjustCumulativeParams(body, true);

	// everything's fine
	m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &id.m_Height, NULL);
	t.Commit();

	LOG_INFO() << "Macroblock import succeeded";

	TryGoUp();

	return true;
}

bool NodeProcessor::get_KernelHashPreimage(const Merkle::Hash& id, ECC::uintBig& val)
{
	SpendableKey<Merkle::Hash, DbType::Kernel> skey;
	skey.m_Key = id;

	NodeDB::Blob blobKey(&skey, sizeof(skey)), blobVal(val);

	return m_DB.GetSpendableBody(blobKey, blobVal);
}

} // namespace beam
