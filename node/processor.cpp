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

void NodeProcessor::Initialize(const char* szPath, bool bResetCursor /* = false */)
{
	m_DB.Open(szPath);
	m_DbTx.Start(m_DB);

	Merkle::Hash hv;
	Blob blob(hv);

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

	m_nSizeUtxoComission = 0;
	ZeroObject(m_Extra);
	m_Extra.m_SubsidyOpen = true;

	if (bResetCursor)
		m_DB.ResetCursor();

	InitCursor();

	InitializeFromBlocks();

	m_Horizon.m_Schwarzschild = std::max(m_Horizon.m_Schwarzschild, m_Horizon.m_Branching);
	m_Horizon.m_Schwarzschild = std::max(m_Horizon.m_Schwarzschild, (Height) Rules::get().MaxRollbackHeight);

	if (!bResetCursor)
		TryGoUp();
}

NodeProcessor::~NodeProcessor()
{
	if (m_DbTx.IsInProgress())
	{
		try {
			m_DbTx.Commit();
		} catch (std::exception& e) {
			LOG_ERROR() << "DB Commit failed: %s" << e.what();
		}
	}
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

		m_Cursor.m_LoHorizon = m_DB.ParamIntGetDef(NodeDB::ParamID::LoHorizon);
	}
	else
		ZeroObject(m_Cursor);

	m_Cursor.m_DifficultyNext = get_NextDifficulty();
}

void NodeProcessor::EnumCongestions(uint32_t nMaxBlocksBacklog)
{
	bool noRequests = true;
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

		Height nBlocks = 0;
		const uint32_t nMaxBlocks = 32;
		uint64_t pBlockRow[nMaxBlocks];

		while (true)
		{
			pBlockRow[nBlocks % nMaxBlocks] = sid.m_Row;
			nBlocks++;

			if (Rules::HeightGenesis == sid.m_Height)
			{
				sid.m_Height--;
				break;
			}

			if (!m_DB.get_Prev(sid))
			{
				nBlocks = 0;
				break;
			}

			if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(sid.m_Row))
				break;
		}

		noRequests = false;

		Block::SystemState::ID id;

		if (nBlocks)
		{
			if (!nMaxBlocksBacklog)
				nMaxBlocksBacklog = 1;
			else
			{
				if (nMaxBlocksBacklog > nMaxBlocks)
					nMaxBlocksBacklog = nMaxBlocks;

				if (nMaxBlocksBacklog > nBlocks)
					nMaxBlocksBacklog = static_cast<uint32_t>(nBlocks);
			}

			for (uint32_t i = 0; i < nMaxBlocksBacklog; i++)
			{
				sid.m_Height++;
				sid.m_Row = pBlockRow[(--nBlocks) % nMaxBlocks];

				if (i && (NodeDB::StateFlags::Functional & m_DB.GetStateFlags(sid.m_Row)))
					break;

				m_DB.get_StateID(sid, id);

				RequestDataInternal(id, sid.m_Row, true);
			}
		}
		else
		{
			Block::SystemState::Full s;
			m_DB.get_State(sid.m_Row, s);

			id.m_Height = s.m_Height - 1;
			id.m_Hash = s.m_Prev;

			RequestDataInternal(id, sid.m_Row, false);
		}
	}
	if (noRequests)
	{
		OnUpToDate();
	}
}

void NodeProcessor::RequestDataInternal(const Block::SystemState::ID& id, uint64_t row, bool bBlock)
{
	if (id.m_Height >= m_Cursor.m_LoHorizon)
	{
		PeerID peer;
		bool bPeer = m_DB.get_Peer(row, peer);

		RequestData(id, bBlock, bPeer ? &peer : NULL);
	}
	else
	{
		LOG_WARNING() << id << " State unreachable!"; // probably will pollute the log, but it's a critical situation anyway
	}
}

void NodeProcessor::TryGoUp()
{
	bool bDirty = false;
	uint64_t rowid = m_Cursor.m_Sid.m_Row;

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
		if (m_Cursor.m_Sid.m_Row != rowid)
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

		for (Height hFossil = get_FossilHeight(); ; )
		{
			if (++hFossil >= h)
				break;

			NodeDB::WalkerState ws(m_DB);
			for (m_DB.EnumStatesAt(ws, hFossil); ws.MoveNext(); )
			{
				if (!(NodeDB::StateFlags::Active & m_DB.GetStateFlags(ws.m_Sid.m_Row)))
					m_DB.SetStateNotFunctional(ws.m_Sid.m_Row);

				m_DB.DelStateBlockAll(ws.m_Sid.m_Row);
				m_DB.set_Peer(ws.m_Sid.m_Row, NULL);
			}

			m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &hFossil, NULL);
		}
	}
}

Height NodeProcessor::get_FossilHeight()
{
	return m_DB.ParamIntGetDef(NodeDB::ParamID::FossilHeight, Rules::HeightGenesis - 1);
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
				pDst[i].m_Maturity = txv.m_vInputs[i]->m_Maturity;
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
			txv.m_vInputs[i]->m_Maturity = pDst[i].m_Maturity;
	}
};

void NodeProcessor::ReadBody(Block::Body& res, const ByteBuffer& bbP, const ByteBuffer& bbE)
{
	Deserializer der;
	der.reset(bbP);
	der & Cast::Down<Block::BodyBase>(res);
	der & Cast::Down<TxVectors::Perishable>(res);

	der.reset(bbE);
	der & Cast::Down<TxVectors::Ethernal>(res);
}

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

	Merkle::FixedMmmr mmr;
	size_t iTrg;

	if (h <= get_FossilHeight())
	{
		Block::Body::RW rw;
		if (!OpenLatestMacroblock(rw))
			OnCorrupted();

		rw.Reset();
		rw.NextKernelFF(h);

		// 1st calculate the count
		uint64_t nTotal = 0;
		for (; rw.m_pKernel && rw.m_pKernel->m_Maturity == h; rw.NextKernel())
			nTotal++;

		mmr.Reset(nTotal);
		rw.Reset();
		rw.NextKernelFF(h);

		iTrg = ProcessKrnMmr(mmr, std::move(rw), h, idKrn, ppRes);
	}
	else
	{
		uint64_t rowid = FindActiveAtStrict(h);

		ByteBuffer bbE;
		m_DB.GetStateBlock(rowid, NULL, &bbE, NULL);

		TxVectors::Ethernal txve;
		TxVectors::Perishable txvp; // dummy

		Deserializer der;
		der.reset(bbE);
		der & txve;

		TxVectors::Reader r(txvp, txve);
		r.Reset();

		mmr.Reset(txve.m_vKernels.size());
		iTrg = ProcessKrnMmr(mmr, std::move(r), 0, idKrn, ppRes);
	}

	if (uint64_t(-1) == iTrg)
		OnCorrupted();

	mmr.get_Proof(proof, iTrg);
	return h;
}

bool NodeProcessor::HandleBlock(const NodeDB::StateID& sid, bool bFwd)
{
	ByteBuffer bbP, bbE;
	RollbackData rbData;
	m_DB.GetStateBlock(sid.m_Row, &bbP, &bbE, &rbData.m_Buf);

	Block::SystemState::Full s;
	m_DB.get_State(sid.m_Row, s); // need it for logging anyway

	Block::SystemState::ID id;
	s.get_ID(id);

	Block::Body block;
	try {
		ReadBody(block, bbP, bbE);
	}
	catch (const std::exception&) {
		LOG_WARNING() << id << " Block deserialization failed";
		return false;
	}

	std::vector<Merkle::Hash> vKrnID(block.m_vKernels.size()); // allocate mem for all kernel IDs, we need them for initial verification vs header, and at the end - to add to the kernel index.
	// better to allocate the memory, then to calculate IDs twice
	for (size_t i = 0; i < vKrnID.size(); i++)
		block.m_vKernels[i]->get_ID(vKrnID[i]);

	bool bFirstTime = false;

	if (bFwd)
	{
		if (rbData.m_Buf.empty())
		{
			bFirstTime = true;

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

			if (!VerifyBlock(block, block.get_Reader(), sid.m_Height))
			{
				LOG_WARNING() << id << " context-free verification failed";
				return false;
			}
		}
	}
	else
	{
		assert(!rbData.m_Buf.empty());
		rbData.Export(block);
	}

	bool bOk = HandleValidatedBlock(block.get_Reader(), block, sid.m_Height, bFwd);
	if (!bOk)
		LOG_WARNING() << id << " invalid in its context";

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
			rbData.Import(block);
			m_DB.SetStateRollback(sid.m_Row, rbData.m_Buf);


			assert(m_Cursor.m_LoHorizon <= m_Cursor.m_Sid.m_Height);
			if (m_Cursor.m_Sid.m_Height - m_Cursor.m_LoHorizon > Rules::get().MaxRollbackHeight)
			{
				m_Cursor.m_LoHorizon = m_Cursor.m_Sid.m_Height - Rules::get().MaxRollbackHeight;
				m_DB.ParamSet(NodeDB::ParamID::LoHorizon, &m_Cursor.m_LoHorizon, NULL);
			}

		}
		else
			verify(HandleValidatedBlock(block.get_Reader(), block, sid.m_Height, false));
	}

	if (bOk)
	{
		for (size_t i = 0; i < vKrnID.size(); i++)
		{
			const Merkle::Hash& hv = vKrnID[i];
			if (bFwd)
				m_DB.InsertKernel(hv, sid.m_Height);
			else
				m_DB.DeleteKernel(hv, sid.m_Height);
		}

		if (bFwd)
		{
			auto r = block.get_Reader();
			r.Reset();
			RecognizeUtxos(std::move(r), sid.m_Height);
		}
		else
			m_DB.DeleteEventsAbove(m_Cursor.m_ID.m_Height);

		LOG_INFO() << id << " Block interpreted. Fwd=" << bFwd;
	}

	return bOk;
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

			// In case of macroblock we can't recover the original input height. But in our current implementation macroblocks always go from the beginning, hence they don't contain input.
			m_DB.InsertEvent(hMax, Blob(&evt, sizeof(evt)), Blob(NULL, 0));
		}
	}

	for (; r.m_pUtxoOut; r.NextUtxoOut())
	{
		const Output& x = *r.m_pUtxoOut;

		struct Walker :public IKeyWalker
		{
			const Output& m_Output;
			UtxoEvent::Value m_Value;

			Walker(const Output& x) :m_Output(x) {}

			virtual bool OnKey(Key::IPKdf& kdf, Key::Index iKdf) override
			{
				Key::IDV kidv;
				if (!m_Output.Recover(kdf, kidv))
					return true; // continue enumeration

				m_Value.m_iKdf = iKdf;
				m_Value.m_Kidv = kidv;
				return false; // stop
			}
		};

		Walker w(x);
		if (!EnumViewerKeys(w))
		{
			// bingo!
			Height h;
			if (x.m_Maturity)
			{
				w.m_Value.m_Maturity = x.m_Maturity;
				// try to reverse-engineer the original block from the maturity
				h = x.m_Maturity - x.get_MinMaturity(0);
			}
			else
			{
				h = hMax;
				w.m_Value.m_Maturity = x.get_MinMaturity(h);
			}

			const UtxoEvent::Key& key = x.m_Commitment;
			m_DB.InsertEvent(h, Blob(&w.m_Value, sizeof(w.m_Value)), Blob(&key, sizeof(key)));
		}
	}
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
	if (body.m_SubsidyClosing && (m_Extra.m_SubsidyOpen != bFwd))
		return false; // invalid subsidy close flag

	if (!HandleValidatedTx(std::move(r), h, bFwd, pHMax))
		return false;

	if (body.m_SubsidyClosing)
		ToggleSubsidyOpened();

	ECC::Scalar::Native kOffset = body.m_Offset;

	if (bFwd)
		m_Extra.m_Subsidy += body.m_Subsidy;
	else
	{
		m_Extra.m_Subsidy -= body.m_Subsidy;
		kOffset = -kOffset;
	}

	m_Extra.m_Offset += kOffset;

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
			d.m_Maturity = h;
			kMax = d;
		}
		else
		{
			if (v.m_Maturity > *pHMax)
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

		assert(p->m_Value.m_Count); // we don't store zeroes

		if (!--p->m_Value.m_Count)
			m_Utxos.Delete(cu);
		else
			cu.InvalidateElement();

		if (!pHMax)
			Cast::NotConst(v).m_Maturity = d.m_Maturity;
	} else
	{
		d.m_Maturity = v.m_Maturity;

		bool bCreate = true;
		UtxoTree::Key key;
		key = d;

		p = m_Utxos.Find(cu, key, bCreate);

		if (bCreate)
			p->m_Value.m_Count = 1;
		else
		{
			p->m_Value.m_Count++;
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

void NodeProcessor::ToggleSubsidyOpened()
{
	UtxoTree::Key::Data d;
	ZeroObject(d);
	d.m_Commitment.m_Y = true; // invalid commitment

	UtxoTree::Key key;
	key = d;

	UtxoTree::Cursor cu;
	bool bCreate = true;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, key, bCreate);

	assert(m_Extra.m_SubsidyOpen == bCreate);
	m_Extra.m_SubsidyOpen = !bCreate;

	if (bCreate)
		p->m_Value.m_Count = 1;
	else
	{
		assert(1 == p->m_Value.m_Count);
		m_Utxos.Delete(cu);
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

	m_DB.DelStateBlockAll(row);
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

	if (!s.IsValid())
	{
		LOG_WARNING() << id << " header invalid!";
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
		uint64_t rowid = m_DB.InsertState(s);
		m_DB.set_Peer(rowid, &peer);

		LOG_INFO() << id << " Header accepted";
		OnStateData();
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

	if (id.m_Height < m_Cursor.m_LoHorizon)
		return DataStatus::Unreachable;

	LOG_INFO() << id << " Block received";

	m_DB.SetStateBlock(rowid, bbP, bbE);
	m_DB.SetStateFunctional(rowid);
	m_DB.set_Peer(rowid, &peer);

	if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(rowid))
		TryGoUp();

	OnBlockData();
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

bool NodeProcessor::ValidateTxWrtHeight(const Transaction& tx, Height h)
{
	for (size_t i = 0; i < tx.m_vKernels.size(); i++)
		if (!tx.m_vKernels[i]->m_Height.IsInRange(h))
			return false;

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
				const UtxoTree::MyLeaf& n = Cast::Up<UtxoTree::MyLeaf>(x);
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
	bc.m_Block.m_Subsidy += Rules::get().CoinbaseEmission;
	if (!m_Extra.m_SubsidyOpen)
		bc.m_Block.m_SubsidyClosing = false;

	SerializerSizeCounter ssc;
	ssc & bc.m_Block;

	Block::Builder bb;

	Output::Ptr pOutp;
	TxKernel::Ptr pKrn;

	bb.AddCoinbaseAndKrn(bc.m_Kdf, h, pOutp, pKrn);
	ssc & *pOutp;
	ssc & *pKrn;

	ECC::Scalar::Native offset = bc.m_Block.m_Offset;

	if (BlockContext::Mode::Assemble != bc.m_Mode)
	{
		if (!HandleBlockElement(*pOutp, h, NULL, true))
			return 0;

		bc.m_Block.m_vOutputs.push_back(std::move(pOutp));
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

		if (x.m_Profit.m_Fee.Hi)
		{
			// huge fees are unsupported
			bc.m_TxPool.Delete(x);
			continue;
		}

		Amount feesNext = bc.m_Fees + x.m_Profit.m_Fee.Lo;
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

		if (ValidateTxWrtHeight(tx, h) && HandleValidatedTx(tx.get_Reader(), h, true))
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
			bb.AddFees(bc.m_Kdf, h, bc.m_Fees, pOutp);
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
	if (m_Cursor.m_Sid.m_Row)
		bc.m_Hdr.m_Prev = m_Cursor.m_ID.m_Hash;
	else
		ZeroObject(bc.m_Hdr.m_Prev);

	if (bc.m_Block.m_SubsidyClosing)
		ToggleSubsidyOpened();

	get_Definition(bc.m_Hdr.m_Definition, true);

	bc.m_Block.NormalizeE();

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

	if (bc.m_Block.m_SubsidyClosing)
		ToggleSubsidyOpened();

	bc.m_Hdr.m_PoW.m_Difficulty = m_Cursor.m_DifficultyNext;
	bc.m_Hdr.m_TimeStamp = getTimestamp();

	bc.m_Hdr.m_ChainWork = m_Cursor.m_Full.m_ChainWork + bc.m_Hdr.m_PoW.m_Difficulty;

	// Adjust the timestamp to be no less than the moving median (otherwise the block'll be invalid)
	Timestamp tm = get_MovingMedian() + 1;
	bc.m_Hdr.m_TimeStamp = std::max(bc.m_Hdr.m_TimeStamp, tm);
}

NodeProcessor::BlockContext::BlockContext(TxPool::Fluff& txp, Key::IKdf& kdf)
	:m_TxPool(txp)
	,m_Kdf(kdf)
{
	m_Fees = 0;
	m_Block.ZeroInit();
	m_Block.m_SubsidyClosing = true; // by default insist on it. If already closed - this flag will automatically be turned OFF
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
	{
		bc.m_Hdr.m_Height = h;

		if (BlockContext::Mode::Assemble != bc.m_Mode)
		{
			bc.m_Block.NormalizeE(); // kernels must be normalized before the header is generated
			GenerateNewHdr(bc);
		}
	}

	verify(HandleValidatedTx(bc.m_Block.get_Reader(), h, false)); // undo changes

	// reset input maturities
	for (size_t i = 0; i < bc.m_Block.m_vInputs.size(); i++)
		bc.m_Block.m_vInputs[i]->m_Maturity = 0;

	if (!nSizeEstimated)
		return false;

	if (BlockContext::Mode::Assemble == bc.m_Mode)
		return true;

	size_t nCutThrough = bc.m_Block.NormalizeP(); // right before serialization
	nCutThrough; // remove "unused var" warning


	Serializer ser;

	ser.reset();
	ser & Cast::Down<Block::BodyBase>(bc.m_Block);
	ser & Cast::Down<TxVectors::Perishable>(bc.m_Block);
	ser.swap_buf(bc.m_BodyP);

	ser.reset();
	ser & Cast::Down<TxVectors::Ethernal>(bc.m_Block);
	ser.swap_buf(bc.m_BodyE);

	size_t nSize = bc.m_BodyP.size() + bc.m_BodyE.size();

	if (BlockContext::Mode::SinglePass == bc.m_Mode)
		assert(nCutThrough ?
			(nSize < nSizeEstimated) :
			(nSize == nSizeEstimated));

	return nSize <= Rules::get().MaxBodySize;
}

bool NodeProcessor::VerifyBlock(const Block::BodyBase& block, TxBase::IReader&& r, const HeightRange& hr)
{
	return block.IsValid(hr, m_Extra.m_SubsidyOpen, std::move(r));
}

void NodeProcessor::ExtractBlockWithExtra(Block::Body& block, const NodeDB::StateID& sid)
{
	ByteBuffer bbP, bbE;
	RollbackData rbData;
	m_DB.GetStateBlock(sid.m_Row, &bbP, &bbE, &rbData.m_Buf);

	ReadBody(block, bbP, bbE);
	rbData.Export(block);

	for (size_t i = 0; i < block.m_vOutputs.size(); i++)
	{
		Output& v = *block.m_vOutputs[i];
		v.m_Maturity = v.get_MinMaturity(sid.m_Height);
	}

	block.NormalizeP(); // needed, since the maturity is adjusted non-even

	for (size_t i = 0; i < block.m_vKernels.size(); i++)
		block.m_vKernels[i]->m_Maturity = sid.m_Height;
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

		m_DB.DelStateBlockAll(sid.m_Row); // if somehow it was downloaded
		m_DB.set_Peer(sid.m_Row, NULL);

		sid.m_Height = id.m_Height;
		m_DB.MoveFwd(sid);
	}

	// kernels
	for (; r.m_pKernel; r.NextKernel())
	{
		r.m_pKernel->get_ID(hv);
		m_DB.InsertKernel(hv, r.m_pKernel->m_Maturity);
	}

	LOG_INFO() << "Recovering owner UTXOs...";
	RecognizeUtxos(std::move(r), id.m_Height);

	m_DB.ParamSet(NodeDB::ParamID::LoHorizon, &id.m_Height, NULL);
	m_DB.ParamSet(NodeDB::ParamID::FossilHeight, &id.m_Height, NULL);

	InitCursor();

	LOG_INFO() << "Macroblock import succeeded";

	return true;
}

Height NodeProcessor::OpenLatestMacroblock(Block::Body::RW& rw)
{
	NodeDB::WalkerState ws(m_DB);
	for (m_DB.EnumMacroblocks(ws); ws.MoveNext(); )
	{
		if (ws.m_Sid.m_Height > m_Cursor.m_ID.m_Height)
			continue; //?

		if (OpenMacroblock(rw, ws.m_Sid))
			return ws.m_Sid.m_Height;
	}

	return Rules::HeightGenesis - 1;
}

bool NodeProcessor::EnumBlocks(IBlockWalker& wlk)
{
	if (m_Cursor.m_ID.m_Height < Rules::HeightGenesis)
		return true;

	Block::Body::RW rw;

	Height h = OpenLatestMacroblock(rw);
	if (h >= Rules::HeightGenesis)
	{
		Block::BodyBase body;
		Block::SystemState::Sequence::Prefix prefix;

		rw.Reset();
		rw.get_Start(body, prefix);

		if (!wlk.OnBlock(body, std::move(rw), 0, Rules::HeightGenesis, &h))
			return false;
	}

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

	ByteBuffer bbP, bbE;
	for (; !vPath.empty(); vPath.pop_back())
	{
		bbP.clear();
		bbE.clear();

		m_DB.GetStateBlock(vPath.back(), &bbP, &bbE, NULL);

		Block::Body block;
		ReadBody(block, bbP, bbE);

		if (!wlk.OnBlock(block, block.get_Reader(), vPath.back(), ++h, NULL))
			return false;
	}

	return true;
}


void NodeProcessor::InitializeFromBlocks()
{
	struct MyWalker
		:public IBlockWalker
	{
		NodeProcessor* m_pThis;
		bool m_bFirstBlock = true;

		virtual bool OnBlock(const Block::BodyBase& body, TxBase::IReader&& r, uint64_t rowid, Height h, const Height* pHMax) override
		{
			if (pHMax)
			{
				LOG_INFO() << "Interpreting MB up to " << *pHMax << "...";
			} else
				if (m_bFirstBlock)
				{
					m_bFirstBlock = false;
					LOG_INFO() << "Interpreting blocks up to " << m_pThis->m_Cursor.m_ID.m_Height << "...";
				}

			if (!m_pThis->HandleValidatedBlock(std::move(r), body, h, true, pHMax))
				OnCorrupted();

			return true;
		}
	};

	MyWalker wlk;
	wlk.m_pThis = this;
	EnumBlocks(wlk);

	if (m_Cursor.m_ID.m_Height >= Rules::HeightGenesis)
	{
		// final check
		Merkle::Hash hv;
		get_Definition(hv, false);
		if (m_Cursor.m_Full.m_Definition != hv)
			OnCorrupted();
	}
}

bool NodeProcessor::IUtxoWalker::OnBlock(const Block::BodyBase&, TxBase::IReader&& r, uint64_t rowid, Height, const Height* pHMax)
{
	if (rowid)
		m_This.get_DB().get_State(rowid, m_Hdr);
	else
		ZeroObject(m_Hdr);

	for (r.Reset(); r.m_pUtxoIn; r.NextUtxoIn())
		if (!OnInput(*r.m_pUtxoIn))
			return false;

	for ( ; r.m_pUtxoOut; r.NextUtxoOut())
		if (!OnOutput(*r.m_pUtxoOut))
			return false;

	return true;
}

bool NodeProcessor::UtxoRecoverSimple::Proceed()
{
	ECC::Mode::Scope scope(ECC::Mode::Fast);
	return m_This.EnumBlocks(*this);
}

bool NodeProcessor::UtxoRecoverEx::OnOutput(uint32_t iKey, const Key::IDV& kidv, const Output& x)
{
	Value& v0 = m_Map[x.m_Commitment];
	if (v0.m_Count)
		v0.m_Count++; // ignore overflow possibility
	else
	{
		v0.m_Kidv = kidv;
		v0.m_iKey = iKey;;
		v0.m_Count = 1;
	}

	return true;
}

bool NodeProcessor::UtxoRecoverEx::OnInput(const Input& x)
{
	UtxoMap::iterator it = m_Map.find(x.m_Commitment);
	if (m_Map.end() != it)
	{
		Value& v = it->second;
		assert(v.m_Count);

		if (! --v.m_Count)
			m_Map.erase(it);
	}
	return true;
}

bool NodeProcessor::UtxoRecoverSimple::OnOutput(const Output& x)
{
	Key::IDV kidv;

	for (uint32_t iKey = 0; iKey < m_vKeys.size(); iKey++)
		if (x.Recover(*m_vKeys[iKey], kidv))
			return OnOutput(iKey, kidv, x);

	return true;
}

bool NodeProcessor::UtxoRecoverSimple::OnInput(const Input& x)
{
	return true; // ignore
}

} // namespace beam
