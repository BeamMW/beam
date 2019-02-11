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

#include "radixtree.h"
#include "ecc_native.h"

namespace beam {

/////////////////////////////
// RadixTree
uint16_t RadixTree::Node::get_Bits() const
{
	return m_Bits & ~(s_Clean | s_Leaf);
}

const uint8_t* RadixTree::get_NodeKey(const Node& n) const
{
	return (Node::s_Leaf & n.m_Bits) ? GetLeafKey(Cast::Up<Leaf>(n)) : Cast::Up<Joint>(n).m_pKeyPtr;
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
		DeleteLeaf(Cast::Up<Leaf>(p));
	else
	{
		Joint* p1 = (Joint*) p;

		for (size_t i = 0; i < _countof(p1->m_ppC); i++)
			DeleteNode(p1->m_ppC[i]);

		DeleteJoint(p1);
	}
}

uint8_t RadixTree::CursorBase::get_BitRawStat(const uint8_t* p0, uint16_t nBit)
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
	Leaf* p = Cast::Up<Leaf>(m_pp[m_nPtrs - 1]);
	assert(Node::s_Leaf & p->m_Bits);
	return *p;
}

void RadixTree::CursorBase::InvalidateElement()
{
	for (uint16_t n = m_nPtrs; n--; )
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
		Joint* pPrev = Cast::Up<Joint>(cu.m_pp[cu.m_nPtrs - 2]);
		assert(pPrev);

		for (size_t i = 0; ; i++)
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

bool RadixTree::Goto(CursorBase& cu, const uint8_t* pKey, uint16_t nBits) const
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

		uint16_t nThreshold = std::min<uint16_t>(cu.m_nBits + p->get_Bits(), nBits);

		for ( ; cu.m_nBits < nThreshold; cu.m_nBits++, cu.m_nPosInLastNode++)
			if (1 & (cu.get_BitRaw(pKey) ^ cu.get_BitRaw(pKeyNode)))
				return false; // no match

		if (cu.m_nBits == nBits)
			return true;

		assert(cu.m_nPosInLastNode == p->get_Bits());

		Joint* pN = Cast::Up<Joint>(p);
		p = pN->m_ppC[cu.get_Bit(pKey)];

		assert(p); // joints should have both children!

		cu.m_pp[cu.m_nPtrs++] = p;
		cu.m_nBits++;
		cu.m_nPosInLastNode = 0;
	}

	return true;
}

RadixTree::Leaf* RadixTree::Find(CursorBase& cu, const uint8_t* pKey, uint16_t nBits, bool& bCreate)
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
		cu.InvalidateElement();

		uint16_t iC = cu.get_Bit(pKey);

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

	cu.InvalidateElement();

	Leaf* p = Cast::Up<Leaf>(cu.m_pp[cu.m_nPtrs - 1]);
	assert(Node::s_Leaf & p->m_Bits);

	const uint8_t* pKeyDead = GetLeafKey(*p);

	ReplaceTip(cu, NULL);
	DeleteLeaf(p);

	if (1 == cu.m_nPtrs)
		assert(!m_pRoot);
	else
	{
		cu.m_nPtrs--;

		Joint* pPrev = Cast::Up<Joint>(cu.m_pp[cu.m_nPtrs - 1]);
		for (size_t i = 0; ; i++)
		{
			assert(i < _countof(pPrev->m_ppC));
			Node* pN = pPrev->m_ppC[i];
			if (pN)
			{
				const uint8_t* pKey1 = get_NodeKey(*pN);
				assert(pKey1 != pKeyDead);

				for (uint16_t j = cu.m_nPtrs; j--; )
				{
					Joint* pPrev2 = Cast::Up<Joint>(cu.m_pp[j]);
					if (pPrev2->m_pKeyPtr != pKeyDead)
						break;

					pPrev2->m_pKeyPtr = pKey1;
				}

				pN->m_Bits += pPrev->m_Bits + 1;
				ReplaceTip(cu, pN);

				DeleteJoint(pPrev);

				break;
			}
		}
	}
}


bool RadixTree::Traverse(const Node& n, ITraveler& t) const
{
	if (t.m_pCu->m_pp)
		t.m_pCu->m_pp[t.m_pCu->m_nPtrs++] = Cast::NotConst(&n);

	uint16_t nBits = n.get_Bits();
	if (nBits)
	{
		const uint8_t* pK = get_NodeKey(n);

		for (size_t iBound = 0; iBound < _countof(t.m_pBound); iBound++)
		{
			const uint8_t*& pB = t.m_pBound[iBound];
			if (!pB)
				continue;

			int nCmp = Cmp(pK, pB, t.m_pCu->m_nBits, nBits);
			if (!nCmp)
				continue;

			if ((nCmp < 0) == !iBound)
				return true;

			pB = NULL;
		}

		t.m_pCu->m_nBits += nBits;
	}

	if (Node::s_Leaf & n.m_Bits)
		return t.OnLeaf(Cast::Up<Leaf>(n));

	nBits = t.m_pCu->m_nBits;
	uint16_t nPtrs = t.m_pCu->m_nPtrs;

	const uint8_t* pBound[2];
	memcpy(pBound, t.m_pBound, sizeof(t.m_pBound));

	const Joint& x = Cast::Up<Joint>(n);
	for (uint8_t i = 0; i < _countof(x.m_ppC); i++)
	{
		bool bSkip = false;

		if (i)
		{
			t.m_pCu->m_nBits = nBits;
			t.m_pCu->m_nPtrs = nPtrs;
		}

		for (size_t iBound = 0; iBound < _countof(t.m_pBound); iBound++)
		{
			const uint8_t*& pB = t.m_pBound[iBound];
			if (i)
				pB = pBound[iBound]; // restore
			if (!pB)
				continue;

			int nCmp = Cmp1(i, pB, t.m_pCu->m_nBits);
			if (!nCmp)
				continue;

			if ((nCmp < 0) == !iBound)
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

int RadixTree::Cmp(const uint8_t* pKey, const uint8_t* pThreshold, uint16_t n0, uint16_t dn)
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

int RadixTree::Cmp1(uint8_t n, const uint8_t* pThreshold, uint16_t n0)
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
		hv = Zero;
}

const Merkle::Hash& RadixHashTree::get_Hash(Node& n, Merkle::Hash& hv)
{
	if (Node::s_Leaf & n.m_Bits)
	{
		const Merkle::Hash& ret = get_LeafHash(n, hv);
		n.m_Bits |= Node::s_Clean;
		return ret;
	}

	MyJoint& x = Cast::Up<MyJoint>(n);
	if (!(Node::s_Clean & x.m_Bits))
	{
		ECC::Hash::Processor hp;

		for (size_t i = 0; i < _countof(x.m_ppC); i++)
		{
			ECC::Hash::Value hvPlaceholder;
			hp << get_Hash(*x.m_ppC[i], hvPlaceholder);
		}

		hp >> x.m_Hash;
		x.m_Bits |= Node::s_Clean;
	}

	return x.m_Hash;
}

void RadixHashTree::get_Proof(Merkle::Proof& proof, const CursorBase& cu)
{
	uint16_t n = cu.get_Depth();
	assert(n);

	Node** pp = cu.get_pp();

	const Node* pPrev = pp[--n];
	size_t nOut = proof.size(); // may already be non-empty, we'll append

	for (proof.resize(nOut + n); n--; nOut++)
	{
		const Joint& x = Cast::Up<Joint>(*pp[n]);

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
	ECC::Hash::Processor()
		<< Blob(key.m_pArr, Key::s_Bytes) // whole description of the UTXO
		<< m_Count
		>> hv;
}

void Input::State::get_ID(Merkle::Hash& hv, const ECC::Point& comm) const
{
	UtxoTree::Key::Data d;
	d.m_Commitment = comm;
	d.m_Maturity = m_Maturity;

	UtxoTree::Key key;
	key = d;

	UtxoTree::Value val;
	val.m_Count = m_Count;
	val.get_Hash(hv, key);
}

const Merkle::Hash& UtxoTree::get_LeafHash(Node& n, Merkle::Hash& hv)
{
	MyLeaf& x = Cast::Up<MyLeaf>(n);
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
			MyLeaf& x = Cast::Up<MyLeaf>(Cast::NotConst(n));
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
	memcpy(m_Commitment.m_X.m_pData, key.m_pArr, m_Commitment.m_X.nBytes);
	const uint8_t* pKey = key.m_pArr + m_Commitment.m_X.nBytes;

	m_Commitment.m_Y = 1 & (pKey[0] >> 7);

	m_Maturity = 0;
	for (size_t i = 0; i < sizeof(m_Maturity); i++, pKey++)
		m_Maturity = (m_Maturity << 8) | (pKey[0] << 1) | (pKey[1] >> 7);

	return *this;
}

UtxoTree::Key& UtxoTree::Key::operator = (const Data& d)
{
	memcpy(m_pArr, d.m_Commitment.m_X.m_pData, d.m_Commitment.m_X.nBytes);

	uint8_t* pKey = m_pArr + d.m_Commitment.m_X.nBytes;
	memset0(pKey, sizeof(m_pArr) - d.m_Commitment.m_X.nBytes);

	if (d.m_Commitment.m_Y)
		pKey[0] |= (1 << 7);

	for (size_t i = 0; i < sizeof(d.m_Maturity); i++)
	{
		uint8_t val = uint8_t(d.m_Maturity >> ((sizeof(d.m_Maturity) - i - 1) << 3));
		pKey[i] |= val >> 1;
		pKey[i + 1] |= (val << 7);
	}

	return *this;
}

} // namespace beam
