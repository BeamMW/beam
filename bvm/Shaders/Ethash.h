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
#include "MultiProof.h"

struct Ethash
{
	typedef Opaque<128> Hash1024;
	typedef Opaque<64> Hash512;
	typedef Opaque<32> Hash256;

	static const uint32_t nSolutionElements = 64;

	struct ProofBase
	{
		typedef Opaque<20> THash; // truncated to 160 bits, to reduce the proof size. Still decent secury.
		typedef uint32_t TCount;
		typedef const Hash1024* TElement;

		static const uint32_t nEpochsTotal = 1024;
	};

	struct MyMultiProof
		:public ProofBase
	{
		inline static void Evaluate(THash& hv, const Hash1024* pElem)
		{
			HashProcessor::Sha256 hp;
			hp << (*pElem) >> hv;
		}

		inline static void EvaluateEpoch(THash& hv, const THash& hvEpochRoot, uint32_t nEpochElements)
		{
			HashProcessor::Sha256 hp;
			hp
				<< hvEpochRoot
				<< nEpochElements
				>> hv;
		}

		inline static void TestEqual(const Hash1024* p0, const Hash1024* p1)
		{
			Env::Halt_if(_POD_(*p0) != *p1);
		}

		inline static void InterpretHash(THash& hv, const THash& hv2)
		{
			HashProcessor::Sha256 hp;
			hp << hv << hv2 >> hv;
		}

		uint32_t m_nProofRemaining;
		const THash* m_pProof;

		inline const THash& get_NextProofHash()
		{
			Env::Halt_if(!m_nProofRemaining);

			m_nProofRemaining--;
			return *m_pProof++;
		}

		inline void get_NextProofHash(THash& hv)
		{
			_POD_(hv) = get_NextProofHash();
		}
	};

	typedef MultiProof::Verifier<MyMultiProof> MyVerifier;


	//////////////////////////////////
	// ethash solution interpreter. Takes solution elements as input (from global dataset), returns final point (mix-hash) and supposed positions of the provided elements
	static void InterpretPath(uint32_t nFullDatasetCount, const Hash512& hvSeed, const Hash1024* pSol, Hash256& res, MyVerifier::Item* pItems)
	{
        constexpr uint32_t nWords = sizeof(Hash1024) / sizeof(uint32_t);

        uint32_t nSeedInit = Utils::FromLE((const uint32_t&) hvSeed);

        Hash1024 hvMix;
        _POD_(((Hash512*) &hvMix)[0]) = hvSeed;
        _POD_(((Hash512*) &hvMix)[1]) = hvSeed;

        for (uint32_t i = 0; i < nSolutionElements; ++i)
        {
			pItems[i].m_Element = pSol + i;
			pItems[i].m_Index = fnv1(i ^ nSeedInit, ((uint32_t*) &hvMix)[i % nWords]) % nFullDatasetCount;
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

	// all-in-one verification
	static uint32_t VerifyHdr(uint32_t iEpoch, uint32_t nDatasetCount, const Hash512& hvSeed, uint64_t nonce, uint64_t difficulty, const void* pProof, uint32_t nSizeProof)
	{
		// 2. Use provided solution items, simulate pow path
		constexpr uint32_t nFixSizePart = sizeof(Hash1024) * nSolutionElements;
		Env::Halt_if(nFixSizePart > nSizeProof);

		Hash256 hvMix;
		MyVerifier::Item pIndices[nSolutionElements];
		InterpretPath(nDatasetCount, hvSeed, (const Hash1024*) pProof, hvMix, pIndices);

		// 3. Interpret merkle multi-proof, evaluate the epoch root
		MyVerifier mpv;
		mpv.m_pProof = (const MyVerifier::THash*) (((const Hash1024*) pProof) + nSolutionElements);

		uint32_t nMaxProofNodes = (nSizeProof - nFixSizePart) / sizeof(MyVerifier::THash);
		mpv.m_nProofRemaining = nMaxProofNodes;

		MyVerifier::THash hvRoot;
		mpv.EvaluateRoot(hvRoot, pIndices, nSolutionElements, nDatasetCount);

		// Promote the epoch root to super root, and verify it.
		mpv.EvaluateEpoch(hvRoot, hvRoot, nDatasetCount);

		for (uint32_t h = 0; (1U << h) < MyVerifier::nEpochsTotal; h++, iEpoch >>= 1)
		{
			const auto& hv = mpv.get_NextProofHash();

			HashProcessor::Sha256 hp;

			if (1 & iEpoch)
				hp << hv << hvRoot;
			else
				hp << hvRoot << hv;
			
			hp >> hvRoot;
		}

		static const MyVerifier::THash s_SuperRoot = { 0x1C,0x36,0x97,0x82,0xF3,0xC5,0xA8,0x96,0x9E,0xD9,0x45,0x52,0xD6,0x3D,0x2C,0xF0,0x68,0x9E,0xF6,0x4A };
		Env::Halt_if(_POD_(hvRoot) != s_SuperRoot);

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
		return nFixSizePart + (nMaxProofNodes - mpv.m_nProofRemaining) * sizeof(MyVerifier::THash);
	}


private:

    static uint32_t fnv1(uint32_t u, uint32_t v)
    {
        constexpr uint32_t fnv_prime = 0x01000193;
        return (u * fnv_prime) ^ v;
    }

};