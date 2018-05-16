#include "node_processor.h"
#include "../utility/serialize.h"
#include "../core/ecc_native.h"
#include "../core/serialization_adapters.h"

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

void NodeProcessor::Initialize(const char* szPath)
{
	m_DB.Open(szPath);

	// Load all th 'live' data
	{
		NodeDB::WalkerSpendable wsp(m_DB);
		for (m_DB.EnumUnpsent(wsp); wsp.MoveNext(); )
		{
			assert(wsp.m_nUnspentCount);
			if (!wsp.m_Key.n)
				OnCorrupted();

			uint8_t nType = *(uint8_t*) wsp.m_Key.p;
			((uint8_t*&) wsp.m_Key.p)++;
			wsp.m_Key.n--;

			switch (nType)
			{
			case DbType::Utxo:
				{
					if (UtxoTree::Key::s_Bytes != wsp.m_Key.n)
						OnCorrupted();

					static_assert(sizeof(UtxoTree::Key) == UtxoTree::Key::s_Bytes, "");
					const UtxoTree::Key& key = *(UtxoTree::Key*) wsp.m_Key.p;

					UtxoTree::Cursor cu;
					bool bCreate = true;

					m_Utxos.Find(cu, key, bCreate)->m_Value.m_Count = wsp.m_nUnspentCount;
					assert(bCreate);
				}
				break;

			case DbType::Kernel:
				{
					if (sizeof(Merkle::Hash) != wsp.m_Key.n)
						OnCorrupted();

					const Merkle::Hash& key = *(Merkle::Hash*) wsp.m_Key.p;

					RadixHashOnlyTree::Cursor cu;
					bool bCreate = true;

					m_Kernels.Find(cu, key, bCreate);
					assert(bCreate);
				}
				break;

			default:
				OnCorrupted();
			}
		}
	}

	NodeDB::Transaction t(m_DB);
	TryGoUp();
	t.Commit();
}

void NodeProcessor::EnumCongestions()
{
	NodeDB::StateID sidPos;
	if (!m_DB.get_Cursor(sidPos))
		sidPos.m_Height = 0;

	// request all potentially missing data
	NodeDB::WalkerState ws(m_DB);
	for (m_DB.EnumTips(ws); ws.MoveNext(); )
	{
		NodeDB::StateID& sid = ws.m_Sid; // alias
		if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(sid.m_Row))
			continue;

		if (sid.m_Height < sidPos.m_Height)
			continue; // not interested in tips behind the current cursor

		bool bBlock = true;

		while (sid.m_Height)
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
		NodeDB::StateID sidPos, sidTrg;
		m_DB.get_Cursor(sidPos);

		{
			NodeDB::WalkerState ws(m_DB);
			m_DB.EnumFunctionalTips(ws);

			if (!ws.MoveNext())
			{
				assert(!sidPos.m_Row);
				break; // nowhere to go
			}
			sidTrg = ws.m_Sid;
		}

		assert(sidTrg.m_Height >= sidPos.m_Height);

		// Calculate the path
		std::vector<uint64_t> vPath;
		while (sidTrg.m_Row != sidPos.m_Row)
		{
			assert(sidTrg.m_Row);
			vPath.push_back(sidTrg.m_Row);

			if (sidPos.m_Row && (sidPos.m_Height == sidTrg.m_Height))
			{
				Rollback(sidPos);
				bDirty = true;

				if (!m_DB.get_Prev(sidPos))
					ZeroObject(sidPos);
			}

			if (!m_DB.get_Prev(sidTrg))
				ZeroObject(sidTrg);
		}

		bool bPathOk = true;

		for (size_t i = vPath.size(); i--; )
		{
			if (sidPos.m_Row)
				sidPos.m_Height++;
			sidPos.m_Row = vPath[i];

			bDirty = true;
			if (!GoForward(sidPos))
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
		NodeDB::StateID sidPos;
		m_DB.get_Cursor(sidPos);
		PruneOld(sidPos.m_Height);

		OnNewState();
	}
}

void NodeProcessor::PruneOld(Height h)
{
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
		uint64_t rowid;

		{
			NodeDB::WalkerState ws(m_DB);
			m_DB.EnumStatesAt(ws, hFossil);
			if (!ws.MoveNext())
				OnCorrupted();

			rowid = ws.m_Sid.m_Row;

			if (ws.MoveNext())
			{
				if (!hFossil)
					break; // several genesis blocks. Currently not blocked.

				OnCorrupted();
			}
		}

		assert(m_DB.GetStateFlags(rowid) & NodeDB::StateFlags::Active);

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

struct NodeProcessor::RollbackData
{
	// helper structures for rollback
	struct Utxo {
		Height m_Maturity; // the extra info we need to restore an UTXO, in addition to the Input.
	};

	ByteBuffer m_Buf;
	Utxo* m_pUtxo;

	Utxo* get_BufAs() const
	{
		assert(!m_Buf.empty());
		return (Utxo*) &m_Buf.at(0);
	}

	size_t get_Utxos() const { return m_pUtxo - get_BufAs(); }
};

bool NodeProcessor::HandleBlock(const NodeDB::StateID& sid, bool bFwd)
{
	ByteBuffer bb;
	RollbackData rbData;
	m_DB.GetStateBlock(sid.m_Row, bb, rbData.m_Buf);

	Block::Body block;
	try {

		Deserializer der;
		der.reset(bb.empty() ? NULL : &bb.at(0), bb.size());
		der & block;
	}
	catch (const std::exception&) {
		return false;
	}

	bb.clear();

	bool bFirstTime = false;
	Block::SystemState::Full s;

	if (bFwd)
	{
		size_t n = std::max(size_t(1), block.m_vInputs.size() * sizeof(RollbackData::Utxo));
		if (rbData.m_Buf.size() != n)
		{
			bFirstTime = true;

			m_DB.get_State(sid.m_Row, s);

			Merkle::Proof proof;
			m_DB.get_Proof(proof, sid, sid.m_Height);

			Merkle::Interpret(s.m_Prev, proof);
			if (s.m_History != s.m_Prev)
				return false; // The state (even the header) is formed incorrectly!

			if (!block.IsValid(sid.m_Height, sid.m_Height))
				return false;

			rbData.m_Buf.resize(n);
		}
	} else
		assert(!rbData.m_Buf.empty());


	rbData.m_pUtxo = rbData.get_BufAs();
	if (!bFwd)
		rbData.m_pUtxo += block.m_vInputs.size();

	bool bOk = HandleValidatedTx(block, sid.m_Height, bFwd, rbData);

	if (bFirstTime && bOk)
	{
		// check the validity of state description.
		Merkle::Hash hv;
		get_CurrentLive(hv);

		if (s.m_LiveObjects != hv)
			bOk = false;

		if (bOk)
			m_DB.SetStateRollback(sid.m_Row, rbData.m_Buf);
		else
			HandleValidatedTx(block, sid.m_Height, false, rbData);
	}

	if (bOk)
	{
		ECC::Scalar kOffset;
		NodeDB::Blob blob(kOffset.m_Value.m_pData, sizeof(kOffset.m_Value.m_pData));

		if (!m_DB.ParamGet(NodeDB::ParamID::StateExtra, NULL, &blob))
			kOffset.m_Value = ECC::Zero;

		ECC::Scalar::Native k(kOffset), k2(block.m_Offset);
		if (!bFwd)
			k2 = -k2;

		k += k2;
		kOffset = k;

		m_DB.ParamSet(NodeDB::ParamID::StateExtra, NULL, &blob);
	}

	return bOk;
}

bool NodeProcessor::HandleValidatedTx(const TxBase& tx, Height h, bool bFwd, RollbackData& rbData)
{
	size_t nInp = 0, nOut = 0, nKrnInp = 0, nKrnOut = 0;

	bool bOk = true;
	if (bFwd)
	{
		for ( ; nInp < tx.m_vInputs.size(); nInp++)
			if (!HandleBlockElement(*tx.m_vInputs[nInp], bFwd, h, rbData))
			{
				bOk = false;
				break;
			}
	} else
	{
		nInp = tx.m_vInputs.size();
		nOut = tx.m_vOutputs.size();
		nKrnInp = tx.m_vKernelsInput.size();
		nKrnOut = tx.m_vKernelsOutput.size();
	}

	if (bFwd && bOk)
	{
		for ( ; nOut < tx.m_vOutputs.size(); nOut++)
			if (!HandleBlockElement(*tx.m_vOutputs[nOut], h, bFwd))
			{
				bOk = false;
				break;
			}
	}

	if (bFwd && bOk)
	{
		for ( ; nKrnInp < tx.m_vKernelsInput.size(); nKrnInp++)
			if (!HandleBlockElement(*tx.m_vKernelsInput[nKrnInp], bFwd, true))
			{
				bOk = false;
				break;
			}
	}

	if (bFwd && bOk)
	{
		for (; nKrnOut < tx.m_vKernelsOutput.size(); nKrnOut++)
			if (!HandleBlockElement(*tx.m_vKernelsOutput[nKrnOut], bFwd, false))
			{
				bOk = false;
				break;
			}
	}

	if (!(bFwd && bOk))
	{
		// Rollback all the changes. Must succeed!
		while (nKrnOut--)
			HandleBlockElement(*tx.m_vKernelsOutput[nKrnOut], false, false);

		while (nKrnInp--)
			HandleBlockElement(*tx.m_vKernelsInput[nKrnInp], false, true);

		while (nOut--)
			HandleBlockElement(*tx.m_vOutputs[nOut], h, false);

		while (nInp--)
			HandleBlockElement(*tx.m_vInputs[nInp], false, h, rbData);
	}

	return bOk;
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

bool NodeProcessor::HandleBlockElement(const Input& v, bool bFwd, Height h, RollbackData& rbData)
{
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;

	if (bFwd)
		d.m_Maturity = 0; // find min
	else
	{
		const RollbackData::Utxo& x = *--rbData.m_pUtxo;
		d.m_Maturity = x.m_Maturity;
	}

	SpendableKey<UtxoTree::Key, DbType::Utxo> skey;
	skey.m_Key = d;

	UtxoTree::Cursor cu;
	bool bCreate = !bFwd;
	UtxoTree::MyLeaf* p = m_Utxos.Find(cu, skey.m_Key, bCreate);

	if (!p && bFwd)
	{
		// try to find the closest match
		struct Traveler :public UtxoTree::ITraveler
		{
			UtxoTree::MyLeaf* m_pLeaf;
			virtual bool OnLeaf(const RadixTree::Leaf& x) override
			{
				m_pLeaf = (UtxoTree::MyLeaf*) &x;
				return false; // stop iteration
			}
		};

		Traveler t;
		if (UtxoTree::Traverse(cu, t))
			return false; // not found

		d = t.m_pLeaf->m_Key;

		if (v.m_Commitment != d.m_Commitment)
			return false; // not found

		if (d.m_Maturity > h)
			return false; // not mature enough!

		p = t.m_pLeaf;
		skey.m_Key = t.m_pLeaf->m_Key;

	}

	cu.Invalidate();

	if (bFwd)
	{
		assert(p->m_Value.m_Count); // we don't store zeroes

		if (! --p->m_Value.m_Count)
			m_Utxos.Delete(cu);

		rbData.m_pUtxo->m_Maturity = d.m_Maturity;
		rbData.m_pUtxo++;

	} else
	{
		if (bCreate)
			p->m_Value.m_Count = 1;
		else
			p->m_Value.m_Count++;
	}

	m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), 0, bFwd ? -1 : 1);
	return true;
}

bool NodeProcessor::HandleBlockElement(const Output& v, Height h, bool bFwd)
{
	UtxoTree::Key::Data d;
	d.m_Commitment = v.m_Commitment;
	d.m_Maturity = h;
	d.m_Maturity += v.m_Coinbase ? Block::s_MaturityCoinbase : Block::s_MaturityStd;

	// add explicit incubation offset, beware of overflow attack (to cheat on maturity settings)
	Height hSum = d.m_Maturity + v.m_Incubation;
	d.m_Maturity = (d.m_Maturity <= hSum) ? hSum : Height(-1);

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

			Serializer ser;
			if (v.m_pConfidential)
				ser & *v.m_pConfidential;
			else
				ser & *v.m_pPublic;

			SerializeBuffer sb = ser.buffer();

			m_DB.AddSpendable(blob, NodeDB::Blob(sb.first, (uint32_t) sb.second), 1, 1);
		}
		else
		{
			p->m_Value.m_Count++;
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

bool NodeProcessor::HandleBlockElement(const TxKernel& v, bool bFwd, bool bIsInput)
{
	bool bAdd = (bFwd != bIsInput);

	SpendableKey<Merkle::Hash, DbType::Kernel> skey;
	v.get_HashTotal(skey.m_Key);

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
			Serializer ser;
			ser & v;
			SerializeBuffer sb = ser.buffer();

			m_DB.AddSpendable(blob, NodeDB::Blob(sb.first, (uint32_t) sb.second), 1, 1);
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

	rbData.m_pUtxo = rbData.get_BufAs();

	for (size_t n = 0; n < block.m_vInputs.size(); n++)
	{
		const Input& v = *block.m_vInputs[n];

		UtxoTree::Key::Data d;
		d.m_Commitment = v.m_Commitment;
		d.m_Maturity = rbData.m_pUtxo[n].m_Maturity;


		SpendableKey<UtxoTree::Key, DbType::Utxo> skey;
		skey.m_Key = d;

		m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), -1, 0);
	}

	for (size_t n = 0; n < block.m_vKernelsInput.size(); n++)
	{
		SpendableKey<Merkle::Hash, DbType::Kernel> skey;
		block.m_vKernelsInput[n]->get_HashTotal(skey.m_Key);

		m_DB.ModifySpendable(NodeDB::Blob(&skey, sizeof(skey)), -1, 0);
	}
}

bool NodeProcessor::GoForward(const NodeDB::StateID& sid)
{
	if (HandleBlock(sid, true))
	{
		m_DB.MoveFwd(sid);
		return true;
	}

	m_DB.DelStateBlock(sid.m_Row);
	m_DB.SetStateNotFunctional(sid.m_Row);

	PeerID peer;
	if (m_DB.get_Peer(sid.m_Row, peer))
	{
		m_DB.set_Peer(sid.m_Row, NULL);
		OnPeerInsane(peer);
	}

	return false;
}

void NodeProcessor::Rollback(const NodeDB::StateID& sid)
{
	if (!HandleBlock(sid, false))
		OnCorrupted();

	NodeDB::StateID sid2(sid);
	m_DB.MoveBack(sid2);
}

bool NodeProcessor::get_CurrentState(Block::SystemState::Full& s)
{
	NodeDB::StateID sid;
	if (!m_DB.get_Cursor(sid))
		return false;

	m_DB.get_State(sid.m_Row, s);
	return true;
}

bool NodeProcessor::get_CurrentState(Block::SystemState::ID& id)
{
	Block::SystemState::Full s;
	if (!get_CurrentState(s))
		return false;

	s.get_ID(id);
	return true;
}

bool NodeProcessor::IsRelevantHeight(Height h)
{
	uint64_t hFossil = m_DB.ParamIntGetDef(NodeDB::ParamID::FossilHeight);
	return !hFossil || (h > hFossil);
}

bool NodeProcessor::OnState(const Block::SystemState::Full& s, const PeerID& peer)
{
	if (!IsRelevantHeight(s.m_Height))
		return false;

	Block::SystemState::ID id;
	s.get_ID(id);
	if (m_DB.StateFindSafe(id))
		return false;

	NodeDB::Transaction t(m_DB);
	uint64_t rowid = m_DB.InsertState(s);
	m_DB.set_Peer(rowid, &peer);
	t.Commit();

	return true;
}

bool NodeProcessor::OnBlock(const Block::SystemState::ID& id, const NodeDB::Blob& block, const PeerID& peer)
{
	uint64_t rowid = m_DB.StateFindSafe(id);
	if (!rowid)
		return false;

	if (NodeDB::StateFlags::Functional & m_DB.GetStateFlags(rowid))
		return false;

	NodeDB::Transaction t(m_DB);

	m_DB.SetStateBlock(rowid, block);
	m_DB.SetStateFunctional(rowid);
	m_DB.set_Peer(rowid, &peer);

	if (NodeDB::StateFlags::Reachable & m_DB.GetStateFlags(rowid))
		TryGoUp();

	t.Commit();

	return true;
}

void NodeProcessor::DeriveKey(ECC::Scalar::Native& out, const ECC::Kdf& kdf, Height h, KeyType::Enum eType, uint32_t nIdx /* = 0 */)
{
	kdf.DeriveKey(out, h, eType, nIdx);
}

bool NodeProcessor::IsStateNeeded(const Block::SystemState::ID& id)
{
	return IsRelevantHeight(id.m_Height) && !m_DB.StateFindSafe(id);
}

/////////////////////////////
// TxPool
struct SerializerSizeCounter
{
	struct Counter
	{
		size_t m_Value;

		size_t write(const void *ptr, const size_t size)
		{
			m_Value += size;
			return size;
		}

	} m_Counter;

	yas::binary_oarchive<Counter, SERIALIZE_OPTIONS> _oa;


	SerializerSizeCounter() : _oa(m_Counter)
	{
		m_Counter.m_Value = 0;
	}

	template <typename T> SerializerSizeCounter& operator & (const T& object)
	{
		_oa & object;
		return *this;
	}
};

bool NodeProcessor::TxPool::AddTx(Transaction::Ptr&& pValue, Height h)
{
	assert(pValue);

	TxBase::Context ctx;
	if (!pValue->IsValid(ctx))
		return false;

	if ((h < ctx.m_hMin) || (h > ctx.m_hMax))
		return false;

	SerializerSizeCounter ssc;
	ssc & pValue;

	Element* p = new Element;
	p->m_Threshold.m_Value	= ctx.m_hMax;
	p->m_Profit.m_Fee	= ctx.m_Fee;
	p->m_Profit.m_nSize	= ssc.m_Counter.m_Value;
	p->m_Tx.m_pValue = std::move(pValue);

	m_setThreshold.insert(p->m_Threshold);
	m_setProfit.insert(p->m_Profit);
	m_setTxs.insert(p->m_Tx);

	return true;
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

bool NodeProcessor::TxPool::Element::Tx::operator < (const Tx& t) const
{
	assert(m_pValue && t.m_pValue);
	// TODO: Normally we can account for tx offset only, different transactions highly unlikely to have the same offset.
	// But current wallet implementation doesn't use the offset.
	return m_pValue->cmp(*t.m_pValue) < 0;
}

bool NodeProcessor::TxPool::Element::Profit::operator < (const Profit& t) const
{
	// TODO: handle overflow. To be precise need to use big-int (128-bit) arithmetics
	return m_Fee * t.m_nSize > t.m_Fee * m_nSize;
}

/////////////////////////////
// Block generation
template <typename T>
void AppendCloneArray(Serializer& ser, std::vector<T>& trg, const std::vector<T>& src)
{
	size_t i0 = trg.size();
	trg.resize(i0 + src.size());

	for (size_t i = 0; i < src.size(); i++)
	{
		ser.reset();
		ser & src[i];
		SerializeBuffer sb = ser.buffer();

		Deserializer der;
		der.reset(sb.first, sb.second);
		der & trg[i0 + i];
	}
}

struct NodeProcessor::BlockBulder
{
	Block::Body m_Block;
	ECC::Scalar::Native m_Offset;

	void AddOutput(const ECC::Scalar::Native& k, Amount val, bool bCoinbase)
	{
		Output::Ptr pOutp(new Output);
		pOutp->Create(k, val, true);
		pOutp->m_Coinbase = bCoinbase;
		m_Block.m_vOutputs.push_back(std::move(pOutp));

		ECC::Scalar::Native km = -k;
		m_Offset += km;
	}
};

Height NodeProcessor::get_NextHeight()
{
	NodeDB::StateID sid;
	m_DB.get_Cursor(sid);

	return sid.m_Row ? (sid.m_Height + 1) : 0;
}

bool NodeProcessor::GenerateNewBlock(TxPool& txp, Block::SystemState::Full& s, ByteBuffer& bbBlock, Amount& fees)
{
	NodeDB::Transaction t(m_DB);

	Height h = get_NextHeight();
	fees = 0;

	size_t nBlockSize = 0;

	// due to (potential) inaccuracy in the block size estimation, our rough estimate - take no more than 95% of allowed block size, minus potential UTXOs to consume fees and coinbase.
	const size_t nRoughExtra = sizeof(ECC::Point) * 2 + sizeof(ECC::RangeProof::Confidential) + sizeof(ECC::RangeProof::Public) + 300;
	const size_t nSizeThreshold = Block::s_MaxBodySize * 95 / 100 - nRoughExtra;

	ByteBuffer bbRbData;
	RollbackData rbData;
	rbData.m_pUtxo = NULL;

	BlockBulder ctxBlock;
	ctxBlock.m_Offset = ECC::Zero;

	Serializer ser;

	for (TxPool::ProfitSet::iterator it = txp.m_setProfit.begin(); txp.m_setProfit.end() != it; )
	{
		TxPool::Element& x = (it++)->get_ParentObj();
		if (nBlockSize + x.m_Profit.m_nSize > nSizeThreshold)
			break;

		Transaction& tx = *x.m_Tx.m_pValue;

		if (!tx.m_vInputs.empty())
		{
			size_t nInputs = rbData.m_pUtxo ? rbData.get_Utxos() : 0;
			rbData.m_Buf.resize(rbData.m_Buf.size() + sizeof(RollbackData::Utxo) * tx.m_vInputs.size());
			rbData.m_pUtxo = rbData.get_BufAs() + nInputs;
		}

		if (HandleValidatedTx(tx, h, true, rbData))
		{
			// Clone the transaction before copying it to the block. We're forced to do this because they're not shared.
			// TODO: Fix this!

			AppendCloneArray(ser, ctxBlock.m_Block.m_vInputs, tx.m_vInputs);
			AppendCloneArray(ser, ctxBlock.m_Block.m_vOutputs, tx.m_vOutputs);
			AppendCloneArray(ser, ctxBlock.m_Block.m_vKernelsInput, tx.m_vKernelsInput);
			AppendCloneArray(ser, ctxBlock.m_Block.m_vKernelsOutput, tx.m_vKernelsOutput);

			fees += x.m_Profit.m_Fee;
			ctxBlock.m_Offset += ECC::Scalar::Native(tx.m_Offset);
			nBlockSize += x.m_Profit.m_nSize;

		} else
			txp.Delete(x); // isn't available in this context
	}

	if (fees)
	{
		ECC::Scalar::Native kFee;
		DeriveKey(kFee, m_Kdf, h, KeyType::Comission);

		ctxBlock.AddOutput(kFee, fees, false);
		verify(HandleBlockElement(*ctxBlock.m_Block.m_vOutputs.back(), h, true));
	}
	else
	{
		ECC::Scalar::Native kKernel;
		DeriveKey(kKernel, m_Kdf, h, KeyType::Kernel);

		TxKernel::Ptr pKrn(new TxKernel);
		pKrn->m_Excess = ECC::Point::Native(ECC::Context::get().G * kKernel);

		ECC::Hash::Value hv;
		pKrn->get_HashForSigning(hv);
		pKrn->m_Signature.Sign(hv, kKernel);
		ctxBlock.m_Block.m_vKernelsOutput.push_back(std::move(pKrn));

		verify(HandleBlockElement(*ctxBlock.m_Block.m_vKernelsOutput.back(), true, false)); // Will fail if kernel key duplicated!

		kKernel = -kKernel;
		ctxBlock.m_Offset += kKernel;
	}

	ECC::Scalar::Native kCoinbase;
	DeriveKey(kCoinbase, m_Kdf, h, KeyType::Coinbase);

	ctxBlock.AddOutput(kCoinbase, Block::s_CoinbaseEmission, true);

	verify(HandleBlockElement(*ctxBlock.m_Block.m_vOutputs.back(), h, true));

	// Finalize block construction.
	if (h)
	{
		m_DB.get_State(sid.m_Row, s);
		s.get_Hash(s.m_Prev);
		m_DB.get_PredictedStatesHash(s.m_History, sid);
	}
	else
	{
		ZeroObject(s.m_Prev);
		ZeroObject(s.m_History);
	}

	s.m_Height = h;
	get_CurrentLive(s.m_LiveObjects);

	// For test: undo the changes, and then redo, using the newly-created block
	verify(HandleValidatedTx(ctxBlock.m_Block, h, false, rbData));

	t.Rollback(); // commit/rollback - doesn't matter, by now the system state should be fully restored to its original state. 

	ctxBlock.m_Block.Sort();
	ctxBlock.m_Block.DeleteIntermediateOutputs();
	ctxBlock.m_Block.m_Offset = ctxBlock.m_Offset;

	ser.reset();
	ser & ctxBlock.m_Block;
	ser.swap_buf(bbBlock);

	return bbBlock.size() <= Block::s_MaxBodySize;
}

} // namespace beam
