#pragma once

#include "storage.h"
#include "ecc_native.h"

namespace beam {

/////////////////////////////
// RadixTree
uint16_t RadixTree::Node::get_Bits() const
{
	return m_Bits & ~(s_Clean | s_Leaf);
}

const uint8_t* RadixTree::Node::get_Key() const
{
	return (s_Leaf & m_Bits) ? ((Leaf*) this)->m_pKeyArr : ((Joint*) this)->m_pKeyPtr;
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

uint8_t RadixTree::CursorBase::get_BitRaw(const uint8_t* p0) const
{
	return p0[m_nBits >> 3] >> (7 ^ (7 & m_nBits));
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

		const uint8_t* pKeyNode = p->get_Key();

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


	memcpy(pN->m_pKeyArr, pKey, (nBits + 7) >> 3);

	if (cu.m_nPtrs)
	{
		cu.Invalidate();

		uint32_t iC = cu.get_Bit(pKey);

		Node* p = cu.m_pp[cu.m_nPtrs - 1];
		assert(p);

		const uint8_t* pKey1 = p->get_Key();
		assert(cu.get_Bit(pKey1) != iC);

		// split
		Joint* pJ = CreateJoint();
		pJ->m_pKeyPtr = /*pN->m_pKeyArr*/pKey1;
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

	const uint8_t* pKeyDead = p->m_pKeyArr;

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
				const uint8_t* pKey1 = (p->m_Bits & Node::s_Leaf) ? ((Leaf*) p)->m_pKeyArr : ((Joint*) p)->m_pKeyPtr;
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

/////////////////////////////
// UtxoTree
void UtxoTree::get_Hash(Merkle::Hash& hv)
{
	Node* p = get_Root();
	if (p)
		hv = get_Hash(*p, hv);
	else
		hv = ECC::Zero;
}

const Merkle::Hash& UtxoTree::get_Hash(Node& n, Merkle::Hash& hv)
{
	if (Node::s_Leaf & n.m_Bits)
	{
		MyLeaf& x = (MyLeaf&) n;
		x.m_Bits |= Node::s_Clean;

		ECC::Hash::Processor hp;
		hp.Write(x.m_pKeyArr, Key::s_Bytes); // whole description of the UTXO
		hp << x.m_Count;

		hp >> hv;
		return hv;

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

} // namespace beam
