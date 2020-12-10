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

// big-uint implementation in a 'wasm-friendly' way.
// Everything via templates. No arrays, no mandatory memory store/load.
namespace MultiPrecision
{
	typedef uint32_t Word;
	typedef uint64_t DWord;
	typedef int64_t DWordSigned;

	static const uint32_t nWordBits = sizeof(Word) * 8;

	inline Word get_Safe(const Word* pSrc, uint32_t nSrc, uint32_t i)
	{
		return (i < nSrc) ? pSrc[i] : 0;
	}

	inline void SetShifted(Word* pDst, uint32_t nDst, const Word* pSrc, uint32_t nSrc, int nLShift)
	{
		uint32_t iSrc = -(nLShift / (int) nWordBits); // don't care about overflow
		nLShift %= nWordBits;

		if (nLShift)
		{
			if (nLShift > 0)
				iSrc--;
			else
				nLShift += nWordBits;

			uint32_t nRShift = nWordBits - nLShift;
			Word s0 = get_Safe(pSrc, nSrc, iSrc);

			for (uint32_t iDst = 0; iDst < nDst; iDst++)
			{
				Word s1 = get_Safe(pSrc, nSrc, ++iSrc);
				pDst[iDst] = (s0 >> nRShift) | (s1 << static_cast<uint32_t>(nLShift));
				s0 = s1;
			}
		}
		else
		{
			for (uint32_t iDst = 0; iDst < nDst; iDst++, iSrc++)
				pDst[iDst] = get_Safe(pSrc, nSrc, iSrc);
		}
	}

	template <uint32_t nWords_> struct UInt
		:public UInt<nWords_ - 1>
	{

		static const uint32_t nWords = nWords_;
		typedef UInt<nWords - 1> Base;

		Word m_Val;

		template <uint32_t nDepth>
		Word get_Val() const
		{
			if constexpr (nDepth == nWords)
				return m_Val;
			return Base::template get_Val<nDepth>();
		}

		template <uint32_t nDepth>
		void set_Val(Word x)
		{
			if constexpr (nDepth == nWords)
				m_Val = x;
			else
				Base::template set_Val<nDepth>(x);
		}

		void Set0()
		{
			m_Val = 0;
			Base::Set0();
		}

		UInt()
		{
			static_assert(sizeof(*this) == sizeof(Word) * nWords);
		}

		template <typename T>
		UInt(const T& x)
		{
			operator = (x);
		}

		void ToBE(Word* p) const
		{
			*p = Utils::FromBE(m_Val);
			Base::ToBE(p + 1);
		}

		void FromBE(const Word* p)
		{
			m_Val = Utils::FromBE(*p);
			Base::FromBE(p + 1);
		}

		template <typename T>
		void ToBE_T(T& arg) const
		{
			static_assert(sizeof(*this) == sizeof(arg), "");
			ToBE(reinterpret_cast<Word*>(&arg));
		}

		template <typename T>
		void FromBE_T(const T& arg)
		{
			static_assert(sizeof(*this) == sizeof(arg), "");
			FromBE(reinterpret_cast<const Word*>(&arg));
		}

		void operator = (uint64_t x)
		{
			set_Ord<0>(x);
		}

		template <uint32_t nShiftWords, typename T>
		void Set(T val)
		{
			set_Ord<nShiftWords, T>(val);
		}

		template <uint32_t wa>
		void Set(const UInt<wa>& a, int nLShift)
		{
			SetShifted(get_AsArr(), nWords, a.get_AsArr(), wa, nLShift);
		}

		const Word* get_AsArr() const
		{
			return reinterpret_cast<const Word*>(this);
		}

		Word* get_AsArr()
		{
			return reinterpret_cast<Word*>(this);
		}

		template <uint32_t wa>
		void operator += (const UInt<wa>& a)
		{
			SetAdd(*this, a);
		}

		template <uint32_t wa>
		void operator -= (const UInt<wa>& a)
		{
			SetSub(*this, a);
		}

		template <uint32_t wa>
		UInt< 1 + ((nWords >= wa) ? nWords : wa) > operator + (const UInt<wa>& a) const
		{
			UInt< 1 + ((nWords >= wa) ? nWords : wa) > ret;
			if constexpr (nWords <= wa)
				ret.SetAdd(*this, a);
			else
				ret.SetAdd(a, *this);
			return ret;
		}

		template <uint32_t wa>
		UInt< 1 + ((nWords >= wa) ? nWords : wa) > operator - (const UInt<wa>& a) const
		{
			UInt< 1 + ((nWords >= wa) ? nWords : wa) > ret;
			ret.SetSub(*this, a);
			return ret;
		}

		template <uint32_t wa>
		UInt<nWords + wa> operator * (const UInt<wa>& a) const
		{
			UInt<nWords + wa> ret;
			ret.Set0();

			if constexpr (nWords <= wa)
				AddMulInto(ret, a);
			else
				a.AddMulInto(ret, *this);

			return ret;
		}

		template <uint32_t wa>
		int cmp(const UInt<wa>& a) const
		{
			if constexpr (nWords >= wa)
				return _Cmp(a);

			return -a._Cmp(*this);
		}

		template <typename T> bool operator < (const T& x) const { return cmp(x) < 0; }
		template <typename T> bool operator > (const T& x) const { return cmp(x) > 0; }
		template <typename T> bool operator <= (const T& x) const { return cmp(x) <= 0; }
		template <typename T> bool operator >= (const T& x) const { return cmp(x) >= 0; }
		template <typename T> bool operator == (const T& x) const { return cmp(x) == 0; }
		template <typename T> bool operator != (const T& x) const { return cmp(x) != 0; }

		template <uint32_t wa, uint32_t wb>
		DWord SetAdd(const UInt<wa>& a, const UInt<wb>& b)
		{
			// *this = a + b
			DWord carry = Base::SetAdd(a, b);
			carry += a.template get_Val<nWords>();
			carry += b.template get_Val<nWords>();

			m_Val = (Word)carry;
			return carry >> nWordBits;
		}

		template <uint32_t wa, uint32_t wb>
		DWordSigned SetSub(const UInt<wa>& a, const UInt<wb>& b)
		{
			// *this = a + b
			DWordSigned carry = Base::SetAdd(a, b);
			carry += a.template get_Val<nWords>();
			carry -= b.template get_Val<nWords>();

			m_Val = (Word)carry;
			return carry >> nWordBits;
		}

	protected:
		template <uint32_t wx>
		friend struct UInt;

		template <uint32_t nShiftWords, typename T>
		T set_Ord(T val)
		{
			if constexpr (nShiftWords >= nWords)
			{
				m_Val = 0;
				Base::template set_Ord<nShiftWords>((T)0);
				return val;
			}

			val = Base::template set_Ord<nShiftWords>(val);
			m_Val = (Word)val;
			return val >> nWordBits;
		}

		template <uint32_t wDst>
		DWord MoveCarry(DWord carry)
		{
			carry += get_Val<wDst>();
			set_Val<wDst>((Word)carry);
			return carry >> nWordBits;
		}

		template <uint32_t wa, uint32_t wb>
		void AddMulInto(UInt<wa>& a, const UInt<wb>& b) const
		{
			// a += *this * b
			Base::AddMulInto(a, b);
			DWord carry = b.template AddMulInto2<wa, nWords>(a, m_Val);

			a.template MoveCarry<nWords + wb>(carry);
		}

		template <uint32_t wa, uint32_t wb>
		DWord AddMulInto2(UInt<wa>& a, Word b) const
		{
			// a.offset_by_w1 += *this * b
			DWord carry = Base::template AddMulInto2<wa, wb>(a, b);
			carry += ((DWord)b) * m_Val;
			return a.template MoveCarry<nWords + wb - 1>(carry);
		}

		template <uint32_t wa>
		int _Cmp(const UInt<wa>& a) const
		{
			if (m_Val < a.template get_Val<nWords>())
				return -1;

			if (m_Val > a.template get_Val<nWords>())
				return 1;

			return Base::_Cmp(a);
		}
	};

	template <> struct UInt<0>
	{
		void Set0() {}

		template <uint32_t nDepth>
		Word get_Val() const
		{
			return 0;
		}

		template <uint32_t nDepth>
		void set_Val(Word x)
		{
		}

		void ToBE(Word* p) const
		{
		}

		void FromBE(const Word* p)
		{
		}

		template <uint32_t wa, uint32_t wb>
		DWord SetAdd(const UInt<wa>&, const UInt<wb>&)
		{
			return 0;
		}

		template <uint32_t wa, uint32_t wb>
		DWordSigned SetSub(const UInt<wa>&, const UInt<wb>&)
		{
			return 0;
		}

		template <uint32_t wa, uint32_t wb>
		void AddMulInto(UInt<wa>& a, const UInt<wb>& b) const
		{
		}

		template <uint32_t wa, uint32_t wb>
		DWord AddMulInto2(UInt<wa>& a, Word b) const
		{
			return 0;
		}

		template <uint32_t nShiftWords, typename T>
		T set_Ord(T val)
		{
			return val;
		}

		template <uint32_t wa>
		int _Cmp(const UInt<wa>& a) const
		{
			return 0;
		}
	};

	template <uint32_t nShiftWords, typename T>
	UInt< (sizeof(T) + sizeof(Word) - 1) / sizeof(Word) + nShiftWords > FromEx(T x)
	{
		UInt< (sizeof(T) + sizeof(Word) - 1) / sizeof(Word) + nShiftWords > ret;
		ret.template Set<nShiftWords, T>(x);
		return ret;
	}

	template <typename T>
	UInt< (sizeof(T) + sizeof(Word) - 1) / sizeof(Word) > From(T x)
	{
		return FromEx<0, T>(x);
	}

} // namespace MultiPrecision

// the following is useful for Amount manipulation, while ensuring no overflow
namespace Strict {

	template <typename T>
	inline void Add(T& a, const T& b)
	{
		a += b;
		Env::Halt_if(a < b); // overflow
	}

	template <typename T>
	inline void Sub(T& a, const T& b)
	{
		Env::Halt_if(a < b); // not enough
		a -= b;
	}

} // namespace Strict

namespace Utils {

	template <typename T>
	inline T AverageUnsigned(T a, T b)
	{
		a += b;
		bool bHasCarry = (a < b); // msb leaked out
		a /= 2;

		if (bHasCarry)
			// halved carry turns into msb
			a |= ((T)1) << (sizeof(T) * 8 - 1);

		return a;
	}

} // namespace Utils
