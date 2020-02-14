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
		struct Prepared
		{
			static const uint8_t nBits = 4;
			static const int nMaxOdd = (1 << nBits) - 1;

			static const int nCount = (nMaxOdd >> 1) + 1;
			secp256k1_ge_storage m_pPt[nCount]; // odd powers
		};

		struct WNaf
		{
			struct Cursor
			{
				uint8_t m_iBit;
				uint8_t m_iElement;
				static const uint8_t s_HiBit = 0x80;

				static_assert(Prepared::nMaxOdd <= uint8_t(-1));

				bool FindCarry(const secp256k1_scalar&);
				void MoveAfterCarry(const secp256k1_scalar&);
				void MoveNext(const secp256k1_scalar&);
			};

			static uint8_t get_Bit(const secp256k1_scalar&, uint8_t iBit);
			static void xor_Bit(secp256k1_scalar&, uint8_t iBit);

			Cursor m_Pos;
			Cursor m_Neg;
		};

		struct Scalar
		{
			secp256k1_scalar m_Pos;
			secp256k1_scalar m_Neg;

			bool SplitPosNeg(); // returns carry
		};

		struct Context
		{
			secp256k1_gej* m_pRes;

			unsigned int m_Count;
			const Prepared* m_pPrep;
			Scalar* m_pS;
			WNaf* m_pWnaf;

			void Calculate() const;

		private:
			void Process(uint16_t iBit, unsigned int i, bool bNeg) const;
		};

	private:

		struct Normalizer;
	};

	template <unsigned int nMaxCount>
	struct MultiMac_WithBufs
		:public MultiMac
	{
		Prepared m_pPrepared[nMaxCount];
		WNaf m_pWnaf[nMaxCount];
		Scalar m_pS[nMaxCount];

		unsigned int m_Count = 0;

		void Reset() {
			m_Count = 0;
		}

		secp256k1_scalar& Add()
		{
			assert(m_Count < nMaxCount);
			return m_pS[m_Count++].m_Pos;
		}

		void Add(const secp256k1_scalar& k)
		{
			Add() = k;
		}

		void Calculate(secp256k1_gej& res)
		{
			Context ctx;
			ctx.m_pRes = &res;
			ctx.m_Count = m_Count;
			ctx.m_pPrep = m_pPrepared;
			ctx.m_pS = m_pS;
			ctx.m_pWnaf = m_pWnaf;
			ctx.Calculate();
		}
	};
} // namespace ECC_Min
