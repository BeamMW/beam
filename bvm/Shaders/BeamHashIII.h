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
#include "Sort.h"

struct BeamHashIII
{
	struct IndexDecoder
	{
		typedef uint32_t Word;
		static const uint32_t s_WordBits = sizeof(Word) * 8;

		template <uint32_t nBitsPerIndex, uint32_t nSrcIdx, uint32_t nSrcTotal>
		struct State
		{
			static_assert(nBitsPerIndex <= s_WordBits, "");
			static_assert(nBitsPerIndex >= s_WordBits/2, "unpack should affect no more than 3 adjacent indices");

			static const Word s_Msk = (((Word) 1) << nBitsPerIndex) - 1;

			static const uint32_t s_BitsDecoded = nSrcIdx * s_WordBits;

			static const uint32_t s_iDst = s_BitsDecoded / nBitsPerIndex;
			static const uint32_t s_nDstBitsDone = s_BitsDecoded % nBitsPerIndex;
			static const uint32_t s_nDstBitsRemaining = nBitsPerIndex - s_nDstBitsDone;

			static void Do(Word* pDst, const Word* pSrc)
			{
				Word src = Utils::FromLE(pSrc[nSrcIdx]);

				if constexpr (s_nDstBitsDone > 0)
					pDst[s_iDst] |= (src << s_nDstBitsDone) & s_Msk;
				else
					pDst[s_iDst] = src & s_Msk;

				pDst[s_iDst + 1] = (src >> s_nDstBitsRemaining) & s_Msk;

				if constexpr (s_nDstBitsRemaining + nBitsPerIndex < s_WordBits)
					pDst[s_iDst + 2] = (src >> (s_nDstBitsRemaining + nBitsPerIndex)) & s_Msk;

				if constexpr (nSrcIdx + 1 < nSrcTotal)
					State<nBitsPerIndex, nSrcIdx + 1, nSrcTotal>::Do(pDst, pSrc);
			}
		};
	};


	struct sipHash {

		static uint64_t rotl(uint64_t x, uint64_t b)
		{
			return (x << b) | (x >> (64 - b));
		}

		uint64_t m_pState[4];

		struct Wrk
		{
			uint64_t v0, v1, v2, v3;

			void Round()
			{
				v0 += v1; v2 += v3;
				v1 = rotl(v1, 13);
				v3 = rotl(v3, 16);
				v1 ^= v0; v3 ^= v2;
				v0 = rotl(v0, 32);
				v2 += v1; v0 += v3;
				v1 = rotl(v1, 17);
				v3 = rotl(v3, 21);
				v1 ^= v2; v3 ^= v0;
				v2 = rotl(v2, 32);
			}
		};

		uint64_t siphash24(uint64_t nonce) const
		{
			Wrk wrk;
			wrk.v0 = m_pState[0];
			wrk.v1 = m_pState[1];
			wrk.v2 = m_pState[2];
			wrk.v3 = m_pState[3];
			wrk.v3 ^= nonce;
			wrk.Round();
			wrk.Round();
			wrk.v0 ^= nonce;
			wrk.v2 ^= 0xff;
			wrk.Round();
			wrk.Round();
			wrk.Round();
			wrk.Round();

			return (wrk.v0 ^ wrk.v1 ^ wrk.v2 ^ wrk.v3);
		}

	};

	static const uint32_t s_workBitSize = 448;
	static const uint32_t s_collisionBitSize = 24;
	static const uint32_t s_numRounds = 5;

	struct StepElemLite
	{
		static_assert(!(s_workBitSize % 8), "");
		static const uint32_t s_workBytes = s_workBitSize / 8;

		typedef uint64_t WorkWord;
		static_assert(!(s_workBytes % sizeof(WorkWord)), "");
		static const uint32_t s_workWords = s_workBytes / sizeof(WorkWord);

		WorkWord m_pWorkWords[s_workWords];

		void Init(const sipHash& s, uint32_t index)
		{
			for (int32_t i = _countof(m_pWorkWords); i--; )
				m_pWorkWords[i] = s.siphash24((index << 3) + i);
		}

		void MergeWith(const StepElemLite& x, uint32_t remLen)
		{
			// Create a new rounds step element from matching two ancestors
			assert(!(remLen % 8));
			const uint32_t remBytes = remLen / 8;

			for (int32_t i = _countof(m_pWorkWords); i--; )
				m_pWorkWords[i] ^= x.m_pWorkWords[i];

			static_assert(!(s_collisionBitSize % 8), "");
			const uint32_t collisionBytes = s_collisionBitSize / 8;

			assert(s_workBytes - remBytes >= collisionBytes);

			Env::Memcpy(m_pWorkWords, ((uint8_t*)m_pWorkWords) + collisionBytes, remBytes); // it's actually memmove
			Env::Memset(((uint8_t*)m_pWorkWords) + remBytes, 0, s_workBytes - remBytes);
		}

		void applyMix(uint32_t remLen, const uint32_t* pIdx, uint32_t nIdx, bool bVerbose)
		{
			WorkWord pTemp[9];
			static_assert(sizeof(pTemp) > sizeof(m_pWorkWords), "");
			Env::Memcpy(pTemp, m_pWorkWords, sizeof(m_pWorkWords));
			Env::Memset(pTemp + _countof(m_pWorkWords), 0, sizeof(pTemp) - sizeof(m_pWorkWords));

			// Add in the bits of the index tree to the end of work bits
			uint32_t padNum = ((512 - remLen) + s_collisionBitSize) / (s_collisionBitSize + 1);
			if (padNum > nIdx)
				padNum = nIdx;

			for (uint32_t i = 0; i < padNum; i++)
			{
				uint32_t nShift = remLen + i * (s_collisionBitSize + 1);
				uint32_t n0 = nShift / (sizeof(WorkWord) * 8);
				nShift %= (sizeof(WorkWord) * 8);

				auto idx = pIdx[i];

				pTemp[n0] |= ((WorkWord)idx) << nShift;

				if (nShift + s_collisionBitSize + 1 > (sizeof(WorkWord) * 8))
					pTemp[n0 + 1] |= idx >> (sizeof(WorkWord) * 8 - nShift);
			}


			// Applyin the mix from the lined up bits
			uint64_t result = 0;
			for (uint32_t i = 0; i < 8; i++)
			{
				result += sipHash::rotl(pTemp[i], (29 * (i + 1)) & 0x3F);

#ifdef HOST_BUILD
				if (bVerbose)
					std::cout << "\t\tword = " << pTemp[i] << ", res = " << result << std::endl;
#endif // HOST_BUILD
			}

			result = sipHash::rotl(result, 24);

#ifdef HOST_BUILD
			if (bVerbose)
				std::cout << "\tres = " << result << std::endl;
#endif // HOST_BUILD

			// Wipe out lowest 64 bits in favor of the mixed bits
			m_pWorkWords[0] = result;

#ifdef HOST_BUILD
			if (bVerbose)
			{
				std::cout << "\tpElem = ";
				beam::uintBigImpl::_PrintFull((const uint8_t*)m_pWorkWords, sizeof(m_pWorkWords), std::cout);
				std::cout << std::endl;
			}
#endif // HOST_BUILD
		}

		bool hasCollision(const StepElemLite& x) const
		{
			auto val = m_pWorkWords[0] ^ x.m_pWorkWords[0];
			const uint32_t msk = (1U << s_collisionBitSize) - 1;
			return !(val & msk);
		}
	};


	static bool Verify(const void* pInp, uint32_t nInp, const void* pNonce, uint32_t nNonce, const uint8_t* pSol, uint32_t nSol)
	{
#ifdef HOST_BUILD
		std::cout << "BeamHashIII input: pInp = ";
		beam::uintBigImpl::_PrintFull((const uint8_t*)pInp, nInp, std::cout);
		std::cout << ", pNonce = ";
		beam::uintBigImpl::_PrintFull((const uint8_t*) pNonce, nNonce, std::cout);
		std::cout << ", pSol = ";
		beam::uintBigImpl::_PrintFull((const uint8_t*) pSol, nSol, std::cout);
		std::cout << std::endl;

#endif // HOST_BUILD

		if (104 != nSol)
			return false;

		sipHash prePoW;

		{
#pragma pack (push, 1)
			struct Personal {
				char m_sz[8];
				uint32_t m_WorkBits;
				uint32_t m_Rounds;
			} pers;
#pragma pack (pop)

			Env::Memcpy(pers.m_sz, "Beam-PoW", sizeof(pers.m_sz));
			pers.m_WorkBits = s_workBitSize;
			pers.m_Rounds = s_numRounds;

			HashProcessor::Blake2b hp(&pers, sizeof(pers), sizeof(prePoW));

			hp.Write(pInp, nInp);
			hp.Write(pNonce, nNonce);
			hp.Write(pSol + 100, 4); // last 4 bytes are the extra nonce
			hp >> prePoW;

#ifdef HOST_BUILD
			std::cout << "\tprePow =";
			beam::uintBigImpl::_PrintFull((const uint8_t*) &prePoW, sizeof(prePoW), std::cout);
			std::cout << std::endl;
#endif // HOST_BUILD

		}

		uint32_t pIndices[32];
		IndexDecoder::State<25, 0, 25>::Do(pIndices, (const uint32_t*)pSol);

#ifdef HOST_BUILD
		for (uint32_t i = 0; i < _countof(pIndices); i++)
			std::cout << "\tIndex-" << i << " = " << pIndices[i] << std::endl;
#endif // HOST_BUILD

		StepElemLite pElemLite[_countof(pIndices)];
		for (uint32_t i = 0; i < _countof(pIndices); i++)
			pElemLite[i].Init(prePoW, pIndices[i]);

		uint32_t round = 1;
		for (uint32_t nStep = 1; nStep < _countof(pIndices); nStep <<= 1)
		{
#ifdef HOST_BUILD
			std::cout << "\tRound =" << round << std::endl;
#endif // HOST_BUILD

			bool bVerbose = (2 == round);

			for (uint32_t i0 = 0; i0 < _countof(pIndices); )
			{
				uint32_t remLen = s_workBitSize - (round - 1) * s_collisionBitSize;
				if (round == 5) remLen -= 64;

				pElemLite[i0].applyMix(remLen, pIndices + i0, nStep, bVerbose);

				uint32_t i1 = i0 + nStep;
				pElemLite[i1].applyMix(remLen, pIndices + i1, nStep, bVerbose);

#ifdef HOST_BUILD
				std::cout << "\tElem-" << i0 << " = ";
				beam::uintBigImpl::_PrintFull((const uint8_t*) &pElemLite[i0], sizeof(pElemLite[i0]), std::cout);
				std::cout << std::endl;
				std::cout << "\tElem-" << i1 << " = ";
				beam::uintBigImpl::_PrintFull((const uint8_t*)&pElemLite[i1], sizeof(pElemLite[i1]), std::cout);
				std::cout << std::endl;
#endif // HOST_BUILD


				if (!pElemLite[i0].hasCollision(pElemLite[i1]))
				{
#ifdef HOST_BUILD
					std::cout << "\tNo collisions!" << round << std::endl;
#endif // HOST_BUILD
					return false;
				}

				if (pIndices[i0] >= pIndices[i1])
				{
#ifdef HOST_BUILD
					std::cout << "\tIndices OOO!" << round << std::endl;
#endif // HOST_BUILD
					return false;
				}

				remLen = s_workBitSize - round * s_collisionBitSize;
				if (round == 4) remLen -= 64;
				if (round == 5) remLen = s_collisionBitSize;

				pElemLite[i0].MergeWith(pElemLite[i1], remLen);

				i0 = i1 + nStep;
			}

			round++;
		}

		if (!Env::Memis0(pElemLite[0].m_pWorkWords, sizeof(pElemLite[0].m_pWorkWords)))
		{
#ifdef HOST_BUILD
			std::cout << "\tMemis0 failed" << std::endl;
#endif // HOST_BUILD
			return false;
		}

		// ensure all the indices are distinct
		static_assert(sizeof(pElemLite) >= sizeof(pIndices), "");
		auto* pSorted = MergeSort<uint32_t>::Do(pIndices, (uint32_t*)pElemLite, _countof(pIndices));

		for (uint32_t i = 0; i + 1 < _countof(pIndices); i++)
			if (pSorted[i] >= pSorted[i + 1])
			{
#ifdef HOST_BUILD
				std::cout << "\tIndex order failed" << std::endl;
#endif // HOST_BUILD
				return false;
			}

#ifdef HOST_BUILD
		std::cout << "\tValidated ok" << std::endl;
#endif // HOST_BUILD
		return true;
	}

};
