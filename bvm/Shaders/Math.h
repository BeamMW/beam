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

	static const uint32_t nWordBits = sizeof(Word) * 8;

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
		void operator += (const UInt<wa>& a)
		{
			SetAdd(*this, a);
		}

		template <uint32_t wa>
		UInt< 1 + ((nWords >= wa) ? nWords : wa) > operator + (const UInt<wa>& a)
		{
			UInt< 1 + ((nWords >= wa) ? nWords : wa) > ret;
			if constexpr (nWords <= wa)
				ret.SetAdd(*this, a);
			else
				ret.SetAdd(a, *this);
			return ret;
		}

		template <uint32_t wa>
		UInt<nWords + wa> operator * (const UInt<wa>& a)
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

		template <uint32_t wa, uint32_t wb>
		DWord SetAdd(const UInt<wa>&, const UInt<wb>&)
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
