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

	template <uint32_t nBits_>
	struct uintBig_t
	{
		static_assert(!(7 & nBits_), "should be byte-aligned");

		static const uint32_t nBits = nBits_;
		static const uint32_t nBytes = nBits_ >> 3;

        uintBig_t()
        {
            ZeroObject(m_pData);
        }

		uintBig_t(const uint8_t p[nBytes])
		{
			memcpy(m_pData, p, nBytes);
		}

        uintBig_t(const std::initializer_list<uint8_t>& v)
        {
			FromArray(v.begin(), v.size());
        }

		uintBig_t(const std::vector<uint8_t>& v)
		{
			FromArray(v.empty() ? NULL : &v.at(0), v.size());
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
			FromArray(v.m_pData, v.nBytes);
			return *this;
		}

		void FromArray(const uint8_t* p, uint32_t nBytes_)
		{
			if (nBytes_ >= nBytes)
				memcpy(m_pData, p + nBytes_ - nBytes, nBytes);
			else
			{
				memset0(m_pData, nBytes - nBytes_);
				memcpy(m_pData + nBytes - nBytes_, p, nBytes_);
			}
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
			for (uint32_t i = nBytes; i--; )
				if (++m_pData[i])
					break;

		}

		// Simple arithmetics. For casual use only (not performance-critical)
		void operator += (const uintBig_t& x)
		{
			uint16_t carry = 0;
			for (int i = nBytes; i--; )
			{
				carry += m_pData[i];
				carry += x.m_pData[i];

				m_pData[i] = (uint8_t) carry;
				carry >>= 8;
			}
		}

		uintBig_t operator * (const uintBig_t& x) const
		{
			uintBig_t res;
			res = Zero;

			for (size_t j = 0; j < nBytes; j++)
			{
				uint8_t y = x.m_pData[nBytes - 1 - j];

				uint16_t carry = 0;
				for (size_t i = nBytes; i-- > j; )
				{
					uint16_t val = m_pData[i];
					val *= y;
					carry += val;
					carry += res.m_pData[i - j];

					res.m_pData[i - j] = (uint8_t) carry;
					carry >>= 8;
				}
			}

			return res;
		}

		void Inv()
		{
			for (int i = nBytes; i--; )
				m_pData[i] ^= 0xff;
		}

		void Negate()
		{
			Inv();
			Inc();
		}

		void operator ^= (const uintBig_t& x)
		{
			for (unsigned int i = nBytes; i--; )
				m_pData[i] ^= x.m_pData[i];
		}

		int cmp(const uintBig_t& x) const { return memcmp(m_pData, x.m_pData, nBytes); }
		COMPARISON_VIA_CMP(uintBig_t)


		friend std::ostream& operator<< (std::ostream& stream, const uintBig_t& matrix);
	};

} // namespace beam
