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
		static int _Cmp(const uint8_t* pSrc0, uint32_t nSrc0, const uint8_t* pSrc1, uint32_t nSrc1);
		static void _Print(const uint8_t* pDst, uint32_t nDst, std::ostream&);
		static void _Print(const uint8_t* pDst, uint32_t nDst, char*);

		static uint32_t _GetOrder(const uint8_t* pDst, uint32_t nDst);
		static bool _Accept(uint8_t* pDst, const uint8_t* pThr, uint32_t nDst, uint32_t nThrOrder);

		template <typename T>
		static void _AssignRangeAligned(uint8_t* pDst, uint32_t nDst, T x, uint32_t nOffsetBytes, uint32_t nBytesX)
		{
			static_assert(T(-1) > 0, "must be unsigned");

			assert(nDst >= nBytesX + nOffsetBytes);
			nDst -= (nOffsetBytes + nBytesX);

			for (uint32_t i = nBytesX; i--; x >>= 8)
				pDst[nDst + i] = (uint8_t) x;
		}

		template <typename T>
		static bool _AssignRangeAlignedSafe(uint8_t* pDst, uint32_t nDst, T x, uint32_t nOffsetBytes, uint32_t nBytesX) // returns false if truncated
		{
			if (nDst < nOffsetBytes)
				return false;

			uint32_t n = nDst - nOffsetBytes;
			bool b = (nBytesX <= n);

			_AssignRangeAligned<T>(pDst, nDst, x, nOffsetBytes, b ? nBytesX : n);
			return b;
		}

		template <typename T>
		static bool _AssignSafe(uint8_t* pDst, uint32_t nDst, T x, uint32_t nOffset) // returns false if truncated
		{
			uint32_t nOffsetBytes = nOffset >> 3;
			nOffset &= 7;

			if (!_AssignRangeAlignedSafe<T>(pDst, nDst, x << nOffset, nOffsetBytes, sizeof(x)))
				return false;

			if (nOffset)
			{
				nOffsetBytes += sizeof(x);
				if (nDst - 1 < nOffsetBytes)
					return false;

				uint8_t resid = uint8_t(x >> ((sizeof(x) << 3) - nOffset));
				pDst[nDst - 1 - nOffsetBytes] = resid;
			}

			return true;
		}

		template <typename T>
		static void _ExportAligned(T& out, const uint8_t* pDst, uint32_t nDst)
		{
			static_assert(T(-1) > 0, "must be unsigned");

			out = pDst[0];
			for (uint32_t i = 1; i < nDst; i++)
				out = (out << 8) | pDst[i];
		}

		static void _ShiftRight(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc, uint32_t nBits);
		static void _ShiftLeft(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc, uint32_t nBits);

	};

	template <uint32_t nBytes_>
	struct uintBig_t
		:public uintBigImpl
	{
		static const uint32_t nBits = nBytes_ << 3;
		static const uint32_t nBytes = nBytes_;

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
			_Assign(m_pData, nBytes, v.begin(), static_cast<uint32_t>(v.size()));
        }

		uintBig_t(const Blob& v)
		{
			operator = (v);
		}

		template <typename T>
		uintBig_t(T x)
		{
			AssignOrdinal(x);
		}

		// in Big-Endian representation
		uint8_t m_pData[nBytes];

		uintBig_t& operator = (Zero_)
		{
			ZeroObject(m_pData);
			return *this;
		}

		template <uint32_t nBytesOther_>
		uintBig_t& operator = (const uintBig_t<nBytesOther_>& v)
		{
			_Assign(m_pData, nBytes, v.m_pData, v.nBytes);
			return *this;
		}

		uintBig_t& operator = (const Blob& v)
		{
			_Assign(m_pData, nBytes, static_cast<const uint8_t*>(v.p), v.n);
			return *this;
		}

		bool operator == (Zero_) const
		{
			return memis0(m_pData, nBytes);
		}

		template <typename T>
		void AssignOrdinal(T x)
		{
			memset0(m_pData, nBytes - sizeof(x));
			AssignRange<T, 0>(x);
		}

		// from ordinal types (unsigned)
		template <typename T>
		uintBig_t& operator = (T x)
		{
			AssignOrdinal(x);
			return *this;
		}

		template <typename T>
		void Export(T& x) const
		{
			static_assert(sizeof(T) >= nBytes, "");
			_ExportAligned(x, m_pData, nBytes);
		}

		template <uint32_t iWord, typename T>
		void ExportWord(T& x) const
		{
			static_assert(sizeof(T) * (iWord + 1) <= nBytes, "");
			_ExportAligned(x, m_pData + sizeof(T) * iWord, sizeof(T));
		}

		template <typename T, uint32_t nOffset>
		void AssignRange(T x)
		{
			static_assert(!(nOffset & 7), "offset must be on byte boundary");
			static_assert(nBytes >= sizeof(x) + (nOffset >> 3), "too small");

			_AssignRangeAligned<T>(m_pData, nBytes, x, nOffset >> 3, sizeof(x));
		}

		template <typename T>
		bool AssignSafe(T x, uint32_t nOffset) // returns false if truncated
		{
			return _AssignSafe(m_pData, nBytes, x, nOffset);
		}

		void Inc()
		{
			_Inc(m_pData, nBytes);
		}

		template <uint32_t nBytesOther_>
		void operator += (const uintBig_t<nBytesOther_>& x)
		{
			_Inc(m_pData, nBytes, x.m_pData, x.nBytes);
		}

		template <uint32_t nBytes0, uint32_t nBytes1>
		void AssignMul(const uintBig_t<nBytes0>& x0, const uintBig_t<nBytes1> & x1)
		{
			_Mul(m_pData, nBytes, x0.m_pData, x0.nBytes, x1.m_pData, x1.nBytes);
		}

		template <uint32_t nBytesOther_>
		uintBig_t<nBytes + nBytesOther_> operator * (const uintBig_t<nBytesOther_>& x) const
		{
			uintBig_t<nBytes + nBytesOther_> res;
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

		template <uint32_t nBytesOther_>
		void operator ^= (const uintBig_t<nBytesOther_>& x)
		{
			_Xor(m_pData, nBytes, x.m_pData, x.nBytes);
		}

		template <uint32_t nBytesOther_>
		int cmp(const uintBig_t<nBytesOther_>& x) const
		{
			return _Cmp(m_pData, nBytes, x.m_pData, x.nBytes);
		}

		uint32_t get_Order() const
		{
			// how much the number should be shifted to reach zero.
			// returns 0 iff the number is already zero.
			return _GetOrder(m_pData, nBytes);
		}

		template <uint32_t nBytesOther_>
		void ShiftRight(uint32_t nBits, uintBig_t<nBytesOther_>& res) const
		{
			_ShiftRight(res.m_pData, res.nBytes, m_pData, nBytes, nBits);
		}

		template <uint32_t nBytesOther_>
		void ShiftLeft(uint32_t nBits, uintBig_t<nBytesOther_>& res) const
		{
			_ShiftLeft(res.m_pData, res.nBytes, m_pData, nBytes, nBits);
		}

		// helper, for uniform random generation within specific bounds
		struct Threshold
		{
			const uintBig_t& m_Val;
			uint32_t m_Order;

			Threshold(const uintBig_t& val)
				:m_Val(val)
			{
				m_Order = val.get_Order();
			}

			operator bool() const { return m_Order > 0; }

			bool Accept(uintBig_t& dst) const
			{
				return _Accept(dst.m_pData, m_Val.m_pData, nBytes, m_Order);
			}
		};

		COMPARISON_VIA_CMP

		static const uint32_t nTxtLen = nBytes * 2; // not including 0-term

		void Print(char* sz) const
		{
			_Print(m_pData, nBytes, sz);
		}

		friend std::ostream& operator << (std::ostream& s, const uintBig_t& x)
		{
			_Print(x.m_pData, x.nBytes, s);
			return s;
		}
	};

	template <typename T>
	struct uintBigFor {
		typedef uintBig_t<sizeof(T)> Type;
	};

	template <typename T>
	inline typename uintBigFor<T>::Type uintBigFrom(T x) {
		return typename uintBigFor<T>::Type(x);
	}

	struct FourCC
	{
		uint32_t V; // In "host" order, i.e. platform-dependent
		operator uint32_t () const { return V; }

		FourCC() {}
		FourCC(uint32_t x) :V(x) {}

		struct Text
		{
			char m_sz[sizeof(uint32_t) + 1];
			Text(uint32_t);
			operator const char* () const { return m_sz; }
		};

		template <uint8_t a, uint8_t b, uint8_t c, uint8_t d>
		struct Const {
			static const uint32_t V = (((((a << 8) | b) << 8) | c) << 8) | d;
		};

	};

	std::ostream& operator << (std::ostream& s, const FourCC::Text& x);
	std::ostream& operator << (std::ostream& s, const FourCC& x);

#define ARRAY_ELEMENT_SAFE(arr, index) ((arr)[(((index) < _countof(arr)) ? (index) : (_countof(arr) - 1))])
#define FOURCC_FROM(name) beam::FourCC::Const<ARRAY_ELEMENT_SAFE(#name,0), ARRAY_ELEMENT_SAFE(#name,1), ARRAY_ELEMENT_SAFE(#name,2), ARRAY_ELEMENT_SAFE(#name,3)>::V

} // namespace beam
