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

#define USE_BASIC_CONFIG

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#else
#	pragma warning (push, 0) // suppress warnings from secp256k1
#	pragma warning (disable: 4706) // assignment within conditional expression
#endif

#include "secp256k1-zkp/src/basic-config.h"
#include "secp256k1-zkp/include/secp256k1.h"
#include "secp256k1-zkp/src/scalar.h"
#include "secp256k1-zkp/src/group.h"
#include "secp256k1-zkp/src/hash.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#	pragma GCC diagnostic pop
#else
#	pragma warning (default: 4706)
#	pragma warning (pop)
#endif

namespace ECC_Min
{
	inline const uint32_t nBits = 256;

	struct MultiMac
	{
		typedef uint8_t Index;

		struct WnafBase
		{
			// window NAF (wNAF) representation

			struct Entry
			{
				uint16_t m_Odd;
				uint16_t m_iBit;

				static const uint16_t s_Negative = uint16_t(1) << 15;
			};

			struct Link
			{
				Index m_iElement;
				uint8_t m_iEntry; // 1-based

			} m_Next;

			struct Shared
			{
				Link m_pTable[ECC_Min::nBits + 1];

				void Reset();

				unsigned int Add(Entry* pTrg, const secp256k1_scalar& k, unsigned int nWndBits, WnafBase&, Index iElement);

				unsigned int Fetch(unsigned int iBit, WnafBase&, const Entry*, bool& bNeg);
			};

		protected:


			struct Context;
		};

		template <unsigned int nWndBits>
		struct Wnaf_T
			:public WnafBase
		{
			static const unsigned int nMaxEntries = ECC_Min::nBits / (nWndBits + 1) + 1;
			static_assert(nMaxEntries <= uint8_t(-1));

			Entry m_pVals[nMaxEntries];

			unsigned int Init(Shared& s, const secp256k1_scalar& k, Index iElement)
			{
				return s.Add(m_pVals, k, nWndBits, *this, iElement);
			}

			unsigned int Fetch(Shared& s, unsigned int iBit, bool& bNeg)
			{
				return s.Fetch(iBit, *this, m_pVals, bNeg);
			}
		};

		struct Casual
		{
			// In fast mode: x1 is assigned from the beginning, then on-demand calculated x2 and then only odd multiples.
			static const int nBits = 3;
			static const int nMaxOdd = (1 << nBits) - 1; // 15
			static const int nCount = (nMaxOdd >> 1) + 1;

			secp256k1_gej m_pPt[nCount];
			secp256k1_fe m_pFe[nCount];
			unsigned int m_nNeeded;

			typedef Wnaf_T<nBits> Wnaf;
			Wnaf m_Wnaf;
		};

		Index m_Casual;

		WnafBase::Shared m_ws;

		MultiMac() { Reset(); }

		void Reset();
		void Add(Casual&, const secp256k1_gej&, const secp256k1_scalar&);
		void Calculate(secp256k1_gej&, Casual*);


	private:

		struct Normalizer;
	};

	template <unsigned int nMaxCasual>
	struct MultiMac_WithBufs
		:public MultiMac
	{
		struct Bufs {
			Casual m_pCasual[nMaxCasual];
		} m_Bufs;

		void Add(const secp256k1_gej& gej, const secp256k1_scalar& k)
		{
			static_assert(nMaxCasual <= Index(-1));
			assert(m_Casual < nMaxCasual);
			MultiMac::Add(m_Bufs.m_pCasual[m_Casual], gej, k);
		}

		void Calculate(secp256k1_gej& res)
		{
			assert(m_Casual <= nMaxCasual);
			MultiMac::Calculate(res, m_Bufs.m_pCasual);
		}
	};
} // namespace ECC_Min
