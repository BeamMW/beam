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

#pragma once
#include "Math.h"
#include "Sort.h"

struct Ethash
{
	typedef Opaque<128> Hash1024;
	typedef Opaque<64> Hash512;
	typedef Opaque<32> Hash256;

	static const uint32_t nSolutionElements = 64;

	struct EpochParams
	{
		uint32_t m_DatasetCount;
		HashValue m_hvRoot;
	};

	struct Item
	{
		uint32_t m_nIndex;
		const Hash1024* m_pElem;

		bool operator < (uint32_t i) const
		{
			return m_nIndex < i;
		}
	};

	//////////////////////////////////
	// ethash solution interpreter. Takes solution elements as input (from global dataset), returns final point (mix-hash) and supposed positions of the provided elements
	static void InterpretPath(uint32_t nFullDatasetCount, const Hash512& hvSeed, const Hash1024* pSol, Hash256& res, Item* pItems)
	{
        constexpr uint32_t nWords = sizeof(Hash1024) / sizeof(uint32_t);

        uint32_t nSeedInit = Utils::FromLE((const uint32_t&) hvSeed);

        Hash1024 hvMix;
        _POD_(((Hash512*) &hvMix)[0]) = hvSeed;
        _POD_(((Hash512*) &hvMix)[1]) = hvSeed;

        for (uint32_t i = 0; i < nSolutionElements; ++i)
        {
			pItems[i].m_pElem = pSol + i;
			pItems[i].m_nIndex = fnv1(i ^ nSeedInit, ((uint32_t*) &hvMix)[i % nWords]) % nFullDatasetCount;
            const Hash1024& hvElem = pSol[i];

            for (uint32_t j = 0; j < nWords; ++j)
                ((uint32_t*) &hvMix)[j] = fnv1(((uint32_t*) &hvMix)[j], ((uint32_t*) &hvElem)[j]);
        }

        for (uint32_t i = 0; i < nWords; i += 4)
        {
            uint32_t h1 = fnv1(((uint32_t*) &hvMix)[i], ((uint32_t*) &hvMix)[i + 1]);
            uint32_t h2 = fnv1(h1, ((uint32_t*) &hvMix)[i + 2]);
            uint32_t h3 = fnv1(h2, ((uint32_t*) &hvMix)[i + 3]);
            ((uint32_t*) &res)[i / 4] = h3;
        }
	}

	//////////////////////////////////
	// Multi-merkle-proof verifier. Verifies that 
	struct MultiProofVerifier
	{
		uint32_t m_Count;
		uint32_t m_nProofRemaining;

		typedef HashValue Hash;
		const Hash* m_pProof;

		void Verify(Item* pItems, const EpochParams& ep)
		{
			m_Count = ep.m_DatasetCount;
			assert(m_Count);

			// evaluate target range. Beware of overflow (for large m_Count)
			uint32_t nLast = 0;
			while (nLast < m_Count)
				nLast = (nLast << 1) | 1;

			Hash hvRoot;
			Evaluate(pItems, nSolutionElements, 0, (nLast >> 1) + 1, hvRoot);

			Env::Halt_if(_POD_(hvRoot) != ep.m_hvRoot);
		}


		bool Evaluate(Item* pItems, uint32_t nItems, uint32_t n, uint32_t nHalf, Hash& hv)
		{
			if (!nItems)
			{
				if (n >= m_Count)
					return false; // out

				Env::Halt_if(!m_nProofRemaining);

				_POD_(hv) = *m_pProof++;
				m_nProofRemaining--;
			}
			else
			{
				// can't be out, contains elements
				if (!nHalf)
				{
					const Hash1024& hvItem = *pItems->m_pElem;

					{
						HashProcessor::Sha256 hp;
						hp.Write(&hvItem, sizeof(hvItem));
						hp >> hv;
					}

					for (uint32_t i = 1; i < nItems; i++)
						Env::Halt_if(_POD_(hvItem) != *pItems[i].m_pElem); // duplicated indexes in solution (unlikely but possible), but provided elements are different
				}
				else
				{

					uint32_t nMid = n + nHalf;
					nHalf >>= 1;

					auto n0 = PivotSplit(pItems, nItems, nMid);

					Evaluate(pItems, n0, n, nHalf, hv);

					Hash hv2;
					if (Evaluate(pItems + n0, nItems - n0, nMid, nHalf, hv2))
						Merkle::Interpret(hv, hv, hv2);
				}
			}

			return true;
		}
	};


	// all-in-one verification
	static uint32_t VerifyHdr(const EpochParams& ep, const HashValue& hvHeaderHash, uint64_t nonce, uint64_t difficulty, const void* pProof, uint32_t nSizeProof)
	{
		// 1. derive pow seed
		Hash512 hvSeed;
		{
			HashProcessor::Base hp;
			hp.m_p = Env::HashCreateKeccak(512);
			hp << hvHeaderHash;

			nonce = Utils::FromLE(nonce);
			hp.Write(&nonce, sizeof(nonce));

			hp >> hvSeed;
		}

		// 2. Use provided solution items, simulate pow path
		constexpr uint32_t nFixSizePart = sizeof(Hash1024) * nSolutionElements;
		Env::Halt_if(nFixSizePart > nSizeProof);

		Hash256 hvMix;
		Item pIndices[nSolutionElements];
		InterpretPath(ep.m_DatasetCount, hvSeed, (const Hash1024*) pProof, hvMix, pIndices);

		// 3. Interpret merkle multi-proof, verify the epoch root commits to the specified solution elements.
		MultiProofVerifier mpv;
		mpv.m_pProof = (const MultiProofVerifier::Hash*) (((const Hash1024*) pProof) + nSolutionElements);

		uint32_t nMaxProofNodes = (nSizeProof - nFixSizePart) / sizeof(MultiProofVerifier::Hash);
		mpv.m_nProofRemaining = nMaxProofNodes;

		mpv.Verify(pIndices, ep);

		// 4. 'final' hash
		{
			HashProcessor::Base hp;
			hp.m_p = Env::HashCreateKeccak(256);
			hp
				<< hvSeed
				<< hvMix
				>> hvMix;
		}

		// 5. Test the difficulty
		MultiPrecision::UInt<sizeof(hvMix) / sizeof(MultiPrecision::Word)> val1; // 32 bytes, 8 words
		val1.FromBE_T(hvMix);

		MultiPrecision::UInt<sizeof(difficulty) / sizeof(MultiPrecision::Word)> val2; // 8 bytes, 2 words
		val2 = difficulty;

		auto val3 = val1 * val2; // 40 bytes, 10 words

		// check that 2 most significant words are 0
		Env::Halt_if(val3.get_Val<val3.nWords>() || val3.get_Val<val3.nWords - 1>());

		// all ok. Return the actually consumed size
		return nFixSizePart + (nMaxProofNodes - mpv.m_nProofRemaining) * sizeof(MultiProofVerifier::Hash);
	}


private:

    static uint32_t fnv1(uint32_t u, uint32_t v)
    {
        constexpr uint32_t fnv_prime = 0x01000193;
        return (u * fnv_prime) ^ v;
    }

};