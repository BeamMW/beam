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

#ifdef _MSC_VER
#   define __restrict__ __restrict
#endif // 

namespace beam {

namespace NumericUtils
{
	template <typename T>
	uint32_t clz(T x); // count leading zeroes

	namespace details
	{
		template <uint32_t n>
		struct Power
		{
			template <uint64_t x>
			struct Of
			{
				static const uint64_t N = Power<n / 2>::template Of<x>::N * Power<n - (n/2)>::template Of<x>::N;
			};

			template <typename T, T base>
			static uint32_t Log(T& res, T x)
			{
				assert(x < Of<base>::N);

				const uint32_t nHalf = (n + 1) / 2; // round to bigger side
				const T vRoot = Power<nHalf>::template Of<base>::N;

				bool bBigger = (x >= vRoot);
				if (bBigger)
					x /= vRoot;

				uint32_t nRet = Power<nHalf>::template Log<T, base>(res, x);

				if (bBigger)
				{
					nRet += nHalf;
					res *= vRoot;
				}

				return nRet;
			}

			template <typename T, T base>
			static T Raise(uint32_t n_)
			{
				const uint32_t nHalf = (n + 1) / 2; // round to bigger side

				if (n_ < n)
					return Power<nHalf>::template Raise<T, base>(n_);

				return Of<base>::N * Power<nHalf>::template Raise<T, base>(n_ - n);
			}

		};

		template <>
		struct Power<1>
		{
			template <uint64_t x>
			struct Of
			{
				static const uint64_t N = x;
			};

			template <typename T, T base>
			static uint32_t Log(T& res, T x)
			{
				assert(x < base);

				res = 1;
				return 0;
			}

			template <typename T, T base>
			static T Raise(uint32_t n_)
			{
				return n_ ? base : 1;
			}

		};

		template <uint64_t n>
		struct Log
		{
			template <uint64_t base>
			struct Of
			{
				// rounded down
				static const uint64_t N = 1 + Log<n / base>::template Of<base>::N;
			};
		};

		template <>
		struct Log<0>
		{
			template <uint64_t base>
			struct Of
			{
				static const int32_t N = -1;
			};
		};

	}

	// Compile-time power and log

	template <uint64_t x, uint32_t n>
	struct PowerOf
	{
		static const uint64_t N = details::Power<n>::template Of<x>::N;
	};

	template <uint64_t x, uint32_t base>
	struct LogOf
	{
		// rounded down
		static const uint32_t N = details::Log<x>::template Of<base>::N;
	};

	template <typename T, T base>
	T Power(uint32_t n)
	{
		// find highest power, for which base^n <= bound
		const uint32_t nMax = LogOf<(T) -1, base>::N;
		return details::Power<nMax>::template Raise<T, base>(n);
	}

	template <typename T, T base>
	uint32_t Log(T& res, T x)
	{
		// find highest power, for which base^n <= bound
		const uint32_t nMax = LogOf<(T) -1, base>::N;
		const T vMax = PowerOf<base, nMax>::N;

		if (x >= vMax)
		{
			res = vMax;
			return nMax;
		}

		return details::Power<nMax>::template Log<T, base>(res, x);
	}

	// radix convertion, compile-time length calculation. Not the most effective algorithm, but should be ok for compile-time
    template <uint32_t aRadix, const uint32_t aLen, uint32_t bRadix>
    struct RadixConverter
    {
        static constexpr uint32_t get_MaxLen()
        {
            uint32_t pW[aLen + 1] = { 0 };
            pW[0] = 1;
            uint32_t nW = 1;

            // calculate aRadix^aLen
            for (uint32_t i = 0; i < aLen; i++)
            {
                uint64_t carry = 0;
                for (uint32_t j = 0; j < nW; j++)
                {
                    carry += ((uint64_t)pW[j]) * aRadix;
                    pW[j] = (uint32_t)carry;
                    carry >>= 32;
                }
                if (carry)
                    pW[nW++] = (uint32_t)carry;
            }

            // subtract 1
            for (uint32_t j = 0; ; j++)
                if (pW[j]--)
                    break;

            // calculate log, round up (how many times to devide before it turns zero)
            uint32_t retVal = 0;

            while (nW)
            {
                if (pW[nW - 1])
                {
                    // divide by new radix
                    uint64_t carry = 0;
                    for (uint32_t j = nW; j--; )
                    {
                        carry |= pW[j];
                        pW[j] = (uint32_t)(carry / bRadix);
                        carry %= bRadix;
                        carry <<= 32;
                    }

                    retVal++;
                }
                else
                    nW--;
            }

            return retVal;
        }
    };

}

namespace MultiWord {

	typedef uint32_t Word;
	typedef uint64_t DWord;
	typedef int64_t DWordSigned;

	static const uint32_t nWordBits = sizeof(Word) * 8;

	template <typename T>
	struct BaseSlice
	{
		T* m_p; // msb at the beginning
		uint32_t m_n;

		T get_Safe(uint32_t i) const
		{
			return (i < m_n) ? m_p[i] : 0;
		}

		BaseSlice get_Head(uint32_t n) const
		{
			assert(n <= m_n);
			return BaseSlice{ m_p, n };
		}

		T* get_TailPtr(uint32_t n) const
		{
			assert(n <= m_n);
			return m_p + m_n - n;
		}

		BaseSlice get_Tail(uint32_t n) const
		{
			return BaseSlice{ get_TailPtr(n), n };
		}

		BaseSlice CutTail(uint32_t n)
		{
			auto ret = get_Tail(n);
			m_n -= n;
			return ret;
		}

		bool IsZero() const
		{
			for (uint32_t i = 0; i < m_n; i++)
				if (m_p[i])
					return false;
			return true;
		}

		void Trim()
		{
			// remove leading zeroes
			while (m_n)
			{
				if (m_p[0])
					break;

				m_p++;
				m_n--;
			}
		}

		int cmp(BaseSlice x) const
		{
			auto me = *this;
			me.Trim();
			x.Trim();

			if (me.m_n < x.m_n)
				return -1;
			if (me.m_n > x.m_n)
				return 1;

			for (uint32_t i = 0; i < me.m_n; i++)
			{
				if (me.m_p[i] < x.m_p[i])
					return -1;
				if (me.m_p[i] > x.m_p[i])
					return 1;
			}

			return 0;
		}
	};

	typedef BaseSlice<const Word> ConstSlice;

	struct Slice
		:public BaseSlice<Word>
	{
		// msb at the beginning

		ConstSlice get_Const() const
		{
			return ConstSlice{ m_p, m_n };
		}

		void Fill(Word w) const
		{
			for (uint32_t i = 0; i < m_n; i++)
				m_p[i] = w;
		}

		void Set0() const { Fill(0); }
		void SetMax() const { Fill(static_cast<Word>(-1)); }

		void Copy(ConstSlice) const;

		template <bool bAdd>
		void Set_AddOrSub(ConstSlice a, ConstSlice b, DWord& carry) const
		{
			Copy(a);
			AddOrSub<bAdd>(b, carry);
		}

		template <bool bAdd>
		void AddOrSub(ConstSlice b, DWord& carry) const;

		void SetMul(ConstSlice a, ConstSlice b) const
		{
			Set0();
			AddOrSub_Mul<true>(a, b);
		}

		template <bool bAdd>
		void AddOrSub_Mul(ConstSlice a, ConstSlice b) const;

		template <bool bAdd>
		void AddOrSub_Mul(ConstSlice a, Word val) const
		{
			ConstSlice b{ &val, 1 };
			AddOrSub_Mul<true>(a, b);
		}

		void SetDivResid(Slice resid, ConstSlice div, Word* __restrict__ pBufResid, Word* __restrict__ pBufDiv) const;

		void LShift(ConstSlice a, uint32_t nBits) const;
		void RShift(ConstSlice a, uint32_t nBits) const;

		DWord Mul(Word) const; // returns carry
		void Power(ConstSlice, uint32_t n, Word* __restrict__ pBuf1, Word* __restrict__ pBuf2) const; // both buffers must be of current slice size

		Word SetDiv(Word div); // returns resid, trims itself

		template <typename T>
		void DecomposeDestroy(T* pDst, uint32_t nDst, Word radix)
		{
			static_assert(std::is_integral_v<T>, "");
			assert(radix > 1);

			while (nDst--)
				pDst[nDst] = static_cast<T>(SetDiv(radix));
		}

		template <typename T>
		static void DecomposeSplitPwr(T* pDst, uint32_t nDst, Word radix, uint32_t nPwr, ConstSlice src)
		{
			src.Trim();
			while (src.m_n--)
			{
				Word val = src.m_p[src.m_n];

				for (uint32_t i = 0; i < nPwr; i++)
				{
					if (!nDst)
						return;
					pDst[--nDst] = static_cast<T>(val % radix);

					val /= radix;
				}
			}

			while (nDst--)
				pDst[nDst] = 0;
		}

		template <typename T>
		void Compose(const T* pSrc, uint32_t nSrc, Word radix, Word* __restrict__ pBuf) const // buf must be of current slice size
		{
			static_assert(std::is_integral_v<T>, "");
			assert(radix > 1);

			Set0();
			if (!nSrc)
				return;

			Slice sPwr{ pBuf + m_n - 1, 1 };
			sPwr.m_p[0] = 1;

			while (true)
			{
				Word w = static_cast<Word>(pSrc[--nSrc]);
				AddOrSub_Mul<true>(sPwr.get_Const(), w);

				if (!nSrc)
					break;

				if (!PowerNext(sPwr, radix))
					break;
			}
		}

		template <typename T>
		void ComposeEx(const T* pSrc, uint32_t nSrc, Word radix, Word* __restrict__ pBuf, Word radixBig, uint32_t nPwr) const // buf must be of current slice size
		{
			static_assert(std::is_integral_v<T>, "");
			assert(radixBig > 1);

			Set0();
			if (!nSrc)
				return;

			Slice sPwr{ pBuf + m_n - 1, 1 };
			sPwr.m_p[0] = 1;

			while (true)
			{
				uint32_t nPortion = std::min(nSrc, nPwr);
				nSrc -= nPortion;

				Word w = static_cast<Word>(pSrc[nSrc]);
				for (uint32_t i = 1; i < nPortion; i++)
					w = w * radix + static_cast<Word>(pSrc[nSrc + i]);

				AddOrSub_Mul<true>(sPwr.get_Const(), w);

				if (!nSrc)
					break;

				if (!PowerNext(sPwr, radixBig))
					break;
			}
		}

	private:

		static void LShiftRaw(Word* pDst, const Word* pSrc, uint32_t nLen, uint32_t nShiftL, Word& w);
		static void RShiftRaw(Word* pDst, const Word* pSrc, uint32_t nLen, uint32_t nShiftR, Word& w);
		void LShiftNormalized(uint32_t nShiftL, ConstSlice src) const;
		void RShiftNormalized(uint32_t nShiftR, ConstSlice src) const;
		void LShiftNormalized_nnz(uint32_t nShiftL, ConstSlice src) const;
		void RShiftNormalized_nnz(uint32_t nShiftR, ConstSlice src) const;

		void CopyInternal(const Word* p) const;

		static void AddToWord(Word& w, DWord& carry)
		{
			carry += w;
			w = static_cast<Word>(carry);
		}

		template <bool bPositive>
		static void MoveCarry(DWord& carry)
		{
			if constexpr (bPositive)
				carry >>= nWordBits;
			else
				((DWordSigned&) carry) >>= nWordBits;
		}

		template <bool bAdd, bool bOp2>
		void AddOrSub_Internal(const Word* b, DWord& carry) const;

		template <bool bAdd>
		void AddOrSub_Mul_Once(ConstSlice a, Word mul, DWord& carry) const;

		template <bool bAdd, bool bFit>
		void AddOrSubMul_Internal(ConstSlice a, ConstSlice b) const;

		void SetDivResidNormalized(Slice resid, ConstSlice div) const;

		bool PowerNext(Slice& sPwr, Word radix) const;
	};

	template <uint32_t nWords_>
	struct Number
	{
		static const uint32_t nWords = nWords_;
		static const uint32_t nSize = sizeof(Word) * nWords;

		Word m_p[nWords];

		Number()
		{
			static_assert(sizeof(Number) == nSize, "");
		}
		template <typename T>
		Number(const T& x)
		{
			static_assert(sizeof(Number) == nSize, "");
			operator = (x);
		}

		ConstSlice get_ConstSlice() const {
			return ConstSlice{ m_p, nWords };
		}

		Slice get_Slice() {
			return Slice{ m_p, nWords };
		}

		Number& operator = (Word x)
		{
			static_assert(nWords >= 1);
			m_p[nWords - 1] = x;

			if constexpr (nWords > 1)
				get_Slice().get_Head(nWords - 1).Set0();

			return *this;
		}

		Number& operator = (uint64_t x)
		{
			static_assert(nWords >= 1);
			m_p[nWords - 1] = static_cast<Word>(x);

			if constexpr (nWords > 1)
			{
				m_p[nWords - 2] = static_cast<Word>(x >> nWordBits);

				if constexpr (nWords > 2)
					get_Slice().get_Head(nWords - 2).Set0();
			}

			return *this;
		}

		void Export(Word& x) const
		{
			static_assert(nWords >= 1);
			x = m_p[nWords - 1];
		}

		void Export(DWord& x) const
		{
			static_assert(nWords >= 2);
			x = (static_cast<DWord>(m_p[nWords - 2]) << nWordBits) | m_p[nWords - 1];
		}

		template <uint32_t wa>
		Number& operator = (const Number<wa>& a)
		{
			get_Slice().Copy(a.get_ConstSlice());
			return *this;
		}

		template <uint32_t wa>
		int cmp(const Number<wa>& a) const
		{
			return get_ConstSlice().cmp(a.get_ConstSlice());
		}

		template <typename T> bool operator < (const T& x) const { return cmp(x) < 0; }
		template <typename T> bool operator > (const T& x) const { return cmp(x) > 0; }
		template <typename T> bool operator <= (const T& x) const { return cmp(x) <= 0; }
		template <typename T> bool operator >= (const T& x) const { return cmp(x) >= 0; }
		template <typename T> bool operator == (const T& x) const { return cmp(x) == 0; }
		template <typename T> bool operator != (const T& x) const { return cmp(x) != 0; }

		template <uint32_t wa>
		Number& operator += (const Number<wa>& a)
		{
			DWord carry = 0;
			get_Slice().AddOrSub<true>(a.get_ConstSlice(), carry);
			return *this;
		}

		template <uint32_t wa>
		Number& operator -= (const Number<wa>& a)
		{
			DWord carry = 0;
			get_Slice().AddOrSub<false>(a.get_ConstSlice(), carry);
			return *this;
		}

		template <uint32_t wa>
		Number< 1 + ((nWords >= wa) ? nWords : wa) > operator + (const Number<wa>& a) const
		{
			Number< 1 + ((nWords >= wa) ? nWords : wa) > ret;
			if constexpr (nWords >= wa)
			{
				ret = *this;
				ret += a;
			}
			else
			{
				ret = a;
				ret += *this;
			}

			return ret;
		}

		template <uint32_t wa>
		Number< 1 + ((nWords >= wa) ? nWords : wa) > operator - (const Number<wa>& a) const
		{
			Number< 1 + ((nWords >= wa) ? nWords : wa) > ret;
			ret = *this;
			ret -= a;
			return ret;
		}

		template <uint32_t wa>
		Number<nWords + wa> operator * (const Number<wa>& a) const
		{
			Number<nWords + wa> ret;
			ret.get_Slice().Set0();
			ret.get_Slice().AddOrSub_Mul<true>(get_ConstSlice(), a.get_ConstSlice());
			return ret;
		}

		template <uint32_t wa>
		Number& operator *= (const Number<wa>& a)
		{
			if constexpr (wa == 1)
				get_Slice().Mul(a.m_p[0]);
			else
			{
				auto x = *this; // copy
				get_Slice().Set0();
				get_Slice().AddOrSub_Mul<true>(x.get_ConstSlice(), a.get_ConstSlice());
			}
			return *this;
		}

		template <uint32_t wResid, uint32_t wDiv>
		void SetDivResid(Number<wResid>& __restrict__ resid, const Number<wDiv>& __restrict__ div)
		{
			Word pBufResid[wResid + 1];
			Word pBufDiv[wDiv];
			get_Slice().SetDivResid(resid.get_Slice(), div.get_ConstSlice(), pBufResid, pBufDiv);
		}

		template <uint32_t wResid, uint32_t wDiv>
		void SetDiv(Number<wResid> resid, const Number<wDiv>& __restrict__ div)
		{
			// resid is passed by val
			SetDivResid(resid, div);
		}

		template <uint32_t wDiv>
		Number<nWords> operator / (const Number<wDiv>& __restrict__ div) const
		{
			Number<nWords> ret;
			ret.SetDiv(*this, div);
			return ret;
		}

		template <uint32_t wa>
		Number& Power(const Number<wa>& __restrict__ x, uint32_t n)
		{
			Word pBuf1[nWords], pBuf2[nWords];
			get_Slice().Power(x.get_ConstSlice(), n, pBuf1, pBuf2);
			return *this;
		}

		template <Word radix>
		static constexpr uint32_t get_Decomposed_MaxLen()
		{
			return NumericUtils::RadixConverter<(1u << (nWordBits / 2)), nWords * 2, radix>::get_MaxLen();
		}

		template <typename T>
		void Decompose(T* pDst, uint32_t nDst, Word radix) const
		{
			auto sSrc = get_ConstSlice();
			sSrc.Trim();

			Word pBuf[nWords];
			Slice sDst{ pBuf, sSrc.m_n };
			sDst.Copy(sSrc);

			sDst.DecomposeDestroy(pDst, nDst, radix);
		}

		template <Word radix, typename T>
		void DecomposeEx(T* pDst, uint32_t nDst) const
		{
			static_assert(radix > 1, "");

			const uint32_t nPwr = NumericUtils::LogOf<1ull << nWordBits, radix>::N;
			static_assert(nPwr >= 1, "");

			if constexpr (nPwr == 1)
				Decompose(pDst, nDst, radix); // already saturated
			else
			{
				// raise the radix to power. Factorize according to it, would be less divisions
				const Word radixMax = static_cast<Word>(NumericUtils::PowerOf<radix, nPwr>::N);
				const uint32_t nLen = get_Decomposed_MaxLen<radixMax>();

				Number<nLen> buf;
				Decompose(buf.m_p, buf.nWords, radixMax);

				Slice::DecomposeSplitPwr(pDst, nDst, radix, nPwr, buf.get_ConstSlice());
			}
		}

		template <typename T>
		void Compose(const T* pSrc, uint32_t nSrc, Word radix)
		{
			Word pBuf[nWords];
			get_Slice().Compose(pSrc, nSrc, radix, pBuf);
		}

		template <Word radix, typename T>
		void ComposeEx(const T* pSrc, uint32_t nSrc)
		{
			static_assert(radix > 1, "");

			const uint32_t nPwr = NumericUtils::LogOf<1ull << nWordBits, radix>::N;
			static_assert(nPwr >= 1, "");

			Word pBuf[nWords];

			if constexpr (nPwr == 1)
				get_Slice().Compose(pSrc, nSrc, radix, pBuf); // already saturated
			else
			{
				// raise the radix to power. Factorize according to it, would be less divisions
				const Word radixMax = static_cast<Word>(NumericUtils::PowerOf<radix, nPwr>::N);
				get_Slice().ComposeEx(pSrc, nSrc, radix, pBuf, radixMax, nPwr);
			}
		}

		static const uint32_t s_TextLen10 = get_Decomposed_MaxLen<10>();
	};

	template <uint32_t nBytes>
	struct NumberForSize {
		typedef Number<(nBytes + sizeof(Word) - 1) / sizeof(Word)> Type;
	};

	template <typename T>
	struct NumberForType {
		typedef typename NumberForSize<sizeof(T)>::Type Type;
	};


	template <typename T>
	inline typename NumberForType<T>::Type From(T x)
	{
		return NumberForType<T>::Type(x);
	}

} // namespace MultiWord
} // namespace beam
