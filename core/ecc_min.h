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

		struct Prepared
		{
			static const int nBits = 4;
			static const int nMaxOdd = (1 << nBits) - 1;

			static const int nCount = (nMaxOdd >> 1) + 1;
			secp256k1_ge_storage m_pPt[nCount]; // odd powers

			typedef Wnaf_T<nBits> Wnaf;
		};

		Index m_Prepared;

		WnafBase::Shared m_ws;

		MultiMac() { Reset(); }

		void Reset();
		void Add(Prepared::Wnaf&, const secp256k1_scalar&);
		void Calculate(secp256k1_gej&, const Prepared*, Prepared::Wnaf*);


	private:

		struct Normalizer;
	};

	template <unsigned int nMaxCount>
	struct MultiMac_WithBufs
		:public MultiMac
	{
		Prepared m_pPrepared[nMaxCount];
		Prepared::Wnaf m_pWnaf[nMaxCount];

		void Add(const secp256k1_scalar& k)
		{
			static_assert(nMaxCount <= Index(-1));
			assert(m_Prepared < nMaxCount);
			MultiMac::Add(m_pWnaf[m_Prepared], k);
		}

		void Calculate(secp256k1_gej& res)
		{
			assert(m_Prepared <= nMaxCount);
			MultiMac::Calculate(res, m_pPrepared, m_pWnaf);
		}
	};
} // namespace ECC_Min
