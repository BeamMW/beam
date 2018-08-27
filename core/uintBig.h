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
#include "common.h"

namespace beam
{
	// Syntactic sugar!
	enum Zero_ { Zero };

	// Simple arithmetics. For casual use only (not performance-critical)

	class uintBigImpl {
	protected:
		void _Assign(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc);

		// all those return carry (exceeding byte)
		static uint8_t _Inc(uint8_t* pDst, uint32_t nDst);
		static uint8_t _Inc(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc);
		static uint8_t _Inc(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc);

		static void _Inv(uint8_t* pDst, uint32_t nDst);
		static void _Xor(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc);
		static void _Xor(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc);

		static void _Mul(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc0, uint32_t nSrc0, const uint8_t* pSrc1, uint32_t nSrc1);

		static void _Print(const uint8_t* pDst, uint32_t nDst, std::ostream&);
	};

	template <uint32_t nBits_>
	struct uintBig_t
		:public uintBigImpl
	{
		static_assert(!(7 & nBits_), "should be byte-aligned");

		static const uint32_t nBits = nBits_;
		static const uint32_t nBytes = nBits_ >> 3;

        uintBig_t()
        {
#ifdef _DEBUG
			memset(m_pData, 0xcd, nBytes);
#endif // _DEBUG
        }

		uintBig_t(Zero_)
		{
			ZeroObject(m_pData);
		}

		uintBig_t(const uint8_t p[nBytes])
		{
			memcpy(m_pData, p, nBytes);
		}

        uintBig_t(const std::initializer_list<uint8_t>& v)
        {
			_Assign(m_pData, nBytes, v.begin(), v.size());
        }

		uintBig_t(const std::vector<uint8_t>& v)
		{
			_Assign(m_pData, nBytes, v.empty() ? NULL : &v.at(0), v.size());
		}

		// in Big-Endian representation
		uint8_t m_pData[nBytes];

		uintBig_t& operator = (Zero_)
		{
			ZeroObject(m_pData);
			return *this;
		}

		template <uint32_t nBitsOther_>
		uintBig_t& operator = (const uintBig_t<nBitsOther_>& v)
		{
			_Assign(m_pData, nBytes, v.m_pData, v.nBytes);
			return *this;
		}

		bool operator == (Zero_) const
		{
			return memis0(m_pData, nBytes);
		}

		// from ordinal types (unsigned)
		template <typename T>
		uintBig_t& operator = (T x)
		{
			memset0(m_pData, nBytes - sizeof(x));
			AssignRange<T, 0>(x);

			return *this;
		}

		template <typename T>
		void AssignRangeAligned(T x, uint32_t nOffsetBytes, uint32_t nBytes_)
		{
			assert(nBytes >= nBytes_ + nOffsetBytes);
			static_assert(T(-1) > 0, "must be unsigned");

			for (uint32_t i = 0; i < nBytes_; i++, x >>= 8)
				m_pData[nBytes - 1 - nOffsetBytes - i] = (uint8_t) x;
		}

		template <typename T, uint32_t nOffset>
		void AssignRange(T x)
		{
			static_assert(!(nOffset & 7), "offset must be on byte boundary");
			static_assert(nBytes >= sizeof(x) + (nOffset >> 3), "too small");

			AssignRangeAligned<T>(x, nOffset >> 3, sizeof(x));
		}

		template <typename T>
		bool AssignRangeAlignedSafe(T x, uint32_t nOffsetBytes, uint32_t nBytes_) // returns false if truncated
		{
			if (nBytes < nOffsetBytes)
				return false;

			uint32_t n = nBytes - nOffsetBytes;
			bool b = (nBytes_ <= n);

			AssignRangeAligned<T>(x, nOffsetBytes, b ? nBytes_ : n);
			return b;
		}

		template <typename T>
		bool AssignSafe(T x, uint32_t nOffset) // returns false if truncated
		{
			static_assert(T(-1) > 0, "must be unsigned");

			uint32_t nOffsetBytes = nOffset >> 3;
			nOffset &= 7;

			if (!AssignRangeAlignedSafe<T>(x << nOffset, nOffsetBytes, sizeof(x)))
				return false;

			if (nOffset)
			{
				nOffsetBytes += sizeof(x);
				if (nBytes - 1 < nOffsetBytes)
					return false;

				uint8_t resid = x >> ((sizeof(x) << 3) - nOffset);
				m_pData[nBytes - 1 - nOffsetBytes] = resid;
			}

			return true;
		}

		void Inc()
		{
			_Inc(m_pData, nBytes);
		}

		template <uint32_t nBitsOther_>
		void operator += (const uintBig_t<nBitsOther_>& x)
		{
			_Inc(m_pData, nBytes, x.m_pData, x.nBytes);
		}

		template <uint32_t nBits0, uint32_t nBits1>
		void AssignMul(const uintBig_t<nBits0>& x0, const uintBig_t<nBits1> & x1)
		{
			_Mul(m_pData, nBytes, x0.m_pData, x0.nBytes, x1.m_pData, x1.nBytes);
		}

		template <uint32_t nBitsOther_>
		uintBig_t operator * (const uintBig_t<nBitsOther_>& x) const
		{
			uintBig_t res;
			res.AssignMul(*this, x);
			return res;
		}

		void Inv()
		{
			_Inv(m_pData, nBytes);
		}

		void Negate()
		{
			Inv();
			Inc();
		}

		template <uint32_t nBitsOther_>
		void operator ^= (const uintBig_t<nBitsOther_>& x)
		{
			_Xor(m_pData, nBytes, x.m_pData, x.nBytes);
		}

		int cmp(const uintBig_t& x) const { return memcmp(m_pData, x.m_pData, nBytes); }
		COMPARISON_VIA_CMP(uintBig_t)


		friend std::ostream& operator << (std::ostream& s, const uintBig_t& x)
		{
			_Print(x.m_pData, x.nBytes, s);
			return s;
		}
	};

} // namespace beam
