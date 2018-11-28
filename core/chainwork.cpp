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

#include "block_crypt.h"

namespace beam
{
	//////////////////////////
	// ChainWorkProof
	//
	// Based on FlyClient idea by Loi Luu, Benedikt Bünz, Mahdi Zamani
	//
	// Every state header includes (implicitly) the merkle tree hash of all the inherited states, whereas the difficulty and the cumulative chainwork of each header is accounted for in its hash.
	// So if we consider the "work axis", we have a Merkle tree of committed ranges of proven work, which are supposed to be contiguous and non-overlapping, up to the blockchain tip.
	// If the Verifier chooses a random point on this axis, the Prover is supposed to present both the range that covers this point with a work proof, as well as the Merkle proof for this range.
	//
	// We assume that the attacker has less than 2/3 power of the honest community (40% of overall power).
	// The goal of the Verifier it to verify that at least 2/3 of the entire chainwork is covered by the proven work ranges.
	// Moreover, since the attacker may take an existing blockchain and forge only some suffix, the Verifier needs to check that at least 2/3 of the chainwork is covered within *any* suffix.
	//
	// To keep the proof compact, the Verifier performs a random sampling of points on the work axis, and verifies the appropriate proofs. So that if there are n points sampled within a specific range,
	// and the attacker indeed has covered less than 2/3 of the range, the probability to bypass the protocol would be less than (2/3)^n.
	// The goal of the prover is to reach the specific probability threshold for any given sufix.
	//
	// Our current probability threshold: ~10^-18 (1 to quintillion). Or roughly 2^-60.
	// Seems quite pessimistic, but not to forget we're talking about random oracle model, not the real interaction. The attacker may make subtle changes to the originally presented blockchain tip,
	// and each time "remine" that top header, to generate different transcript for the protocol. So that the effective probability threshold of this protocol is (roughly) divided by the number of different
	// transcripts which is feasible for the attacker to generate. Assuming the attacker may generate 10^9 transcripts, still the probability to bypass it is order of 10^-9.
	//
	// Assuming 2/3 power of an attacker, and the needed threshold of 2^-60, the number of minimum sampled points in any sufix should be:
	// N = 60 * log(2) / [ log(3) - log(2) ] = 60 * 1.71 = 103
	//
	//
	// Our sampling strategy is according to the following logic:
	// 1. Sample a point within the first 1/N of the asserted chainwork range.
	// 2. Verify the proof for this point.
	// 3. Cut-off the range from the beginning to the current point, and continue to (1)
	//
	//		*CORRECTION*
	//		 In the current implementation we actually sample in *reverse* order. I.e. each time we pick a range below the current suffix, 1/N of its length, and sample a point there. We advance downward until we reach (or cross) zero point.
	//		The reason for this change is that we want to be able to *CROP* the Proof easily (without fully rebuilding it). So that we can prepare the long proof once, and then send each client a truncated proof, according to its state.
	//
	// Note that the sampled point falls into a range, that covers more than just a single point. And, naturally, the cut-off includes the whole range. So that the above scheme should typically
	// converge more rapidly than it may seem, especially toward the end, where difficulties are expected (though not required) to be bigger. Closer to the end, where less than N blocks are remaining to the tip,
	// it should be like just sequentailly iterating the blocks one after another.
	//
	// Additional notes:
	//	- We use "hard" proofs. Unlike regular Merkle proofs, the Verifier is given only the list of hashes, whereas the direction of hashing is deduced from the appropriate height.
	// In other words the Verifier knows the supposed structure of the tree and of the proofs.
	// Such proofs are more robust, they won't allow the attacker to include "different versions" of the block for the same height.
	//	- If there are several consecutively sampled blocks - we include the Merkle proof only for the highest one, since it has a direct reference to those in the range (i.e. it's a Merkle list already).
	// This should make the proof dramatically smaller, given toward the blockchain end all the blocks are expected to be included one after another.
	//	- We verify proofs, order of block heights (i.e. heavier block must have bigger height), and that they don't overlap. But no verification of difficulty adjustment wrt rules.

	
	struct Block::ChainWorkProof::Sampler
	{
		ECC::Oracle m_Oracle;

		Difficulty::Raw m_Begin;
		Difficulty::Raw m_End;
		const Difficulty::Raw& m_LowerBound;

		Sampler(const SystemState::Full& sTip, const Difficulty::Raw& lowerBound)
			:m_LowerBound(lowerBound)
		{
			ECC::Hash::Value hv;
			sTip.get_Hash(hv);
			m_Oracle << hv;

			m_End = sTip.m_ChainWork;
			m_Begin = sTip.m_ChainWork - sTip.m_PoW.m_Difficulty;
		}

		static void TakeFraction(Difficulty::Raw& v)
		{
			// The fraction is 1/103. Which is roughly 635 / 65536
			auto val = v * uintBigFrom((uint16_t) 635);
			memcpy(v.m_pData, val.m_pData, v.nBytes); // i.e. get the upper part of the result
		}

		bool UniformRandom(Difficulty::Raw& out, const Difficulty::Raw& threshold)
		{
			Difficulty::Raw::Threshold thrSel(threshold);
			if (!thrSel)
				return false;

			// sample random, truncate to the appropriate bits length, and use accept/reject criteria
			do
				m_Oracle >> out;
			while (!thrSel.Accept(out));

			return true;
		}

		bool SamplePoint(Difficulty::Raw& out)
		{
			// range = m_End - m_Begin
			Difficulty::Raw range = m_Begin;
			range.Negate();
			range += m_End;

			TakeFraction(range);

			if (range == Zero)
				range = 1U;

			bool bAllCovered = (range >= m_Begin);

			verify(UniformRandom(out, range));

			range.Negate(); // convert to -range

			out += m_Begin;
			out += range; // may overflow, but it's ok

			if ((out < m_LowerBound) || (out >= m_Begin))
				return false;

			if (bAllCovered)
				m_Begin = Zero;
			else
				m_Begin += range;

			return true;
		}
	};

	void Block::ChainWorkProof::Create(ISource& src, const SystemState::Full& sRoot)
	{
		Sampler samp(sRoot, m_LowerBound);

		struct MyBuilder
			:public Merkle::MultiProof::Builder
		{
			ISource& m_Src;
			MyBuilder(ChainWorkProof& x, ISource& src)
				:Merkle::MultiProof::Builder(x.m_Proof)
				,m_Src(src)
			{
			}

			virtual void get_Proof(Merkle::IProofBuilder& bld, uint64_t i) override
			{
				m_Src.get_Proof(bld, Rules::HeightGenesis + i);
			}

		} bld(*this, src);

		for (SystemState::Full s = sRoot; ; )
		{
			if (m_vArbitraryStates.empty())
			{
				m_Heading.m_Prefix = s;
				m_Heading.m_vElements.push_back(s);
			}

			Difficulty::Raw d;
			if (!samp.SamplePoint(d))
				break;

			Height hPrev = s.m_Height;
			src.get_StateAt(s, d);

			assert(s.m_Height < hPrev);
			bool bJump = (s.m_Height + 1 != hPrev);
			if (bJump)
				bld.Add(s.m_Height - Rules::HeightGenesis);

			if (bJump || !m_vArbitraryStates.empty())
				m_vArbitraryStates.push_back(s);

			d = s.m_ChainWork - s.m_PoW.m_Difficulty;

			if (samp.m_Begin > d)
				samp.m_Begin = d;
		}
	}

	bool Block::ChainWorkProof::IsValid(Block::SystemState::Full* pTip /* = NULL */) const
	{
		size_t iState, iHash;
		return
			IsValidInternal(iState, iHash, m_LowerBound, pTip) &&
			(m_vArbitraryStates.size() + m_Heading.m_vElements.size() == iState) &&
			(m_Proof.m_vData.size() == iHash);
	}

	template <typename T> void CopyCroppedVector(std::vector<T>& dst, const std::vector<T>& src)
	{
		std::copy(src.cbegin(), src.cbegin() + dst.size(), dst.begin());
	}

	bool Block::ChainWorkProof::Crop(const ChainWorkProof& src)
	{
		size_t iState, iHash;
		if (!src.IsValidInternal(iState, iHash, m_LowerBound, NULL))
			return false;

		bool bInPlace = (&src == this);

		if (iState >= src.m_Heading.m_vElements.size())
		{
			m_vArbitraryStates.resize(iState - src.m_Heading.m_vElements.size());

			if (!bInPlace)
			{
				m_Heading.m_Prefix = src.m_Heading.m_Prefix;
				m_Heading.m_vElements.resize(src.m_Heading.m_vElements.size());
			}
		}
		else
		{
			m_vArbitraryStates.clear();
			assert(iState); // root must remain!

			SystemState::Full s;
			Cast::Down<SystemState::Sequence::Prefix>(s) = src.m_Heading.m_Prefix;
			Cast::Down<SystemState::Sequence::Element>(s) = src.m_Heading.m_vElements.back();

			for (size_t i = src.m_Heading.m_vElements.size() - 1; i >= iState; )
			{
				s.NextPrefix();
				Cast::Down<SystemState::Sequence::Element>(s) = src.m_Heading.m_vElements[--i];
				s.m_ChainWork += s.m_PoW.m_Difficulty;
			}

			m_Heading.m_Prefix = s;
			m_Heading.m_vElements.resize(iState);
		}

		m_Proof.m_vData.resize(iHash);

		if (!bInPlace)
		{
			CopyCroppedVector(m_vArbitraryStates, src.m_vArbitraryStates);
			CopyCroppedVector(m_Heading.m_vElements, src.m_Heading.m_vElements);
			CopyCroppedVector(m_Proof.m_vData, src.m_Proof.m_vData);
			m_hvRootLive = src.m_hvRootLive;
		}

		return true;
	}

	bool Block::ChainWorkProof::Crop()
	{
		return Crop(*this);
	}

	bool Block::ChainWorkProof::IsValidInternal(size_t& iState, size_t& iHash, const Difficulty::Raw& lowerBound, Block::SystemState::Full* pTip) const
	{
		if (m_Heading.m_vElements.empty())
			return false;

		SystemState::Full s;
		Cast::Down<SystemState::Sequence::Prefix>(s) = m_Heading.m_Prefix;
		Cast::Down<SystemState::Sequence::Element>(s) = m_Heading.m_vElements.back();

		for (size_t i = m_Heading.m_vElements.size() - 1; ; )
		{
			if (!(s.IsValid()))
				return false;

			if (!i--)
				break;

			s.NextPrefix();
			Cast::Down<SystemState::Sequence::Element>(s) = m_Heading.m_vElements[i];
			s.m_ChainWork += s.m_PoW.m_Difficulty;
		}

		for (size_t i = 0; i < m_vArbitraryStates.size(); i++)
		{
			const Block::SystemState::Full& s2 = m_vArbitraryStates[i];
			if (!(s2.IsValid()))
				return false;
		}

		struct MyVerifier :public Merkle::MultiProof::Verifier
		{
			const ChainWorkProof& m_This;
			Merkle::Hash m_hvRootDefinition;
			MyVerifier(const ChainWorkProof& x, uint64_t nCount)
				:Verifier(x.m_Proof, nCount)
				,m_This(x)
			{}

			virtual bool IsRootValid(const Merkle::Hash& hv)
			{
				Merkle::Hash hvDef;
				Merkle::Interpret(hvDef, hv, m_This.m_hvRootLive);
				return hvDef == m_hvRootDefinition;
			}
		};

		MyVerifier ver(*this, s.m_Height - Rules::HeightGenesis);
		ver.m_hvRootDefinition = s.m_Definition;

		Sampler samp(s, lowerBound);
		if (samp.m_Begin >= samp.m_End) // overflow attack?
			return false;

		if (pTip)
			*pTip = s;

		Difficulty::Raw dLoPrev = s.m_ChainWork - s.m_PoW.m_Difficulty;

		for (iState = 1; ; iState++)
		{
			Difficulty::Raw dSamp;
			if (!samp.SamplePoint(dSamp))
				break;

			SystemState::Full s0 = s;

			bool bContiguousRange = (iState < m_Heading.m_vElements.size());

			if (bContiguousRange)
			{
				// still within contiguous range. Update only some params
				s.m_Height--;
				s.m_ChainWork = dLoPrev;
				s.m_PoW.m_Difficulty = m_Heading.m_vElements[iState].m_PoW.m_Difficulty;
			}
			else
			{
				if (m_vArbitraryStates.size() <= iState - m_Heading.m_vElements.size())
					return false;

				s = m_vArbitraryStates[iState - m_Heading.m_vElements.size()];
			}

			if (dSamp >= s.m_ChainWork)
				return false;

			Difficulty::Raw dLo = s.m_ChainWork - s.m_PoW.m_Difficulty;

			if (dSamp < dLo)
				return false;

			if (!bContiguousRange)
			{
				s.get_Hash(ver.m_hvPos);

				if (s.m_Height + 1 == s0.m_Height)
				{
					if (s0.m_Prev != ver.m_hvPos)
						return false;

					if (s.m_ChainWork != dLoPrev)
						return false;
				}
				else
				{
					if (s.m_Height >= s0.m_Height)
						return false;

					if (s.m_ChainWork >= dLoPrev)
						return false;

					ver.Process(s.m_Height - Rules::HeightGenesis);
					if (!ver.m_bVerify)
						return false;
				}
			}

			dLoPrev = dLo;

			if (samp.m_Begin > dLo)
				samp.m_Begin = dLo;
		}

		iHash = ver.get_Pos() - m_Proof.m_vData.begin();
		return true;
	}

	void Block::ChainWorkProof::ZeroInit()
	{
		ZeroObject(m_Heading.m_Prefix);
		m_hvRootLive = Zero;
		m_LowerBound = Zero;
	}

	void Block::ChainWorkProof::Reset()
	{
		ZeroInit();
		m_Heading.m_vElements.clear();
		m_vArbitraryStates.clear();
		m_Proof.m_vData.clear();
	}

} // namespace beam
