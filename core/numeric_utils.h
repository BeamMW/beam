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

	// Syntactic sugar!
	enum Zero_ { Zero };

	template <uint32_t nBytes>
	struct uintBig_t;

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
}

namespace MultiWord {

	typedef uint32_t Word;
	typedef uint64_t DWord;
	typedef int64_t DWordSigned;

	static const uint32_t nWordBits = sizeof(Word) * 8;

	template <typename T, typename TCast>
	struct BaseSlice
	{
		T* m_p; // msb at the beginning
		uint32_t m_n;

		T get_Safe(uint32_t i) const
		{
			return (i < m_n) ? m_p[i] : 0;
		}

		TCast get_Head(uint32_t n) const
		{
			assert(n <= m_n);
			return TCast{ m_p, n };
		}

		T* get_TailPtr(uint32_t n) const
		{
			assert(n <= m_n);
			return m_p + m_n - n;
		}

		TCast get_Tail(uint32_t n) const
		{
			return TCast{ get_TailPtr(n), n };
		}

		TCast CutTail(uint32_t n)
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

		uint32_t get_clz_Words() const
		{
			uint32_t ret = 0;
			for (; ret < m_n; ret++)
				if (m_p[ret])
					break;
			return ret;
		}

		uint32_t get_clz_Bits() const
		{
			uint32_t n = get_clz_Words();
			uint32_t ret = n * nWordBits;
			if (n < m_n)
				ret += NumericUtils::clz(m_p[n]);
			return ret;
		}
	};

	struct ConstSlice
		:public BaseSlice<const Word, ConstSlice> {
	};

	struct Slice
		:public BaseSlice<Word, Slice>
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
			AddOrSub<bAdd, true>(b, carry);
		}

		template <bool bAdd, bool bOp0>
		void AddOrSub(ConstSlice b, DWord& carry) const;

		template <bool bAdd, bool bOp0>
		void AddOrSub(DWord& carry) const
		{
			AddOrSub_Internal<bAdd, bOp0, false>(nullptr, carry);
		}

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

		Word SetDivTrim(Word div); // returns resid, trims itself

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

		template <bool bAdd, bool bOp0, bool bOp2>
		void AddOrSub_Internal(const Word* b, DWord& carry) const;

		template <bool bAdd>
		void AddOrSub_Mul_Once(ConstSlice a, Word mul, DWord& carry) const;

		template <bool bAdd, bool bFit>
		void AddOrSubMul_Internal(ConstSlice a, ConstSlice b) const;

		void SetDivResidNormalized(Slice resid, ConstSlice div) const;
	};

	struct Factorization
	{
		// radix convertion, compile-time length calculation. Not the most effective algorithm, but should be ok for compile-time

		static constexpr uint32_t get_MaxLen(Word aRadix, uint32_t aLen, Word bRadix, Word* pW)
		{
			pW[0] = 1;
			uint32_t nW = 1;

			// calculate aRadix^aLen
			for (uint32_t i = 0; i < aLen; i++)
			{
				DWord carry = 0;
				for (uint32_t j = 0; j < nW; j++)
				{
					carry += static_cast<DWord>(pW[j]) * aRadix;
					pW[j] = static_cast<Word>(carry);
					carry >>= nWordBits;
				}
				if (carry)
					pW[nW++] = static_cast<Word>(carry);
			}

			// subtract 1
			for (uint32_t j = 0; ; j++)
				if (pW[j]--)
					break;

			// calculate log, round up (how many times to divide before it turns zero)
			uint32_t retVal = 0;

			while (true)
			{
				Word resid = pW[nW - 1];
				if (resid)
				{
					// divide by new radix
					DWord nom = resid;
					for (uint32_t j = nW - 1; ; )
					{
						Word res = static_cast<Word>(nom / bRadix);
						pW[j] = res;

						if (!j--)
							break;

						res = resid - res * bRadix; // don't care about overflow, we need only last word
						resid = pW[j];

						nom = (static_cast<DWord>(res) << nWordBits) | resid;
					}

					retVal++;
				}
				else
				{
					nW--;
					if (!nW)
						break;
				}

			}

			return retVal;
		}

		template <uint32_t aLen>
		static constexpr uint32_t get_MaxLen(uint32_t aRadix, uint32_t bRadix)
		{
			Word pBuf[aLen + 1] = { 0 }; // zero-init not necessary, but required by constexpr
			return get_MaxLen(aRadix, aLen, bRadix, pBuf);
		}

		template <uint32_t aLen, uint32_t aRadix, uint32_t bRadix>
		struct Decomposed
		{
			static const uint32_t nMaxLen = get_MaxLen<aLen>(aRadix, bRadix);
		};

		template <typename T>
		struct DefaultOut {
			T* m_pB;
			T* m_pE;

			bool IsDone() const { return m_pB == m_pE; }
			void PushBackInternal(T val) { *(--m_pE) = val;  }
			void PushBack(Word w) { PushBackInternal(static_cast<T>(w)); }
			uint32_t get_Reserve() const { return static_cast<uint32_t>(m_pE - m_pB); }

			void MoveHead(uint32_t n)
			{
				static_assert(std::is_integral_v<T>);
				memmove(m_pB, m_pE, sizeof(T) * n);
			}
		};

		template <typename T>
		static DefaultOut<T> MakeDefaultOut(T* p, uint32_t n) {
			return DefaultOut<T>{ p, p + n };
		}

		template <typename T>
		struct DefaultIn
		{
			const T* m_pB;
			const T* m_pE;
			bool PopFront(Word& w)
			{
				if (m_pB == m_pE)
					return false;
				w = *m_pB++;
				return true;
			}
			uint32_t get_Reserve() const { return static_cast<uint32_t>(m_pE - m_pB); }
		};

		template <typename T>
		static DefaultIn<T> MakeDefaultIn(const T* p, uint32_t n) {
			return DefaultIn<T>{ p, p + n };
		}


		template <uint32_t radix>
		struct Printable
		{
			static char To(Word x)
			{
				static_assert(radix <= 0x10);
				assert(x < radix);

				if constexpr (radix > 0xa)
				{
					if (x >= 0xa)
						return static_cast<char>(x + 'a' - 0xa);
				}
				return static_cast<char>(x + '0');
			}

			static bool From(Word& w, char ch)
			{
				static_assert('0' < 'A', "");
				static_assert('A' < 'a', "");

				if constexpr (radix <= 0xa)
					w = ch - '0';
				else
				{
					if (ch < 'A')
						w = ch - '0';
					else
					{
						if (ch < 'a')
							w = ch - 'A' + 0xa;
						else
							w = ch - 'a' + 0xa;
					}
				}
				return (w < radix);
			}
		};

		template <uint32_t radix>
		struct PrintOut
			:public DefaultOut<char>
			,public Printable<radix>
		{
			void PushBack(Word w)
			{
				static_assert(radix <= 0x10, "");
				PushBackInternal(Printable<radix>::To(w));
			}

			uint32_t Finalize(uint32_t len, bool bZTerm)
			{
				uint32_t nTrim = get_Reserve();
				if ((nTrim == len) && len)
				{
					// empty. Emit at least 1 zero character
					PushBack(0);
					nTrim--;
				}

				if (nTrim)
				{
					len -= nTrim;
					MoveHead(len);
				}

				if (bZTerm)
					m_pB[len] = 0;

				return len;
			}

		};

		template <uint32_t radix>
		static PrintOut<radix> MakePrintOut(char* p, uint32_t n) {
			return PrintOut<radix>{ p, p + n };
		}

		template <uint32_t radix>
		struct ScanIn
			:public DefaultIn<char>
			,public Printable<radix>
		{
			bool PopFront(Word& w)
			{
				if (m_pB == m_pE)
					return false;

				if (!Printable<radix>::From(w, *m_pB))
					return false;
				
				*m_pB++;
				return true;
			}
		};

		template <uint32_t radix>
		static ScanIn<radix> MakeScanIn(const char* p, uint32_t n) {
			return ScanIn<radix>{ p, p + n };
		}

		struct Decomposer
		{
			Slice m_s;
			bool m_FillPadding = true;

			template <typename TOut>
			void Process(TOut&& out, Word radix)
			{
				assert(radix > 1);
				m_s.Trim();
				Process_N(out, radix);
			}

			template <Word radix, typename TOut>
			void Process_T(TOut&& out)
			{
				static_assert(radix > 1, "");
				m_s.Trim();

				const uint32_t nPwr = NumericUtils::LogOf<static_cast<Word>(-1), radix>::N;
				static_assert(nPwr >= 1, "");

				if constexpr (nPwr == 1)
					Process_N(out, radix); // already saturated
				else
				{
					// raise the radix to power. Factorize according to it, would be less divisions
					const Word radixBig = static_cast<Word>(NumericUtils::PowerOf<radix, nPwr>::N);
					Process_N_Big(out, radix, radixBig, nPwr);
				}
			}

		private:

			template <typename TOut>
			void Process_N_Big(TOut&& out, Word radix, Word radixBig, uint32_t nPwr)
			{
				while (m_s.m_n > 1)
				{
					if (out.IsDone())
						return;

					Word w = m_s.SetDivTrim(radixBig);

					for (uint32_t nPortion = nPwr; --nPortion; )
					{
						PushMod(out, w, radix);
						if (out.IsDone())
							return;
					}

					out.PushBack(w);

				}

				Process_1(out, radix);
			}

			template <typename TOut>
			void Process_N(TOut&& out, Word radix)
			{
				while (m_s.m_n > 1)
				{
					if (out.IsDone())
						return;
					out.PushBack(m_s.SetDivTrim(radix));
				}

				Process_1(out, radix);
			}

			template <typename TOut>
			void PushMod(TOut&& out, Word& w, Word radix)
			{
				out.PushBack(w % radix);
				w /= radix;
			}

			template <typename TOut>
			void Process_1(TOut&& out, Word radix)
			{
				assert(m_s.m_n <= 1);
				if (m_s.m_n)
				{
					for (Word w = m_s.m_p[0]; ; )
					{
						if (out.IsDone())
							return;

						assert(w);
						if (w < radix)
						{
							out.PushBack(w);
							break;
						}

						PushMod(out, w, radix);
					}
				}

				if (m_FillPadding)
				{
					while (!out.IsDone())
						out.PushBack(static_cast<Word>(0));
				}
			}

		};

		struct Composer
		{
			Slice m_sRes;
			uint32_t m_Len;

			void Init()
			{
				m_sRes.Set0();
				m_Len = m_sRes.m_n;
				m_sRes.m_p += m_sRes.m_n;
				m_sRes.m_n = 0;
			}

			template <typename TIn>
			void Process(TIn&& in, Word radix)
			{
				assert(radix > 1);

				while (true)
				{
					Word w;
					if (!in.PopFront(w))
						break;

					SelfMul(radix);
					SelfAdd(w);
				}
			}

			template <Word radix, typename TIn>
			void Process_T(TIn&& in)
			{
				static_assert(radix > 1, "");

				const uint32_t nPwr = NumericUtils::LogOf<static_cast<Word>(-1), radix>::N;
				static_assert(nPwr >= 1, "");

				if constexpr (nPwr == 1)
					Process(pSrc, radix); // already saturated
				else
				{
					// raise the radix to power. Factorize according to it, would be less divisions
					const Word radixBig = static_cast<Word>(NumericUtils::PowerOf<radix, nPwr>::N);

					while (true)
					{
						Word w;
						if (!in.PopFront(w))
							return;

						uint32_t nNaggle = 1;
						while (true)
						{
							Word w2;
							if (!in.PopFront(w2))
							{
								// partial naggle
								// use correct radix
								Word radix2 = radix;
								while (--nNaggle)
									radix2 *= radix;

								SelfMul(radix2);
								SelfAdd(w);
								return;
							}

							w = w * radix + w2;
							if (++nNaggle == nPwr)
								break;
						}

						SelfMul(radixBig);
						SelfAdd(w);
					}
				}
			}
		private:

			void SelfAdd(Word);
			void SelfMul(Word);
			void HandleCarry(DWord);
		};

	};



	template <uint32_t nWords_>
	struct Number
	{
		static const uint32_t nWords = nWords_;
		static const uint32_t nSize = sizeof(Word) * nWords;
		static const uint32_t nBits = nSize * 8;

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

		Number& operator = (Zero_)
		{
			get_Slice().Set0();
			return *this;
		}

		Number& operator = (Word x)
		{
			set_Element(x);

			if constexpr (nWords > 1)
				get_Slice().get_Head(nWords - 1).Set0();

			return *this;
		}

		Number& operator = (DWord x)
		{
			set_Element(x);

			if constexpr (nWords > 2)
				get_Slice().get_Head(nWords - 2).Set0();

			return *this;
		}

		struct Neg { ConstSlice m_Arg; };

		Neg operator - () const
		{
			return Neg{ get_ConstSlice() };
		}

		Number& operator = (const Neg& neg)
		{
			DWord carry = 0;
			get_Slice().AddOrSub<false, false>(neg.m_Arg, carry);
			return *this;
		}

		Word get_Msb() const
		{
			static_assert(nWords > 0);
			return m_p[0] >> (nWordBits - 1);
		}

		template <uint32_t i = 0>
		void get_Element(Word& x) const
		{
			static_assert(nWords >= i + 1);
			x = m_p[nWords - (i + 1)];
		}

		template <uint32_t i = 0>
		void get_Element(DWord& x) const
		{
			const uint32_t i2 = i * sizeof(DWord) / sizeof(Word);
			static_assert(nWords >= (i2 + 2));
			x = (static_cast<DWord>(m_p[nWords - (i2 + 2)]) << nWordBits) | m_p[nWords - (i2 + 1)];
		}

		template <typename T, uint32_t i = 0>
		T get_Element() const
		{
			T x;
			get_Element<i>(x);
			return x;
		}

		template <uint32_t i = 0>
		void set_Element(Word x)
		{
			static_assert(nWords >= i + 1);
			m_p[nWords - (i + 1)] = x;
		}

		template <uint32_t i = 0>
		void set_Element(DWord x)
		{
			const uint32_t i2 = i * sizeof(DWord) / sizeof(Word);
			static_assert(nWords >= (i2 + 2));
			m_p[nWords - (i2 + 1)] = static_cast<Word>(x);
			m_p[nWords - (i2 + 2)] = static_cast<Word>(x >> nWordBits);
		}

		template <uint32_t wa>
		Number& operator = (const Number<wa>& a)
		{
			get_Slice().Copy(a.get_ConstSlice());
			return *this;
		}

		template <uint32_t nBytes>
		Number& operator = (const uintBig_t<nBytes>& a)
		{
			a.ToNumber(*this);
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

		bool operator == (Zero_) const { return get_ConstSlice().IsZero(); }
		bool operator != (Zero_) const { return !get_ConstSlice().IsZero(); }

		template <uint32_t wa>
		Number& operator += (const Number<wa>& a)
		{
			DWord carry = 0;
			get_Slice().AddOrSub<true, true>(a.get_ConstSlice(), carry);
			return *this;
		}

		template <uint32_t wa>
		Number& operator -= (const Number<wa>& a)
		{
			DWord carry = 0;
			get_Slice().AddOrSub<false, true>(a.get_ConstSlice(), carry);
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
		Number operator - (const Number<wa>& a) const
		{
			Number ret(*this);
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

		static constexpr uint32_t get_Decomposed_MaxLen(Word radix)
		{
			return Factorization::get_MaxLen<nWords * 2>(1u << (nWordBits / 2), radix);
		}

		template <uint32_t radix>
		struct Decomposed {
			static const uint32_t nMaxLen = get_Decomposed_MaxLen(radix);
		};

		struct DecomposeCtx
			:public Factorization::Decomposer
		{
			Word m_pBuf[nWords];
			DecomposeCtx(const Number& x)
			{
				auto sSrc = x.get_ConstSlice();
				sSrc.Trim();

				m_s.m_p = m_pBuf;
				m_s.m_n = sSrc.m_n;

				m_s.Copy(sSrc); // copy only nnz part
			}
		};

		template <typename TOut>
		void Decompose(TOut&& out, Word radix, bool bTrim = true) const
		{
			DecomposeCtx ctx(*this);
			ctx.m_FillPadding = !bTrim;
			ctx.Process(out, radix);
		}

		template <Word radix, typename TOut>
		void DecomposeEx(TOut&& out, bool bTrim = true) const
		{
			DecomposeCtx ctx(*this);
			ctx.m_FillPadding = !bTrim;
			ctx.Process_T<radix>(out);
		}

		struct ComposeCtx
			:public Factorization::Composer
		{
			ComposeCtx(Number& x)
			{
				m_sRes = x.get_Slice();
				Init();
			}
		};


		template <typename TIn>
		void Compose(TIn&& in, Word radix)
		{
			ComposeCtx ctx(*this);
			ctx.Process(in, radix);
		}

		template <Word radix, typename TIn>
		void ComposeEx(TIn&& in)
		{
			ComposeCtx ctx(*this);
			ctx.Process_T<radix>(in);
		}

		template <uint32_t radix>
		uint32_t Print(char* sz, uint32_t len = Decomposed<radix>::nMaxLen, bool bTrim = true, bool bZTerm = true) const
		{
			auto out = Factorization::MakePrintOut<radix>(sz, len);
			DecomposeEx<radix>(out, bTrim);

			return out.Finalize(len, bZTerm);
		}

		template <uint32_t radix>
		uint32_t Scan(const char* sz, uint32_t len = Decomposed<radix>::nMaxLen)
		{
			auto in = Factorization::MakeScanIn<radix>(sz, len);
			ComposeEx<radix>(in);
			return static_cast<uint32_t>(in.m_pB - sz);
		}

		static const uint32_t nTxtLen10Max = Decomposed<10>::nMaxLen;

		uint32_t PrintDecimal(char* sz, uint32_t len = nTxtLen10Max, bool bTrim = true, bool bZTerm = true) const
		{
			return Print<10>(sz, len, bTrim, bZTerm);
		}

		uint32_t ScanDecimal(const char* sz, uint32_t len = nTxtLen10Max)
		{
			auto in = Factorization::MakeScanIn<10>(sz, len);
			ComposeEx<10>(in);
			return static_cast<uint32_t>(in.m_pB - sz);
		}
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
