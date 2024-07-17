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

template <typename T>
struct TypeTraits
{
	static const uint32_t Bits = sizeof(T) * 8;
	static const bool IsSigned = static_cast<T>(-1) < static_cast<T>(0);

	static const T Min = IsSigned ? (static_cast<T>(1) << (Bits - 1)) : 0;
	static const T Max = ~Min;
	static_assert(Min < Max, "");
};

namespace BitUtils
{
	namespace details
	{
		template <uint32_t nBits>
		struct PerBits {

			template <typename T>
			static uint32_t clz(T x)
			{
				static_assert(!TypeTraits<T>::IsSigned, "");

				const uint32_t nHalf = nBits / 2;
				if constexpr (nHalf > 0)
				{
					auto x1 = static_cast<uint32_t>(x >> nHalf);
					if (x1)
						return PerBits<nHalf>::clz(x1);

					return nHalf + PerBits<nHalf>::clz(static_cast<uint32_t>(x));
				}
				else
				{
					assert(x <= 1);
					return !x;
				}
			}
		};
	}

	template <typename T>
	inline uint32_t clz(T x)
	{
		return details::PerBits<TypeTraits<T>::Bits>::clz(x);
	}

}

namespace Power
{
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
	T Raise(uint32_t n)
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



	template <uint32_t nWords_>
	struct UInt;

	template <> struct UInt<0>
	{
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

		template <uint32_t nShiftWords, typename T>
		T get_Ord() const
		{
			return 0;
		}

		template <uint32_t wa>
		int _Cmp(const UInt<wa>& a) const
		{
			return 0;
		}

		template <uint32_t nShiftWords, uint32_t wa>
		void Assign(const UInt<wa>&)
		{
		}

		template <uint32_t w0, uint32_t w1>
		bool IsZeroRange() const
		{
			return true;
		}
	};


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

		void Fill(Word x)
		{
			m_Val = x;
			if constexpr (nWords > 1)
				Base::Fill(x);
		}

		void Set0()
		{
			Fill(0);
		}

		void SetMax()
		{
			Fill(static_cast<Word>(-1));
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

		template <uint32_t w0, uint32_t w1>
		bool IsZeroRange() const
		{
			if constexpr ((nWords > w0) && (nWords <= w1))
			{
				if (m_Val)
					return false;
			}

			return Base::template IsZeroRange<w0, w1>();
		}

		bool IsZero() const
		{
			return IsZeroRange<0, nWords>();
		}

		void operator = (uint64_t x)
		{
			set_Ord<0>(x);
		}

		template <uint32_t wa>
		void operator = (const UInt<wa>& a)
		{
			Assign<0>(a);
		}

		template <uint32_t nShiftWords, uint32_t wa>
		void Assign(const UInt<wa>& a)
		{
			m_Val = a.template get_Val<nWords - nShiftWords>();
			Base::template Assign<nShiftWords>(a);
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

		template <uint32_t nShiftWords, typename T>
		void Get(T& val) const
		{
			val = get_Ord<nShiftWords, T>();
		}

		template <uint32_t nShiftWords, typename T>
		T Get() const
		{
			return get_Ord<nShiftWords, T>();
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
			// *this = a - b
			DWordSigned carry = Base::SetSub(a, b);
			carry += a.template get_Val<nWords>();
			carry -= b.template get_Val<nWords>();

			m_Val = (Word)carry;
			return carry >> nWordBits;
		}


		template <uint32_t wa, uint32_t wb>
		void SetDiv(const UInt<wa>& a, const UInt<wb>& __restrict__ b)
		{
			UInt<wa> resid = a;
			SetDivResid(resid, b);
		}

		template <uint32_t wa, uint32_t wb>
		void SetDivResid(UInt<wa>& __restrict__ resid, const UInt<wb>& __restrict__ b)
		{
			if constexpr (wb == 0)
				SetMax(); // div by 0
			else
			{
				Word val = b.template get_Val<wb>();
				if (val)
				{
					/*
					if constexpr (wb == 1)
					{
						SetDivResidSimple(resid, val);
						return;
					}
					*/

					uint32_t nz = BitUtils::clz(val);
					assert(nz < nWordBits);

					if (!nz)
						SetDivResidNormalized(resid, b);
					else
					{
						UInt<wb> b2;
						b2.Set_LShift(nz, b);

						UInt<wa + 1> r2;
						r2.Set_LShift(nz, resid);

						SetDivResidNormalized(r2, b2);

						resid.Set_RShift(nz, r2);
					}
				}
				else
					SetDivResid<wa, wb - 1>(resid, b);
			}
		}

		template <uint32_t wb>
		UInt<nWords> operator / (const UInt<wb>& __restrict__ b) const
		{
			UInt<nWords> ret;
			ret.SetDiv(*this, b);
			return ret;
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
			m_Val = (Word) val;

			if constexpr (sizeof(val) <= sizeof(Word))
				return val;

			const uint32_t nShift = (sizeof(val) <= sizeof(Word)) ? 0 : nWordBits; // it's always nWordBits, just a workaround for compiler error
			return val >> nShift;
		}

		template <uint32_t nShiftWords, typename T>
		T get_Ord() const
		{
			T res = Base::template get_Ord<nShiftWords, T>();

			constexpr uint32_t nLShift = (uint32_t) (nWords - nShiftWords - 1); // may overflow, it's ok

			if constexpr (nLShift < sizeof(T) / sizeof(Word))
				res |= ((T) m_Val) << (nLShift * nWordBits);

			return res;
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

		template <uint32_t wa>
		void Set_RShift(uint32_t nBits, const UInt<wa>& __restrict__ a)
		{
			if constexpr (wa > nWords + 1)
				Set_RShift<wa - 1>(nBits, a);
			else
			{
				assert(nBits && nBits < nWordBits);
				m_Val = (a.template get_Val<nWords>() >> nBits) | (a.template get_Val<nWords + 1>() << (nWordBits - nBits));

				if constexpr (nWords > 1)
					Base::Set_RShift(nBits, a);

			}
		}

		template <uint32_t wa>
		void Set_LShift(uint32_t nBits, const UInt<wa>& __restrict__ a)
		{
			if constexpr (wa > nWords)
				Set_LShift<wa - 1>(nBits, a);
			else
			{
				assert(nBits && nBits < nWordBits);
				m_Val = (a.template get_Val<nWords>() << nBits) | (a.template get_Val<nWords - 1>() >> (nWordBits - nBits));

				if constexpr (nWords > 1)
					Base::Set_LShift(nBits, a);

			}
		}

		friend struct Float;
		friend class FloatEx;

		template <uint32_t wa, uint32_t wd>
		void SetDivResidNormalized(UInt<wa>& __restrict__ resid, const UInt<wd>& __restrict__ div)
		{
			m_Val = resid.template DivOnceNormalized<nWords - 1>(div);
			if constexpr (nWords > 1)
				Base::template SetDivResidNormalized(resid, div);
		}

		template <uint32_t nLShift, uint32_t wd>
		Word DivOnceNormalized(const UInt<wd>& __restrict__ div)
		{
			// div must have msb set

			if constexpr (nWords > wd + nLShift + 1)
				return Base::template DivOnceNormalized<nLShift>(div);
			else
			{
				if constexpr (nWords < wd + nLShift)
					return 0;
				else
				{
					Word hiNom = get_Val<wd + nLShift + 1>();
					Word hiDenom = div.template get_Val<wd>();

					DWord hiPart = static_cast<DWord>(hiNom) << nWordBits;
					hiPart |= get_Val<wd + nLShift>();

					Word res = static_cast<Word>(hiPart / hiDenom);
					if (!res)
					{
						// could be due to overflow
						if (!hiNom)
							return 0;

						assert(hiNom >= hiDenom);
						res = static_cast<Word>(-1);
					}

					auto mul = div * UInt<1>(res);

					auto carry0 = SubShifted<nLShift, wd + 1>(mul);
					if (carry0)
					{
						res--;
						auto carry1 = AddShifted<nLShift, wd + 1>(div);

						if (!carry1)
						{
							res--;
							carry1 = AddShifted<nLShift, wd + 1>(div);

							assert(carry1);
						}
					}

					return res;
				}
			}
		}

		template <uint32_t nLShift, uint32_t iWord, uint32_t wd>
		DWordSigned SubShifted(const UInt<wd>& __restrict__ val)
		{
			if constexpr (iWord > 0)
			{
				if constexpr (wd > iWord)
					return SubShifted<nLShift, iWord, wd - 1>(val);
				else
				{
					DWordSigned carry = SubShifted<nLShift, iWord - 1>(val);
					carry += get_Val<nLShift + iWord>();
					carry -= val.template get_Val<iWord>();

					set_Val<nLShift + iWord>((Word) carry);
					return carry >> nWordBits;
				}
			}
			else
				return 0;
		}

		template <uint32_t nLShift, uint32_t iWord, uint32_t wd>
		DWord AddShifted(const UInt<wd>& __restrict__ val)
		{
			if constexpr (iWord > 0)
			{
				if constexpr (wd > iWord)
					return AddShifted<nLShift, iWord, wd - 1>(val);
				else
				{
					DWord carry = AddShifted<nLShift, iWord - 1>(val);
					if constexpr (nLShift + iWord > nWords)
						return carry; // don't propagate it to the non-existing word. Can happen at the beginning of division
					else
					{
						carry += get_Val<nLShift + iWord>();
						carry += val.template get_Val<iWord>();

						set_Val<nLShift + iWord>((Word)carry);
						return carry >> nWordBits;
					}
				}
			}
			else
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


#pragma pack (push, 1)

    struct Float
    {
        // simple impl. fixed-size mantissa, non-negative, no inf/nan and etc.
        uint64_t m_Num; // msb must be set unless zero
        int32_t m_Order;

        static const uint32_t s_Bits = sizeof(m_Num) * 8;
        static const uint64_t s_HiBit = 1ull << (s_Bits - 1);

        Float() {}
        Float(uint64_t val) { Set(val); }

        void Set0()
        {
            m_Num = 0;
            m_Order = 0;
        }

        void Set(uint64_t val)
        {
            m_Num = val;
            m_Order = 0;
            Normalize();
        }

		static Float get_1()
		{
			Float x;
			x.m_Num = s_HiBit;
			x.m_Order = -(int32_t) (s_Bits - 1);
			return x;
		}

		static Float get_Half()
		{
			Float x;
			x.m_Num = s_HiBit;
			x.m_Order = -(int32_t) s_Bits;
			return x;
		}

		static Float get_1_minus_eps()
		{
			Float x;
			x.m_Num = static_cast<uint64_t>(-1);
			x.m_Order = -(int32_t) s_Bits;
			return x;
		}

		template <typename T>
        bool RoundDown(T& ret) const
        {
			static_assert(sizeof(T) <= sizeof(m_Num));

			constexpr int32_t nOrderMax = static_cast<int32_t>((sizeof(T) * 8) - s_Bits);
			static_assert(nOrderMax <= 0);

			if (m_Order > nOrderMax)
			{
				ret = static_cast<uint64_t>(-1); // overflow/inf
				return false; // overflow
			}

            uint32_t ord = -m_Order;
			if (ord >= s_Bits)
			{
#if BVM_TARGET_HF < 6
				Env::get_Height(); // this is workaround!
#endif
				ret = 0;
			} else
				ret = static_cast<T>(m_Num >> ord);

			return true;
        }

		template <typename T>
		bool Round(T& ret) const {
			return (*this + get_Half()).RoundDown(ret);
		}

		template <typename T>
		bool RoundUp(T& ret) const {
			return (*this + get_1_minus_eps()).RoundDown(ret);
		}

		// legacy functions. To be removed
		uint64_t Get() const
		{
			uint64_t ret;
			RoundDown(ret);
			return ret;
		}

		operator uint64_t () const
		{
			return Get();
		}

        bool IsZero() const
        {
            return !m_Num;
        }

		bool IsNormalizedNnz() const
		{
			return !!(s_HiBit & m_Num);
		}

		bool IsOrderWithin(int32_t nMin, int32_t nMax) const
		{
			uint32_t d = m_Order - nMin + s_Bits;
			uint32_t r = nMax - nMin;
			return (d <= r);
		}

        void Normalize()
        {
            // call explicitly only if manipulating directly
            auto nz = BitUtils::clz(m_Num);
            if (nz)
            {
                if (nz != s_Bits)
                {
                    m_Num <<= nz;
                    m_Order -= nz;
                }
                else
                    Set0();
            }
        }

        Float operator + (Float b) const
        {
            if (IsZero())
                return b;
            if (b.IsZero())
                return *this;

            return (m_Order >= b.m_Order) ? AddInternal(b) : b.AddInternal(*this);
        }

        Float operator - (Float b) const
        {
            if (b.IsZero())
                return *this;

            assert(!IsZero());
            assert(m_Order >= b.m_Order);
            uint32_t dOrder = m_Order - b.m_Order;

            if (dOrder >= s_Bits)
                return *this;

            Float res;
            res.m_Num = m_Num - (b.m_Num >> dOrder);
            res.m_Order = m_Order;

            res.Normalize();
            return res;
        }

        Float operator * (Float b) const
        {
            if (IsZero())
                return *this;
            if (b.IsZero())
                return b;

            // both must be normalized
            auto x = MultiPrecision::From(m_Num) * MultiPrecision::From(b.m_Num);

            // the result may loose at most 1 msb
            Float res;
            res.m_Order = m_Order + b.m_Order + s_Bits;
            x.Get<2>(res.m_Num);

            if (!(s_HiBit & res.m_Num))
            {
                res.m_Num = (res.m_Num << 1) | (x.get_Val<2>() >> (MultiPrecision::nWordBits - 1));
                res.m_Order--;
            }

            return res;
        }

        Float operator / (Float b) const
        {
            if (IsZero())
                return *this;
            if (b.IsZero())
                return b; // actually the result should be inf, but nevermind

            Float res;
            res.m_Order = m_Order - b.m_Order - s_Bits;

            // since both operands are normalized, the result must be within (1/2 .. 2)
            if (m_Num >= b.m_Num)
            {
                res.m_Num = DivInternal(m_Num - b.m_Num, b.m_Num);
                res.AddMsb();
            }
            else
            {
                res.m_Num = DivInternal(m_Num, b.m_Num);
                assert(s_HiBit & res.m_Num);
            }

            return res;
        }

        Float operator << (int32_t n)
        {
            if (IsZero())
                return *this;

            Float res;
            res.m_Num = m_Num;
            res.m_Order = m_Order + n;
            return res;
        }

        Float operator >> (int32_t n)
        {
            if (IsZero())
                return *this;

            Float res;
            res.m_Num = m_Num;
            res.m_Order = m_Order - n;
            return res;
        }

		Float& operator += (Float b) {
			return *this = *this + b;
		}

		Float& operator -= (Float b) {
			return *this = *this - b;
		}

		Float& operator *= (Float b) {
			return *this = *this * b;
		}

		Float& operator /= (Float b) {
			return *this = *this / b;
		}

		Float& operator <<= (int32_t n) {
			return *this = *this << n;
		}

		Float& operator >>= (int32_t n) {
			return *this = *this >> n;
		}

		int cmp(const Float& x) const
		{
			if (IsZero())
				return x.IsZero() ? 0 : -1;
			if (x.IsZero())
				return 1;

			if (m_Order < x.m_Order)
				return -1;
			if (m_Order > x.m_Order)
				return 1;

			if (m_Num < x.m_Num)
				return -1;
			if (m_Num > x.m_Num)
				return 1;

			return 0;
		}

		bool operator < (const Float& x) const { return cmp(x) < 0; }
		bool operator > (const Float& x) const { return cmp(x) > 0; }
		bool operator <= (const Float& x) const { return cmp(x) <= 0; }
		bool operator >= (const Float& x) const { return cmp(x) >= 0; }
		bool operator == (const Float& x) const { return cmp(x) == 0; }
		bool operator != (const Float& x) const { return cmp(x) != 0; }


		struct DecimalForm
		{
			uint64_t m_Num; // not necessarily normalized
			int32_t m_Order;

			void Assign(Float x)
			{
				m_Order = 0;

				if (x.IsZero())
					m_Num = 0;
				else
				{
					// multiply or divide by power of 10, to bring the binary order to the range [-3, 0]
					while (true)
					{
						if (x.m_Order > 0)
						{
							Context ctx;
							ctx.m_BinaryThreshold = x.m_Order + 3; // won't overflow, unsigned res will be correct
							ctx.Calculate();
							x /= ctx.Export();
							m_Order += ctx.m_DecimalPwr;
						}
						else
						{
							if (x.m_Order >= -3)
								break;

							Context ctx;
							ctx.m_BinaryThreshold = -x.m_Order; // can't overflow
							ctx.Calculate();
							x *= ctx.Export();
							m_Order -= ctx.m_DecimalPwr;
						}
					}

					x.Round(m_Num);
				}
			}

		private:


			struct Context
			{
				static uint32_t get_Ord(const Float& x)
				{
					return x.m_Order + Float::s_Bits - 1;
				}

				uint64_t m_Num;
				uint32_t m_Ord = 0;
				uint32_t m_DecimalPwr;
				uint32_t m_BinaryThreshold;

				Float Export() const
				{
					Float x;
					x.m_Num = m_Num;
					x.m_Order = m_Ord + 1 - Float::s_Bits;
					return x;
				}

				bool TryMul(const Float& __restrict__ x, uint32_t nDecimalPwr)
				{
					uint32_t nOrd = get_Ord(x);
					assert(nOrd);
					assert(nOrd < m_BinaryThreshold); // shouldn't try excessive values

					if (m_Ord)
					{
						uint32_t nOrdNext = nOrd + m_Ord;
						if (nOrdNext >= m_BinaryThreshold)
							return false;

						Float res = Export() * x;
						nOrdNext = get_Ord(res);
						if (nOrdNext >= m_BinaryThreshold)
							return false;

						if (nOrdNext < m_Ord)
							return false; // overflow

						// ok
						m_Num = res.m_Num;
						m_Ord = nOrdNext;
						m_DecimalPwr += nDecimalPwr;
					}
					else
					{
						m_Num = x.m_Num;
						m_Ord = nOrd;
						m_DecimalPwr = nDecimalPwr;
					}

					return true;
				}

				void ExpReduce()
				{
					assert((m_Ord > 0) && (m_Ord < m_BinaryThreshold));

					Float cur = Export();
					uint32_t nDecimal = m_DecimalPwr;

					if (!TryMul(cur, nDecimal))
						return;

					ExpReduce(); // recursive

					TryMul(cur, nDecimal);
				}

				bool TryMul2(uint32_t nPwr2)
				{
					assert(nPwr2 < s_Bits);
					assert(nPwr2 > 3);

					uint64_t val;
					uint32_t nPwr10 = Power::Log<uint64_t, 10>(val, 1ull << nPwr2);

					return TryMul(Float(val), nPwr10);
				}

				void TryMulStrict(uint32_t nPwr2)
				{
					bool b = TryMul2(nPwr2);
					assert(b);
				}

				void Calculate()
				{
					// Calculate 10^d <= 2^nBinaryOrder
					assert(m_BinaryThreshold > 3);

					const uint32_t nBinMax = s_Bits - 1;
					if (m_BinaryThreshold <= nBinMax)
					{
						TryMulStrict(m_BinaryThreshold);
						return;
					}

					const uint32_t nPwr10 = Power::LogOf<(uint64_t)-1, 10>::N;
					const uint64_t base = Power::PowerOf<10, nPwr10>::N;
					static_assert(Power::LogOf<base, 2>::N <= nBinMax);

					bool b = TryMul(Float(base), nPwr10);
					assert(b);
					ExpReduce();

					uint32_t nPwr2 = m_BinaryThreshold - m_Ord;
					assert(nPwr2 <= nBinMax + 1); // m_Ord is rounded down, nPwr2 is rounded up

					if (nPwr2 <= 3)
						return;

					if ((nPwr2 <= nBinMax) && TryMul2(nPwr2))
						return;

					--nPwr2;
					if (nPwr2 <= 3)
						return;

					TryMulStrict(nPwr2);
				}
			};

		};


        // text formatting tools
        struct Text
        {
            static const uint32_t s_LenMantissaMax = Utils::String::Decimal::DigitsMax<uint64_t>::N - 2;
            static const uint32_t s_LenOrderMax = Utils::String::Decimal::DigitsMax<uint32_t>::N;

            static const uint32_t s_LenMax = s_LenMantissaMax + s_LenOrderMax + 5; // dot, space, E, space, sign. Excluding 0-term.
        };

        uint32_t Print(char* sz, bool bTryStdNotation = true, uint32_t nDigits = Text::s_LenMantissaMax) const
        {
			uint32_t ret = 0;

			if (IsZero())
                sz[ret++] = '0';
			else
			{
				DecimalForm df;
				df.Assign(*this);

				uint64_t nTrg;
				uint32_t nPwr = Power::Log<uint64_t, 10>(nTrg, df.m_Num);
				assert(df.m_Num >= nTrg);
				df.m_Order += nPwr;

				if (!nDigits)
					nDigits = 1u;
				else
					if (nDigits > Text::s_LenMantissaMax)
						nDigits = Text::s_LenMantissaMax;

				if (nPwr >= nDigits)
				{
					// add rounding
					uint64_t y = Power::Raise<uint64_t, 10>(nPwr - nDigits + 1);
					y >>= 1; // take half

					y += df.m_Num; // can overflow
					if (y > df.m_Num)
					{
						df.m_Num = y;

						if (df.m_Num / nTrg >= 10)
						{
							nTrg *= 10;
							df.m_Order++;
						}
					}
				}

				// Standard is the scientific notation: a.bbbbb E order
				// Check if we can print in the standard notation: aaa.bbbb

				if (bTryStdNotation && (df.m_Order > 0) && (((uint32_t) df.m_Order) <= (nDigits - 1)))
				{
					uint32_t nBeforeDot = df.m_Order + 1;

					nTrg /= Power::Raise<uint64_t, 10>(df.m_Order);

					nDigits -= df.m_Order;
					df.m_Order = 0;

					Utils::String::Decimal::Print(sz + ret, nTrg ? (df.m_Num / nTrg) : 0, nBeforeDot);
					ret += nBeforeDot;

				}
				else
					sz[ret++] = Utils::String::Decimal::ToChar(df.m_Num / nTrg);

				uint32_t pos = ret;
				sz[pos++] = '.';

				while (--nDigits)
				{
					if (!(nTrg /= 10))
						break;

					char ch = Utils::String::Decimal::ToChar(df.m_Num / nTrg);
					sz[pos++] = ch;

					if ('0' != ch)
						ret = pos;
				}

				if (df.m_Order)
				{
					sz[ret++] = ' ';
					sz[ret++] = 'E';
					if (df.m_Order < 0)
					{
						sz[ret++] = '-';
						df.m_Order = -df.m_Order;
					}
					else
						sz[ret++] = '+';

					ret += Utils::String::Decimal::Print(sz + ret, df.m_Order);
				}
			}

			sz[ret] = 0;
			return ret;
        }

	private:

        void AddMsb()
        {
            m_Num = (m_Num >> 1) | s_HiBit;
            m_Order++;
        }

        Float AddInternal(const Float& __restrict__ b) const
        {
            assert(!IsZero() && !b.IsZero() && m_Order >= b.m_Order);
            uint32_t dOrder = m_Order - b.m_Order;

            if (dOrder >= s_Bits)
                return *this;

            Float res;
            res.m_Num = m_Num + (b.m_Num >> dOrder);
            res.m_Order = m_Order;
            if (res.m_Num < m_Num)
                // overflow
                res.AddMsb();

            return res;
        }

        static uint64_t DivInternal(uint64_t a, uint64_t b)
        {
            assert((s_HiBit & b) && (a < b));

            MultiPrecision::UInt<4> nom;
            nom.Set<2>(a);

            MultiPrecision::UInt<2> res;
            res.SetDivResidNormalized(nom, MultiPrecision::From(b));

            return res.Get<0, uint64_t>();
        }
    };


	struct FloatLegacy
		:public Float
	{
		uint32_t m_Dummy;

		void operator = (Float x) {
			Cast::Down<Float>(*this) = x;
		}
	};


    class FloatEx
    {
        uint64_t m_Mantissa;
        int32_t m_Order;
		// value == 1.mantissa_without_hibit << order
		// sign - hi bit (positive)

		static const int32_t s_Zero = TypeTraits<int32_t>::Min;
		static const int32_t s_NaN = TypeTraits<int32_t>::Max;

        static const uint32_t s_Bits = sizeof(m_Mantissa) * 8;
        static const uint64_t s_HiBit = 1ull << (s_Bits - 1);

		template <bool bCareful>
		void NormalizeToPositive()
		{
			assert(IsNumberNnz());

			auto nz = BitUtils::clz(m_Mantissa);
			switch (nz)
			{
			case 0:
				break; // ok

			case s_Bits:
				assert(!m_Mantissa);
				m_Order = s_Zero;
				return;

			default:
				m_Mantissa <<= nz;
				OrderAddInternal<bCareful>(-(signed) nz);
			}
		}

		template <bool bCareful>
		void OrderAddInternal(int32_t d)
		{
			assert(IsNumberNnz());

			int32_t n0 = m_Order;
			m_Order += d;

			if constexpr (bCareful)
			{
				// if order arithmetics reaches Zero or Nan - it's fine. We only need to take care of overflow
				if (d < 0)
				{
					if (m_Order > n0)
						m_Order = s_Zero;
				}
				else
				{
					if (m_Order < n0)
						m_Order = s_NaN;
				}
			}
			else
				assert(IsNumberNnz());
		}

		template <typename T>
		void AssignUnsAsPositive(T val)
		{
			static_assert(sizeof(val) <= sizeof(m_Mantissa), "");

			m_Order = s_Bits - 1;
			m_Mantissa = val;
			NormalizeToPositive<false>();
		}

		bool HaveHiBit() const
		{
			return !!(s_HiBit & m_Mantissa);
		}

		uint64_t get_WithHiBit() const { return s_HiBit | m_Mantissa; }

		void AddOrSub_Ord(const FloatEx& __restrict__ b, bool bAdd, bool bFlip)
		{
			assert(m_Order >= b.m_Order);
			if (IsNaN())
				return;
			assert(!b.IsNaN());

			uint32_t d = m_Order - b.m_Order;
			if ((d < s_Bits) && !b.IsZero())
			{
				assert(IsNumberNnz());

				bool bNeg = !HaveHiBit();
				if (bNeg)
				{
					m_Mantissa |= s_HiBit;
					bFlip = !bFlip;
				}

				auto bVal = b.get_WithHiBit() >> d;

				if (b.HaveHiBit() == bNeg)
					bAdd = !bAdd;

				if (bAdd)
				{
					m_Mantissa += bVal;
					if (m_Mantissa < bVal)
						AddMsb(); // overflow
				}
				else
				{
					if (m_Mantissa >= bVal)
						m_Mantissa -= bVal;
					else
					{
						assert(!d);
						bFlip = !bFlip;
						m_Mantissa = bVal - m_Mantissa;
					}

					NormalizeToPositive<true>();
				}
			}

			if (bFlip)
				Negate();
		}

		void AddOrSub(const FloatEx a, FloatEx b, bool bAdd)
		{
			if (a.m_Order >= b.m_Order)
			{
				*this = a;
				AddOrSub_Ord(b, bAdd, false);
			}
			else
			{
				*this = b;
				AddOrSub_Ord(a, bAdd, !bAdd);
			}
		}

		int cmp_Uns(const FloatEx& x) const
		{
			assert(IsNumberNnz() && x.IsNumberNnz());
			assert(!(s_HiBit & (m_Mantissa ^ x.m_Mantissa)));
			if (m_Order > x.m_Order)
				return 1;
			if (m_Order < x.m_Order)
				return -1;
			if (m_Mantissa > x.m_Mantissa)
				return 1;
			if (m_Mantissa < x.m_Mantissa)
				return -1;

			return 0;
		}

		void AddMsb()
		{
			m_Mantissa = (m_Mantissa >> 1) | s_HiBit;
			OrderAddInternal<true>(1);
		}

		static uint64_t DivInternal(uint64_t a, uint64_t b)
		{
			assert((s_HiBit & b) && (a < b));

			MultiPrecision::UInt<4> nom;
			nom.Set<2>(a);

			MultiPrecision::UInt<2> res;
			res.SetDivResidNormalized(nom, MultiPrecision::From(b));

			return res.Get<0, uint64_t>();
		}

		int get_Class() const
		{
			switch (m_Order)
			{
			case s_Zero:
				return 0;
			case s_NaN:
				return 2;
			}
			return HaveHiBit() ? 1 : -1;
		}

	public:

		FloatEx() { Set0();  }

		template <typename T>
		FloatEx(const T& x) { Assign(x); }

		bool IsNaN() const { return s_NaN == m_Order; }
		bool IsZero() const { return s_Zero == m_Order; }
		bool IsNumber() const { return s_NaN != m_Order; }

		bool IsNumberNnz() const
		{
			return IsNumber() && !IsZero();
		}

		bool IsPositive() const
		{
			return IsNumberNnz() && HaveHiBit();
		}

		bool IsNegative() const
		{
			return IsNumberNnz() && !HaveHiBit();
		}

		uint64_t get_Num() const
		{
			return IsNumberNnz() ? get_WithHiBit() : 0;
		}

		int32_t get_Order() const
		{
			return m_Order;
		}

        void Set0() { m_Order = s_Zero; }
		void SetNaN() { m_Order = s_NaN; }

		void Negate() { m_Mantissa ^= s_HiBit; } // safe even if 0/NaN

		void AddOrder(int32_t n)
		{
			if (IsNumberNnz())
				OrderAddInternal<true>(n);
		}
		void Assign(const FloatEx& x)
		{
			m_Mantissa = x.m_Mantissa;
			m_Order = x.m_Order;
		}

		template <typename T>
		void Assign(T val)
		{
			if constexpr (TypeTraits<T>::IsSigned)
			{
				if (val < 0)
				{
					AssignUnsAsPositive(-val);
					Negate();
					return;
				}
			}
			AssignUnsAsPositive(val);
		}

		template <typename T>
		FloatEx& operator = (const T& x)
		{
			Assign(x);
			return *this;
		}

		static FloatEx get_0()
		{
			FloatEx x;
			x.Set0();
			return x;
		}

		static FloatEx get_NaN()
		{
			FloatEx x;
			x.SetNaN();
			return x;
		}

		static FloatEx get_1()
		{
			FloatEx x;
			x.m_Mantissa = s_HiBit;
			x.m_Order = 0;
			return x;
		}

		static FloatEx get_Half()
		{
			FloatEx x;
			x.m_Mantissa = s_HiBit;
			x.m_Order = -1;
			return x;
		}

		static FloatEx get_1_minus_eps()
		{
			FloatEx x;
			x.m_Mantissa = static_cast<uint64_t>(-1);
			x.m_Order = -1;
			return x;
		}

        template <typename T>
        bool RoundDown(T& ret) const
        {
			if (m_Order < 0)
			{
				ret = 0;
				return true; // also covers Zero
			}

			if (IsNaN())
			{
				ret = 0;
				return false;
			}

            typedef TypeTraits<T> Type;
            static_assert(sizeof(T) <= sizeof(m_Mantissa));

            constexpr int32_t nOrderMax = Type::Bits - !!Type::IsSigned - 1;

			if (HaveHiBit())
			{
				if (m_Order > nOrderMax)
				{
					ret = Type::Max; // overflow/inf
					return false; // overflow
				}
			}
			else
			{
				// negative
				if constexpr (!Type::IsSigned)
				{
					ret = 0;
					return false;
				}
				else
				{
					if (m_Order > nOrderMax)
					{
						ret = Type::Min; // underflow
						return false;
					}
				}
			}

			assert((m_Order > 0) && (m_Order <= nOrderMax));
			uint32_t rs = s_Bits - 1 - m_Order;

			ret = static_cast<T>(get_WithHiBit() >> rs);

            if constexpr (Type::IsSigned)
            {
				if (!HaveHiBit())
                    ret = -ret;
            }

            return true;
        }

		template <typename T>
		bool Round(T& ret) const {
			return (*this + get_Half()).RoundDown(ret);
		}

		template <typename T>
		bool RoundUp(T& ret) const {
			return (*this + get_1_minus_eps()).RoundDown(ret);
		}

        FloatEx operator + (const FloatEx& b) const
        {
			FloatEx res;
			res.AddOrSub(*this, b, true);
            return res;
        }

        FloatEx operator - (const FloatEx& b) const
        {
			FloatEx res;
			res.AddOrSub(*this, b, false);
			return res;
		}

        FloatEx operator * (FloatEx b) const
        {
			if (IsNaN())
				return *this;
			if (!b.IsNumberNnz())
				return b;
			if (IsZero())
				return *this;

            auto x = MultiPrecision::From(get_WithHiBit()) * MultiPrecision::From(b.get_WithHiBit());

            // the result may loose at most 1 msb
            FloatEx res;
			res.m_Order = m_Order;
			res.OrderAddInternal<true>(b.m_Order);
            x.Get<2>(res.m_Mantissa);

			if (res.HaveHiBit())
				res.AddOrder(1);
			else
                res.m_Mantissa = (res.m_Mantissa << 1) | (x.get_Val<2>() >> (MultiPrecision::nWordBits - 1));
				
			res.m_Mantissa ^= ((m_Mantissa ^ b.m_Mantissa) & s_HiBit);

            return res;
        }

        FloatEx operator / (FloatEx b) const
        {
			if (!b.IsNumberNnz())
				return get_NaN();
			if (!IsNumberNnz())
				return *this;

			FloatEx res;
			res.m_Order = m_Order;
			res.OrderAddInternal<true>(-b.m_Order);
			res.AddOrder(-1);

			auto aVal = get_WithHiBit();
			auto bVal = b.get_WithHiBit();
            // since both operands are normalized, the result must be within (1/2 .. 2)
            if (aVal >= bVal)
            {
                res.m_Mantissa = DivInternal(aVal - bVal, bVal);
                res.AddMsb();
            }
            else
            {
                res.m_Mantissa = DivInternal(aVal, bVal);
                assert(s_HiBit & res.m_Mantissa);
            }

			res.m_Mantissa ^= ((m_Mantissa ^ b.m_Mantissa) & s_HiBit);

            return res;
        }

        FloatEx operator << (int32_t n)
        {
			auto res = *this;
			res.AddOrder(n);
            return res;
        }

        FloatEx operator >> (int32_t n)
        {
			auto res = *this;
			res.AddOrder(-n);
			return res;
		}

		FloatEx& operator += (FloatEx b) {
			return *this = *this + b;
		}

		FloatEx& operator -= (FloatEx b) {
			return *this = *this - b;
		}

		FloatEx& operator *= (FloatEx b) {
			return *this = *this * b;
		}

		FloatEx& operator /= (FloatEx b) {
			return *this = *this / b;
		}

		FloatEx& operator <<= (int32_t n) {
			return *this = *this << n;
		}

		FloatEx& operator >>= (int32_t n) {
			return *this = *this >> n;
		}

		int cmp(const FloatEx& x) const
		{
			// negative, zero, positive, NaN
			auto c0 = get_Class();
			auto c1 = x.get_Class();
			if (c0 < c1)
				return -1;
			if (c0 > c1)
				return 1;

			if (!IsNumberNnz())
				return 0;

			int res = cmp_Uns(x);
			return HaveHiBit() ? res : (-res);
		}

		bool operator < (const FloatEx& x) const { return cmp(x) < 0; }
		bool operator > (const FloatEx& x) const { return cmp(x) > 0; }
		bool operator <= (const FloatEx& x) const { return cmp(x) <= 0; }
		bool operator >= (const FloatEx& x) const { return cmp(x) >= 0; }
		bool operator == (const FloatEx& x) const { return cmp(x) == 0; }
		bool operator != (const FloatEx& x) const { return cmp(x) != 0; }

    };


#pragma pack (pop)

	static_assert(sizeof(Float) == 12);
	static_assert(sizeof(FloatEx) == 12);
	static_assert(sizeof(FloatLegacy) == 16);

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
