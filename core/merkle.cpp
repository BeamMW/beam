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

#include "common.h"
#include "merkle.h"
#include "ecc_native.h"

namespace beam {
namespace Merkle {

void Interpret(Hash& out, const Hash& hLeft, const Hash& hRight)
{
	ECC::Hash::Processor() << hLeft << hRight >> out;
}

void Interpret(Hash& hOld, const Hash& hNew, bool bNewOnRight)
{
	if (bNewOnRight)
		Interpret(hOld, hOld, hNew);
	else
		Interpret(hOld, hNew, hOld);
}

void Interpret(Hash& hash, const Node& n)
{
	Interpret(hash, n.second, n.first);
}

void Interpret(Hash& hash, const Proof& p)
{
	for (Proof::const_iterator it = p.begin(); p.end() != it; it++)
		Interpret(hash, *it);
}


/////////////////////////////
// Mmr
void Mmr::Append(const Hash& hv)
{
	Hash hv1 = hv;

	Position pos;
	pos.X = m_Count;
	for (pos.H = 0; ; pos.H++, pos.X >>= 1)
	{
		SaveElement(hv1, pos);
		if (!(1 & pos.X))
			break;

		Hash hv0;
		pos.X ^= 1;
		LoadElement(hv0, pos);

		Interpret(hv1, hv0, false);
	}

	m_Count++;
}

void Mmr::get_PredictedHash(Hash& hv, const Hash& hvAppend) const
{
	hv = hvAppend;

	Position pos;
	pos.X = m_Count;
	for (pos.H = 0; pos.X; pos.H++, pos.X >>= 1)
		if (1 & pos.X)
		{
			Hash hv0;
			pos.X ^= 1;
			LoadElement(hv0, pos);

			Interpret(hv, hv0, false);
		}
}

void Mmr::get_Hash(Hash& hv) const
{
	if (!get_HashForRange(hv, 0, m_Count))
		hv = Zero;
}

bool Mmr::get_HashForRange(Hash& hv, uint64_t n0, uint64_t n) const
{
	bool bEmpty = true;

	Position pos;
	for (pos.H = 0; n; pos.H++, n >>= 1, n0 >>= 1)
		if (1 & n)
		{
			Hash hv0;
			pos.X = (n0 + n) ^ 1;
			LoadElement(hv0, pos);

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

void Mmr::get_Proof(Proof& proof, uint64_t i) const
{
	ProofBuilderStd bld;
	bld.m_Proof.swap(proof);

    BEAM_VERIFY(get_Proof(bld, i));
	bld.m_Proof.swap(proof);
}

bool Mmr::get_Proof(IProofBuilder& proof, uint64_t i) const
{
	assert(i < m_Count);

	uint64_t n = m_Count;
	Position pos;
	for (pos.H = 0; n; pos.H++, n >>= 1, i >>= 1)
	{
		Node node;
		node.first = !(i & 1);

		pos.X = i ^ 1;
		bool bFullSibling = !node.first;

		if (!bFullSibling)
		{
			uint64_t n0 = pos.X << pos.H;
			if (n0 >= m_Count)
				continue;

			uint64_t nRemaining = m_Count - n0;
			if (nRemaining >> pos.H)
				bFullSibling = true;
			else
                BEAM_VERIFY(get_HashForRange(node.second, n0, nRemaining));
		}

		if (bFullSibling)
			LoadElement(node.second, pos);

		if (!proof.AppendNode(node, pos))
			return false;
	}

	return true;
}

/////////////////////////////
// DistributedMmr
struct DistributedMmr::Impl
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
	virtual void LoadElement(Hash&, const Position&) const override;
	virtual void SaveElement(const Hash&, const Position&) override;
};

uint32_t DistributedMmr::get_NodeSize(uint64_t n)
{
	uint8_t h = Impl::get_NextPeak(n);
	uint32_t nSize = h * (sizeof(Hash) + sizeof(Key)); // 1st peak - must contain hashes and refs

	if (n)
		nSize += sizeof(Key); // ref to next peak

	return nSize;
}

void DistributedMmr::Impl::SaveElement(const Hash& hash, const Position& pos)
{
	if (pos.H) // we don't store explicitly the hash of the element itself.
	{
		m_pTrgHash[pos.H - 1] = hash;

		assert(m_pNodes[m_nDepth].m_nIdx == m_Count - (uint64_t(1) << (pos.H - 1)));
		m_pTrgKey[pos.H - 1] = m_pNodes[m_nDepth].m_Key;
	}
}

void DistributedMmr::Impl::LoadElement(Hash& hash, const Position& pos) const
{
	// index of the element that carries the information
	uint64_t nIdx = ((pos.X + 1) << pos.H) - 1;
	Key k = Cast::NotConst(this)->FindElement(nIdx);

	if (pos.H)
		hash = ((const Hash*) m_This.get_NodeData(k))[pos.H - 1];
	else
		m_This.get_NodeHash(hash, k);
}

DistributedMmr::Key DistributedMmr::Impl::FindElement(uint64_t nIdx)
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

void DistributedMmr::Append(Key k, void* pBuf, const Hash& hash)
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

void DistributedMmr::get_Hash(Hash& hv) const
{
	Impl impl(Cast::NotConst(*this));
	impl.get_Hash(hv);
}

void DistributedMmr::get_PredictedHash(Hash& hv, const Hash& hvAppend) const
{
	Impl impl(Cast::NotConst(*this));
	impl.get_PredictedHash(hv, hvAppend);
}

void DistributedMmr::get_Proof(IProofBuilder& bld, uint64_t i) const
{
	Impl impl(Cast::NotConst(*this));
	impl.get_Proof(bld, i);
}

/////////////////////////////
// CompactMmr
void CompactMmr::get_Hash(Hash& hv) const
{
	uint32_t i = (uint32_t) m_vNodes.size();
	if (i)
	{
		for (hv = m_vNodes[--i]; i; )
			Interpret(hv, m_vNodes[--i], false);
	} else
		ZeroObject(hv);
}

void CompactMmr::get_PredictedHash(Hash& hv, const Hash& hvAppend) const
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

void CompactMmr::Append(const Hash& hv)
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

/////////////////////////////
// FixedMmmr
void FixedMmmr::Reset(uint64_t nTotal)
{
	m_Total = nTotal;

	uint64_t nHashes = nTotal;
	while (nTotal >>= 1)
		nHashes += nTotal;

	m_vHashes.resize(nHashes);
}

uint64_t FixedMmmr::Pos2Idx(const Position& pos) const
{
	uint64_t nTotal = m_Total;
	uint64_t ret = pos.X;

	for (uint8_t y = 0; y < pos.H; y++)
	{
		ret += nTotal;
		nTotal >>= 1;
	}

	assert(pos.X < nTotal);
	assert(ret < m_vHashes.size());
	return ret;
}

void FixedMmmr::LoadElement(Hash& hv, const Position& pos) const
{
	hv = m_vHashes[Pos2Idx(pos)];
}

void FixedMmmr::SaveElement(const Hash& hv, const Position& pos)
{
	m_vHashes[Pos2Idx(pos)] = hv;
}

/////////////////////////////
// FlyMmr
struct FlyMmr::Inner
	:public Mmr
{
	const FlyMmr& m_This;

	Inner(const FlyMmr& x)
		:m_This(x)
	{
		m_Count = m_This.m_Count;
	}

	void Calculate(Hash& hv, const Position& pos) const
	{
		if (pos.H)
		{
			Position pos2;
			pos2.X = pos.X << 1;
			pos2.H = pos.H - 1;
			Calculate(hv, pos2);

			Hash hv2;
			pos2.X++;
			Calculate(hv2, pos2);

			Interpret(hv, hv2, true);
		}
		else
		{
			assert(pos.X < m_Count);
			m_This.LoadElement(hv, pos.X);
		}
	}

	virtual void LoadElement(Hash& hv, const Position& pos) const override
	{
		Calculate(hv, pos);
	}

	virtual void SaveElement(const Hash&, const Position&) override
	{
		assert(false); // not used
	}
};

void FlyMmr::get_Hash(Hash& hv) const
{
	Inner x(*this);
	x.get_Hash(hv);
}

bool FlyMmr::get_Proof(IProofBuilder& builder, uint64_t i) const
{
	Inner x(*this);
	return x.get_Proof(builder, i);
}


/////////////////////////////
// MultiProof
MultiProof::Builder::Builder(MultiProof& x)
	:m_This(x)
	,m_bSkipSibling(false)
{
}

bool MultiProof::Builder::AppendNode(const Node& n, const Position& pos)
{
	for (; !m_vLast.empty(); m_vLast.pop_back())
	{
		const Position& pos0 = m_vLast.back();
		if (pos0.H > pos.H)
			break;

		if ((pos0.H == pos.H) && (pos0.X == pos.X))
			return false; // the rest of the proof would be the same
	}

	if (pos.H)
		m_vLastRev.push_back(pos);

	if (pos.H || !m_bSkipSibling)
		m_This.m_vData.push_back(n.second);

	return true;
}

void MultiProof::Builder::Add(uint64_t i)
{
	get_Proof(*this, i);

	for ( ; !m_vLastRev.empty(); m_vLastRev.pop_back())
		m_vLast.push_back(m_vLastRev.back());

	m_bSkipSibling = false;
}

MultiProof::Verifier::Verifier(const MultiProof& x, uint64_t nCount)
	:m_phvSibling(NULL)
	,m_bVerify(true)
{
	m_itPos = x.m_vData.begin();
	m_itEnd = x.m_vData.end();
	m_Count = nCount;
}

bool MultiProof::Verifier::AppendNode(const Node& n, const Position& pos)
{
	for (; !m_vLast.empty(); m_vLast.pop_back())
	{
		const MyNode& mn = m_vLast.back();
		if (mn.m_Pos.H > pos.H)
			break;

		if ((mn.m_Pos.H == pos.H) && (mn.m_Pos.X == pos.X))
		{
			// the rest of the proof would be the same
			if (m_bVerify && (mn.m_hv != m_hvPos))
				m_bVerify = false;
			return false;
		}
	}

	if (pos.H)
	{
		m_vLastRev.resize(m_vLastRev.size() + 1);
		m_vLastRev.back().m_Pos = pos;
		m_vLastRev.back().m_hv = m_hvPos;
	}

	if (m_phvSibling)
	{
		assert(!pos.H);
	}
	else
	{
		if (m_itPos == m_itEnd)
		{
			m_bVerify = false;
			return false;
		}

		m_phvSibling = &(*m_itPos++);
	}

	if (m_bVerify)
		Interpret(m_hvPos, *m_phvSibling, n.first);
	m_phvSibling = NULL;


	return true;
}

void MultiProof::Verifier::Process(uint64_t i)
{
	if (i >= m_Count)
		m_bVerify = false;
	else
	{
		if (Mmr::get_Proof(*this, i))
			// probably 1st time. Verify the result
			if (m_bVerify && !IsRootValid(m_hvPos))
				m_bVerify = false;

		for (; !m_vLastRev.empty(); m_vLastRev.pop_back())
			m_vLast.push_back(m_vLastRev.back());
	}
}

} // namespace Merkle
} // namespace beam
