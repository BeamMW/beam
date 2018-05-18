#include "storage.h"
#include "ecc_native.h"
#include <assert.h>

namespace beam {

/////////////////////////////
// RadixTree
uint16_t RadixTree::Node::get_Bits() const
{
	return m_Bits & ~(s_Clean | s_Leaf);
}

const uint8_t* RadixTree::get_NodeKey(const Node& n) const
{
	return (Node::s_Leaf & n.m_Bits) ? GetLeafKey((const Leaf&) n) : ((const Joint&) n).m_pKeyPtr;
}

RadixTree::RadixTree()
	:m_pRoot(NULL)
{
}

RadixTree::~RadixTree()
{
	assert(!m_pRoot);
}

void RadixTree::Clear()
{
	if (m_pRoot)
	{
		DeleteNode(m_pRoot);
		m_pRoot = NULL;
	}
}

void RadixTree::DeleteNode(Node* p)
{
	if (Node::s_Leaf & p->m_Bits)
		DeleteLeaf((Leaf*) p);
	else
	{
		Joint* p1 = (Joint*) p;

		for (int i = 0; i < _countof(p1->m_ppC); i++)
			DeleteNode(p1->m_ppC[i]);

		DeleteJoint(p1);
	}
}

uint8_t RadixTree::CursorBase::get_BitRawStat(const uint8_t* p0, uint32_t nBit)
{
	return p0[nBit >> 3] >> (7 ^ (7 & nBit));
}

uint8_t RadixTree::CursorBase::get_BitRaw(const uint8_t* p0) const
{
	return get_BitRawStat(p0, m_nBits);
}

uint8_t RadixTree::CursorBase::get_Bit(const uint8_t* p0) const
{
	return 1 & get_BitRaw(p0);
}

RadixTree::Leaf& RadixTree::CursorBase::get_Leaf() const
{
	assert(m_nPtrs);
	Leaf* p = (Leaf*) m_pp[m_nPtrs - 1];
	assert(Node::s_Leaf & p->m_Bits);
	return *p;
}

void RadixTree::CursorBase::Invalidate()
{
	for (uint32_t n = m_nPtrs; n--; )
	{
		Node* p = m_pp[n];
		assert(p);

		if (!(Node::s_Clean & p->m_Bits))
			break;

		p->m_Bits &= ~Node::s_Clean;
	}
}

void RadixTree::ReplaceTip(CursorBase& cu, Node* pNew)
{
	assert(cu.m_nPtrs);
	Node* pOld = cu.m_pp[cu.m_nPtrs - 1];
	assert(pOld);

	if (cu.m_nPtrs > 1)
	{
		Joint* pPrev = (Joint*) cu.m_pp[cu.m_nPtrs - 2];
		assert(pPrev);

		for (int i = 0; ; i++)
		{
			assert(i < _countof(pPrev->m_ppC));
			if (pPrev->m_ppC[i] == pOld)
			{
				pPrev->m_ppC[i] = pNew;
				break;
			}
		}
	} else
	{
		assert(m_pRoot == pOld);
		m_pRoot = pNew;
	}
}

bool RadixTree::Goto(CursorBase& cu, const uint8_t* pKey, uint32_t nBits) const
{
	Node* p = m_pRoot;

	if (p)
	{
		cu.m_pp[0] = p;
		cu.m_nPtrs = 1;
	} else
		cu.m_nPtrs = 0;

	cu.m_nBits = 0;
	cu.m_nPosInLastNode = 0;

	while (nBits > cu.m_nBits)
	{
		if (!p)
			return false;

		const uint8_t* pKeyNode = get_NodeKey(*p);

		uint32_t nThreshold = std::min(cu.m_nBits + p->get_Bits(), nBits);

		for ( ; cu.m_nBits < nThreshold; cu.m_nBits++, cu.m_nPosInLastNode++)
			if (1 & (cu.get_BitRaw(pKey) ^ cu.get_BitRaw(pKeyNode)))
				return false; // no match

		if (cu.m_nBits == nBits)
			return true;

		assert(cu.m_nPosInLastNode == p->get_Bits());

		Joint* pN = (Joint*) p;
		p = pN->m_ppC[cu.get_Bit(pKey)];

		assert(p); // joints should have both children!

		cu.m_pp[cu.m_nPtrs++] = p;
		cu.m_nBits++;
		cu.m_nPosInLastNode = 0;
	}

	return true;
}

RadixTree::Leaf* RadixTree::Find(CursorBase& cu, const uint8_t* pKey, uint32_t nBits, bool& bCreate)
{
	if (Goto(cu, pKey, nBits))
	{
		bCreate = false;
		return &cu.get_Leaf();
	}

	assert(cu.m_nBits < nBits);

	if (!bCreate)
		return NULL;

	Leaf* pN = CreateLeaf();

	// Guard the allocated leaf. In case exc will be thrown (during possible allocation of a new joint)
	struct Guard
	{
		Leaf* m_pLeaf;
		RadixTree* m_pTree;

		~Guard() {
			if (m_pLeaf)
				m_pTree->DeleteLeaf(m_pLeaf);
		}
	} g;

	g.m_pTree = this;
	g.m_pLeaf = pN;


	memcpy(GetLeafKey(*pN), pKey, (nBits + 7) >> 3);

	if (cu.m_nPtrs)
	{
		cu.Invalidate();

		uint32_t iC = cu.get_Bit(pKey);

		Node* p = cu.m_pp[cu.m_nPtrs - 1];
		assert(p);

		const uint8_t* pKey1 = get_NodeKey(*p);
		assert(cu.get_Bit(pKey1) != iC);

		// split
		Joint* pJ = CreateJoint();
		pJ->m_pKeyPtr = pKey1;
		pJ->m_Bits = cu.m_nPosInLastNode;

		ReplaceTip(cu, pJ);
		cu.m_pp[cu.m_nPtrs - 1] = pJ;

		pN->m_Bits = nBits - (cu.m_nBits + 1);
		p->m_Bits -= cu.m_nPosInLastNode + 1;

		pJ->m_ppC[iC] = pN;
		pJ->m_ppC[!iC] = p;


	} else
	{
		assert(!m_pRoot);
		m_pRoot = pN;
		pN->m_Bits = nBits;
	}

	cu.m_pp[cu.m_nPtrs++] = pN;
	cu.m_nPosInLastNode = pN->m_Bits; // though not really necessary
	cu.m_nBits = nBits;

	pN->m_Bits |= Node::s_Leaf;

	g.m_pLeaf = NULL; // dismissed

	return pN;
}

void RadixTree::Delete(CursorBase& cu)
{
	assert(cu.m_nPtrs);

	cu.Invalidate();

	Leaf* p = (Leaf*) cu.m_pp[cu.m_nPtrs - 1];
	assert(Node::s_Leaf & p->m_Bits);

	const uint8_t* pKeyDead = GetLeafKey(*p);

	ReplaceTip(cu, NULL);
	DeleteLeaf(p);

	if (1 == cu.m_nPtrs)
		assert(!m_pRoot);
	else
	{
		cu.m_nPtrs--;

		Joint* pPrev = (Joint*) cu.m_pp[cu.m_nPtrs - 1];
		for (int i = 0; ; i++)
		{
			assert(i < _countof(pPrev->m_ppC));
			Node* p = pPrev->m_ppC[i];
			if (p)
			{
				const uint8_t* pKey1 = get_NodeKey(*p);
				assert(pKey1 != pKeyDead);

				for (uint32_t j = cu.m_nPtrs; j--; )
				{
					Joint* pPrev2 = (Joint*) cu.m_pp[j];
					if (pPrev2->m_pKeyPtr != pKeyDead)
						break;

					pPrev2->m_pKeyPtr = pKey1;
				}

				p->m_Bits += pPrev->m_Bits + 1;
				ReplaceTip(cu, p);

				DeleteJoint(pPrev);

				break;
			}
		}
	}
}


bool RadixTree::Traverse(const Node& n, ITraveler& t) const
{
	if (t.m_pCu->m_pp)
		t.m_pCu->m_pp[t.m_pCu->m_nPtrs++] = (Node*) &n;

	uint32_t nBits = n.get_Bits();
	if (nBits)
	{
		const uint8_t* pK = get_NodeKey(n);

		for (int iBound = 0; iBound < _countof(t.m_pBound); iBound++)
		{
			const uint8_t*& pB = t.m_pBound[iBound];
			if (!pB)
				continue;

			int n = Cmp(pK, pB, t.m_pCu->m_nBits, nBits);
			if (!n)
				continue;

			if ((n < 0) == !iBound)
				return true;

			pB = NULL;
		}

		t.m_pCu->m_nBits += nBits;
	}

	if (Node::s_Leaf & n.m_Bits)
		return t.OnLeaf((const Leaf&) n);

	nBits = t.m_pCu->m_nBits;
	uint32_t nPtrs = t.m_pCu->m_nPtrs;

	const uint8_t* pBound[2];
	memcpy(pBound, t.m_pBound, sizeof(t.m_pBound));

	const Joint& x = (const Joint&) n;
	for (uint8_t i = 0; i < _countof(x.m_ppC); i++)
	{
		bool bSkip = false;

		if (i)
		{
			t.m_pCu->m_nBits = nBits;
			t.m_pCu->m_nPtrs = nPtrs;
		}

		for (int iBound = 0; iBound < _countof(t.m_pBound); iBound++)
		{
			const uint8_t*& pB = t.m_pBound[iBound];
			if (i)
				pB = pBound[iBound]; // restore
			if (!pB)
				continue;

			int n = Cmp1(i, pB, t.m_pCu->m_nBits);
			if (!n)
				continue;

			if ((n < 0) == !iBound)
			{
				bSkip = true;
				break;
			}

			pB = NULL;
		}

		if (bSkip)
			continue;

		t.m_pCu->m_nBits++;
		if (!Traverse(*x.m_ppC[i], t))
			return false;
	}

	return true;
}

int RadixTree::Cmp(const uint8_t* pKey, const uint8_t* pThreshold, uint32_t n0, uint32_t dn)
{
	for (dn += n0; n0 < dn; n0++)
	{
		uint8_t a = 1 & CursorBase::get_BitRawStat(pKey, n0);
		uint8_t b = 1 & CursorBase::get_BitRawStat(pThreshold, n0);

		if (a < b)
			return -1;
		if (a > b)
			return 1;
	}
	return 0;
}

int RadixTree::Cmp1(uint8_t n, const uint8_t* pThreshold, uint32_t n0)
{
	uint8_t nBit = 1 & CursorBase::get_BitRawStat(pThreshold, n0);

	if (n < nBit)
		return -1;
	if (n > nBit)
		return 1;
	return 0;
}

bool RadixTree::Traverse(ITraveler& t) const
{
	if (!m_pRoot)
		return true;

	CursorBase cuDummy(NULL);
	if (!t.m_pCu)
		t.m_pCu = &cuDummy;

	t.m_pCu->m_nBits = 0;
	t.m_pCu->m_nPtrs = 0;
	t.m_pCu->m_nPosInLastNode = 0;

	return Traverse(*m_pRoot, t);
}

size_t RadixTree::Count() const
{
	struct Traveler
		:public ITraveler
	{
		size_t m_Count;
		virtual bool OnLeaf(const Leaf&) override {
			m_Count++;
			return true;
		}
	} t;

	t.m_Count = 0;
	Traverse(t);
	return t.m_Count;
}

/////////////////////////////
// RadixHashTree
void RadixHashTree::get_Hash(Merkle::Hash& hv)
{
	Node* p = get_Root();
	if (p)
		hv = get_Hash(*p, hv);
	else
		hv = ECC::Zero;
}

const Merkle::Hash& RadixHashTree::get_Hash(Node& n, Merkle::Hash& hv)
{
	if (Node::s_Leaf & n.m_Bits)
	{
		const Merkle::Hash& ret = get_LeafHash(n, hv);
		n.m_Bits |= Node::s_Clean;
		return ret;
	}

	MyJoint& x = (MyJoint&) n;
	if (!(Node::s_Clean & x.m_Bits))
	{
		ECC::Hash::Processor hp;

		for (int i = 0; i < _countof(x.m_ppC); i++)
		{
			ECC::Hash::Value hv;
			hp << get_Hash(*x.m_ppC[i], hv);
		}

		hp >> x.m_Hash;
		x.m_Bits |= Node::s_Clean;
	}

	return x.m_Hash;
}

void RadixHashTree::get_Proof(Merkle::Proof& proof, const CursorBase& cu)
{
	uint32_t n = cu.get_Depth();
	assert(n);

	Node** pp = cu.get_pp();

	const Node* pPrev = pp[--n];
	size_t nOut = proof.size(); // may already be non-empty, we'll append

	for (proof.resize(nOut + n); n--; nOut++)
	{
		const Joint& x = (const Joint&) *pp[n];

		Merkle::Node& node = proof[nOut];
		node.first = (x.m_ppC[0] == pPrev);

		node.second = get_Hash(*x.m_ppC[node.first != false], node.second);

		pPrev = &x;
	}

	assert(proof.size() == nOut);
}

/////////////////////////////
// UtxoTree
void UtxoTree::Value::get_Hash(Merkle::Hash& hv, const Key& key) const
{
	ECC::Hash::Processor hp;
	hp.Write(key.m_pArr, Key::s_Bytes); // whole description of the UTXO
	hp << m_Count;

	hp >> hv;
}

const Merkle::Hash& UtxoTree::get_LeafHash(Node& n, Merkle::Hash& hv)
{
	MyLeaf& x = (MyLeaf&) n;
	x.m_Value.get_Hash(hv, x.m_Key);
	return hv;
}

void UtxoTree::SaveIntenral(ISerializer& s) const
{
	uint32_t n = (uint32_t) Count();
	s.Process(n);

	struct Traveler
		:public ITraveler
	{
		ISerializer* m_pS;
		virtual bool OnLeaf(const Leaf& n) override {
			MyLeaf& x = (MyLeaf&) n;
			m_pS->Process(x.m_Key);
			m_pS->Process(x.m_Value);
			return true;
		}
	} t;
	t.m_pS = &s;
	Traverse(t);
}

void UtxoTree::LoadIntenral(ISerializer& s)
{
	Clear();

	uint32_t n = 0;
	s.Process(n);

	Key pKey[2];

	for (uint32_t i = 0; i < n; i++)
	{
		Key& key = pKey[1 & i];
		const Key& keyPrev = pKey[!(1 & i)];

		s.Process(key);

		if (i)
		{
			// must be in ascending order
			if (keyPrev.cmp(key) >= 0)
				throw std::runtime_error("incorrect order");
		}

		Cursor cu;
		bool bCreate = true;
		MyLeaf* p = Find(cu, key, bCreate);

		p->m_Value.m_Count = 0;
		s.Process(p->m_Value);
	}
}

int UtxoTree::Key::cmp(const Key& k) const
{
	return memcmp(m_pArr, k.m_pArr, sizeof(m_pArr));
}

UtxoTree::Key::Data& UtxoTree::Key::Data::operator = (const Key& key)
{
	memcpy(m_Commitment.m_X.m_pData, key.m_pArr, sizeof(m_Commitment.m_X.m_pData));
	const uint8_t* pKey = key.m_pArr + sizeof(m_Commitment.m_X.m_pData);

	m_Commitment.m_Y	= (1 & (pKey[0] >> 7)) != 0;

	m_Maturity = 0;
	for (int i = 0; i < sizeof(m_Maturity); i++, pKey++)
		m_Maturity = (m_Maturity << 8) | (pKey[0] << 1) | (pKey[1] >> 7);

	return *this;
}

UtxoTree::Key& UtxoTree::Key::operator = (const Data& d)
{
	memcpy(m_pArr, d.m_Commitment.m_X.m_pData, sizeof(d.m_Commitment.m_X.m_pData));

	uint8_t* pKey = m_pArr + sizeof(d.m_Commitment.m_X.m_pData);
	memset0(pKey, sizeof(m_pArr) - sizeof(d.m_Commitment.m_X.m_pData));

	if (d.m_Commitment.m_Y)
		pKey[0] |= (1 << 7);

	for (int i = 0; i < sizeof(d.m_Maturity); i++)
	{
		uint8_t val = uint8_t(d.m_Maturity >> ((sizeof(d.m_Maturity) - i - 1) << 3));
		pKey[i] |= val >> 1;
		pKey[i + 1] |= (val << 7);
	}

	return *this;
}

/////////////////////////////
// Merkle::Mmr
void Merkle::Mmr::Append(const Hash& hv)
{
	Hash hv1 = hv;
	uint64_t n = m_Count;

	for (uint8_t nHeight = 0; ; nHeight++, n >>= 1)
	{
		SaveElement(hv1, n, nHeight);
		if (!(1 & n))
			break;

		Hash hv0;
		LoadElement(hv0, n ^ 1, nHeight);

		Interpret(hv1, hv0, false);
	}

	m_Count++;
}

void Merkle::Mmr::get_PredictedHash(Hash& hv, const Hash& hvAppend) const
{
	hv = hvAppend;
	uint64_t n = m_Count;

	for (uint8_t nHeight = 0; n; nHeight++, n >>= 1)
		if (1 & n)
		{
			Hash hv0;
			LoadElement(hv0, n ^ 1, nHeight);

			Interpret(hv, hv0, false);
		}
}

void Merkle::Mmr::get_Hash(Hash& hv) const
{
	if (!get_HashForRange(hv, 0, m_Count))
		hv = ECC::Zero;
}

bool Merkle::Mmr::get_HashForRange(Hash& hv, uint64_t n0, uint64_t n) const
{
	bool bEmpty = true;

	for (uint8_t nHeight = 0; n; nHeight++, n >>= 1, n0 >>= 1)
		if (1 & n)
		{
			Hash hv0;
			LoadElement(hv0, n0 + n ^ 1, nHeight);

			if (bEmpty)
			{
				hv = hv0;
				bEmpty = false;
			}
			else
				Interpret(hv, hv0, false);
		}

	return !bEmpty;
}

void Merkle::Mmr::get_Proof(Proof& proof, uint64_t i) const
{
	assert(i < m_Count);

	uint64_t n = m_Count;
	for (uint8_t nHeight = 0; n; nHeight++, n >>= 1, i >>= 1)
	{
		Node node;
		node.first = !(i & 1);

		uint64_t nSibling = i ^ 1;
		bool bFullSibling = !node.first;

		if (!bFullSibling)
		{
			uint64_t n0 = nSibling << nHeight;
			if (n0 >= m_Count)
				continue;

			uint64_t nRemaining = m_Count - n0;
			if (nRemaining >> nHeight)
				bFullSibling = true;
			else
				verify(get_HashForRange(node.second, n0, nRemaining));
		}

		if (bFullSibling)
			LoadElement(node.second, nSibling, nHeight);

		proof.push_back(std::move(node)); // TODO: avoid copy?
	}
}

/////////////////////////////
// Merkle::DistributedMmr
struct Merkle::DistributedMmr::Impl
	:public Mmr
{
	Impl(DistributedMmr& x)
		:m_This(x)
		,m_nDepth(0)
	{
		m_Count = x.m_Count;
		m_pNodes[0].m_Key = x.m_kLast;
		m_pNodes[0].m_nIdx = m_Count - 1;
	}

	DistributedMmr& m_This;
	Hash* m_pTrgHash;
	Key* m_pTrgKey;

	static const uint32_t nDepthMax = sizeof(uint64_t) << 4; // 128, extra cautions because of multiple-peaks structure

	struct Node {
		Key m_Key;
		uint64_t m_nIdx;
	};

	Node m_pNodes[nDepthMax];
	uint32_t m_nDepth;

	Key FindElement(uint64_t nIdx);

	static uint8_t get_Height(uint64_t n)
	{
		uint8_t h = 0;
		for ( ; 1 & n; n >>= 1)
			h++;
		return h;
	}

	static uint8_t get_NextPeak(uint64_t& n0)
	{
		uint8_t h = get_Height(n0);
		n0 -= (uint64_t(1) << h) - 1;
		return h;
	}

	// Mmr
	virtual void LoadElement(Hash&, uint64_t nIdx, uint8_t nHeight) const override;
	virtual void SaveElement(const Hash&, uint64_t nIdx, uint8_t nHeight) override;
};

uint32_t Merkle::DistributedMmr::get_NodeSize(uint64_t n)
{
	uint8_t h = Impl::get_NextPeak(n);
	uint32_t nSize = h * (sizeof(Hash) + sizeof(Key)); // 1st peak - must contain hashes and refs

	if (n)
		nSize += sizeof(Key); // ref to next peak

	return nSize;
}

void Merkle::DistributedMmr::Impl::SaveElement(const Hash& hash, uint64_t nIdx, uint8_t nHeight)
{
	if (nHeight) // we don't store explicitly the hash of the element itself.
	{
		m_pTrgHash[nHeight - 1] = hash;

		assert(m_pNodes[m_nDepth].m_nIdx == m_Count - (uint64_t(1) << (nHeight - 1)));
		m_pTrgKey[nHeight - 1] = m_pNodes[m_nDepth].m_Key;
	}
}

void Merkle::DistributedMmr::Impl::LoadElement(Hash& hash, uint64_t nIdx, uint8_t nHeight) const
{
	// index of the element that carries the information
	nIdx = ((nIdx + 1) << nHeight) - 1;
	Key k = ((Impl*) this)->FindElement(nIdx);

	if (nHeight)
		hash = ((const Hash*) m_This.get_NodeData(k))[nHeight - 1];
	else
		m_This.get_NodeHash(hash, k);
}

Merkle::DistributedMmr::Key Merkle::DistributedMmr::Impl::FindElement(uint64_t nIdx)
{
	while (true)
	{
		Node& n = m_pNodes[m_nDepth];
		uint64_t nPos = n.m_nIdx;

		if (nPos == nIdx)
			return n.m_Key;

		if (nPos < nIdx)
		{
			assert(m_nDepth);
			m_nDepth--;
			continue;
		}

		assert(m_nDepth + 1 < _countof(m_pNodes));
		Node& n2 = m_pNodes[++m_nDepth];

		uint64_t nPos1 = nPos;
		uint8_t h = get_NextPeak(nPos);
		const Key* pK = (Key*) (((Hash*) m_This.get_NodeData(n.m_Key)) + h);

		if (nPos <= nIdx)
		{
			uint64_t dn = nPos1 - nPos + 1;
			while (true)
			{
				dn >>= 1;
				assert(h);

				if (nIdx < nPos + dn)
				{
					n2.m_nIdx = nPos + dn - 1;
					n2.m_Key = pK[h - 1];
					break;
				}

				h--;
				nPos += dn;
			}

		} else
		{
			n2.m_nIdx = nPos - 1;
			n2.m_Key = pK[h];
		}
	}
}

void Merkle::DistributedMmr::Append(Key k, void* pBuf, const Hash& hash)
{
	uint64_t n = m_Count;
	uint8_t h = Impl::get_NextPeak(n);

	Impl impl(*this);
	impl.m_pTrgHash = (Hash*) pBuf;
	impl.m_pTrgKey = (Key*) (impl.m_pTrgHash + h);

	impl.Append(hash);


	if (n)
		impl.m_pTrgKey[h] = impl.FindElement(n - 1);

	m_Count++;
	m_kLast = k;
}

void Merkle::DistributedMmr::get_Hash(Hash& hv) const
{
	Impl impl((DistributedMmr&) *this);
	impl.get_Hash(hv);
}

void Merkle::DistributedMmr::get_PredictedHash(Hash& hv, const Hash& hvAppend) const
{
	Impl impl((DistributedMmr&) *this);
	impl.get_PredictedHash(hv, hvAppend);
}

void Merkle::DistributedMmr::get_Proof(Proof& proof, uint64_t i) const
{
	Impl impl((DistributedMmr&) *this);
	impl.get_Proof(proof, i);
}

/////////////////////////////
// Merkle::CompactMmr
void Merkle::CompactMmr::get_Hash(Hash& hv) const
{
	uint32_t i = (uint32_t) m_vNodes.size();
	if (i)
	{
		for (hv = m_vNodes[--i]; i; )
			Interpret(hv, m_vNodes[--i], false);
	} else
		ZeroObject(hv);
}

void Merkle::CompactMmr::get_PredictedHash(Hash& hv, const Hash& hvAppend) const
{
	hv = hvAppend;
	uint64_t n = m_Count;
	size_t iPos = m_vNodes.size();

	for (uint8_t nHeight = 0; n; nHeight++, n >>= 1)
		if (1 & n)
		{
			assert(n > 0);
			Interpret(hv, m_vNodes[--iPos], false);
		}
	assert(!iPos);
}

void Merkle::CompactMmr::Append(const Hash& hv)
{
	Hash hv1 = hv;
	uint64_t n = m_Count;

	for (uint8_t nHeight = 0; ; nHeight++, n >>= 1)
	{
		if (!(1 & n))
			break;

		assert(!m_vNodes.empty());

		Interpret(hv1, m_vNodes.back(), false);
		m_vNodes.pop_back();
	}

	m_vNodes.push_back(hv1);
	m_Count++;
}

//void Merkle::CompactMmr::Append(CompactMmr& out, Hash& hv) const
//{
//}

} // namespace beam
