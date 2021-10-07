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

namespace BitUtils
{
	namespace details
	{
		template <uint32_t nBits>
		struct PerBits {

			template <typename T>
			static uint32_t get_BitsUsed(T x)
			{
				const uint32_t nHalf = nBits / 2;
				if constexpr (nHalf > 0)
				{
					T x1 = x >> nHalf;

					uint32_t ret;
					if (x1)
						ret = nHalf;
					else
					{
						ret = 0;
						x1 = x;
					}

					return ret + PerBits<nHalf>::get_BitsUsed(x1);
				}
				else
				{
					assert(x <= 1);
					return static_cast<uint32_t>(x);
				}
			}
		};
	}

	template <typename T>
	inline uint32_t FindHiBit(T x)
	{
		return details::PerBits<sizeof(T) * 8>::get_BitsUsed(x);
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

					uint32_t nBits = BitUtils::FindHiBit(val);
					assert(nBits);

					if (nWordBits == nBits)
						SetDivResidNormalized(resid, b);
					else
					{
						UInt<wb> b2;
						b2.Set_LShift(nWordBits - nBits, b);

						UInt<wa + 1> r2;
						r2.Set_LShift(nWordBits - nBits, resid);

						SetDivResidNormalized(r2, b2);

						resid.Set_RShift(nWordBits - nBits, r2);
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

        uint64_t Get() const
        {
            if (m_Order >= 0)
            {
                uint32_t ord = m_Order;
                if (ord >= s_Bits)
                    return static_cast<uint64_t>(-1); // inf

                return m_Num << ord;
            }
            else
            {
                uint32_t ord = -m_Order;
                if (ord >= s_Bits)
                    return 0;

                return m_Num >> ord;

            }
        }

        operator uint64_t () const {
            return Get();
        }

        bool IsZero() const
        {
            return !m_Num;
        }

		bool operator == (const Float& x) const
		{
			return (m_Num == x.m_Num) && (m_Order == x.m_Order);
		}

        void Normalize()
        {
            // call explicitly only if manipulating directly
            auto nBits = BitUtils::FindHiBit(m_Num);
            if (nBits != s_Bits)
            {
                if (nBits)
                {
                    uint32_t nDelta = s_Bits - nBits;
                    m_Num <<= nDelta;
                    m_Order -= nDelta;
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
