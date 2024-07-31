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
#include "Math.h"

namespace MultiPrecision
{
#pragma pack (push, 1)

	struct FloatBase
	{
		uint64_t m_Num;
		int32_t m_Order;
		// value == m_Num << order

		static const uint32_t s_Bits = sizeof(m_Num) * 8;
		static const uint64_t s_HiBit = 1ull << (s_Bits - 1);

		void set_1()
		{
			m_Num = s_HiBit;
			m_Order = -(int32_t) (s_Bits - 1);
		}

		void set_Half()
		{
			m_Num = s_HiBit;
			m_Order = -(int32_t) s_Bits;
		}

		void set_1_minus_eps()
		{
			m_Num = static_cast<uint64_t>(-1);
			m_Order = -(int32_t) s_Bits;
		}

	protected:

		void Assign(const FloatBase& __restrict__ x)
		{
			m_Num = x.m_Num;
			m_Order = x.m_Order;
		}

		bool NormalizeInternal()
		{
			// shit to restore msb
			auto nz = BitUtils::clz(m_Num);
			switch (nz)
			{
			case 0:
				break; // already normalized

			case s_Bits:
				return false;

			default:
				m_Num <<= nz;
				m_Order -= nz;
			}

			return true;
		}

		void AddMsb()
		{
			m_Num = (m_Num >> 1) | s_HiBit;
			m_Order++;
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

		void SetMul(uint64_t a, uint64_t b)
        {
			assert((s_HiBit & a) && (s_HiBit & b)); // both must be normalized
			
            auto x = MultiPrecision::From(a) * MultiPrecision::From(b);

            // the result may loose at most 1 msb
            x.Get<2>(m_Num);

			if (s_HiBit & m_Num)
				m_Order = s_Bits;
			else
            {
				m_Num = (m_Num << 1) | (x.get_Val<2>() >> (MultiPrecision::nWordBits - 1));
				m_Order = s_Bits - 1;
            }
		}

		void SetDiv(uint64_t a, uint64_t b)
		{
			assert((s_HiBit & a) && (s_HiBit & b));
			m_Order = -(signed) s_Bits;

			// since both operands are normalized, the result must be within (1/2 .. 2)
			if (a >= b)
			{
				m_Num = DivInternal(a - b, b);
				AddMsb();
			}
			else
			{
				m_Num = DivInternal(a, b);
				assert(s_HiBit & m_Num);
			}
		}

		void AddInternal(uint64_t b)
		{
			m_Num += b;
			if (m_Num < b)
				AddMsb(); // overflow
		}

		void AddInternal(uint64_t b, uint32_t dOrder)
		{
			if (dOrder < s_Bits)
				AddInternal(b >> dOrder);
		}

		void SetAdd(const FloatBase& __restrict__ a, const FloatBase& __restrict__ b)
		{
			if (a.m_Order >= b.m_Order)
			{
				Assign(a);
				AddInternal(b.m_Num, a.m_Order - b.m_Order);
			}
			else
			{
				Assign(b);
				AddInternal(a.m_Num, b.m_Order - a.m_Order);
			}
		}

		bool SubInternal(const FloatBase& __restrict__ b)
		{
			assert((s_HiBit & m_Num) && (s_HiBit & b.m_Num));
			assert(m_Order >= b.m_Order);
			uint32_t dOrder = m_Order - b.m_Order;

			if (dOrder >= s_Bits)
				return true;

			m_Num -= (b.m_Num >> dOrder);
			return NormalizeInternal();
		}
	};

    struct Float
		:public FloatBase
    {
        // simple impl. fixed-size mantissa, non-negative, no inf/nan and etc.
        // msb must be set unless zero

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
            NormalizeInternal();
        }

		static Float get_1()
		{
			Float x;
			x.set_1();
			return x;
		}

		static Float get_Half()
		{
			Float x;
			x.set_Half();
			return x;
		}

		static Float get_1_minus_eps()
		{
			Float x;
			x.set_1_minus_eps();
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
				ret = 0;
			else
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

        Float operator + (Float b) const
        {
            if (IsZero())
                return b;
            if (b.IsZero())
                return *this;

			Float res;
			res.SetAdd(*this, b);
            return res;
        }

        Float operator - (Float b) const
        {
			Float res = *this;
			res -= b;
			return res;
        }

        Float operator * (Float b) const
        {
            if (IsZero())
                return *this;
            if (b.IsZero())
                return b;

			Float res;
			res.SetMul(m_Num, b.m_Num);
			res.m_Order += m_Order + b.m_Order;

            return res;
        }

        Float operator / (Float b) const
        {
            if (IsZero())
                return *this;
            if (b.IsZero())
                return b; // actually the result should be inf, but nevermind

            Float res;
			res.SetDiv(m_Num, b.m_Num);
			res.m_Order += m_Order - b.m_Order;

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

		Float& operator -= (Float b)
		{
			if (!b.IsZero())
			{
				assert(!IsZero());
				if (!SubInternal(b))
				{
					assert(!m_Num);
					m_Order = 0; // zero
				}
			}
			return *this;
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
			int32_t m_Order10; // value == m_Num * 10^m_Order10
			uint32_t m_NumDigits;

			enum struct Kind {
				Negative,
				Zero,
				Positive,
				NaN
			};

			Kind m_Kind;

			static const uint32_t s_LenMantissaMax = Utils::String::Decimal::DigitsMax<uint64_t>::N;
			static const uint32_t s_LenOrderMax = Utils::String::Decimal::DigitsMax<uint32_t>::N;

			static const uint32_t s_LenScientificMax = s_LenMantissaMax + s_LenOrderMax + 6; // sign, dot, space, E, space, sign. Excluding 0-term.

			void Assign(Float x)
			{
				m_Num = 0;
				m_Order10 = 0;
				m_NumDigits = 1;

				if (x.IsZero())
				{
					m_Kind = Kind::Zero;
					return;
				}

				m_Kind = Kind::Positive;
				assert(x.IsNormalizedNnz());

				// multiply or divide by power of 10, to bring the binary order to the range [-3, 0]
				while (true)
				{
					if (x.m_Order > 0)
					{
						Context ctx;
						ctx.m_BinaryThreshold = x.m_Order + 3; // won't overflow, unsigned res will be correct
						ctx.Calculate();
						x /= ctx.Export();
						m_Order10 += ctx.m_DecimalPwr;
					}
					else
					{
						if (x.m_Order >= -3)
						{
							if (x.m_Order == -3)
							{
								// try to multiply it by 10. It's slightly more than 2^3, but still may fit
								// x * 10 = ((x << 2) + x) << 1
								uint64_t num = x.m_Num + (x.m_Num >> 2);
								if (num > x.m_Num)
								{
									x.m_Num = num;
									x.m_Order = 0;
									m_Order10--;
								}
							}
							break;
						}

						Context ctx;
						ctx.m_BinaryThreshold = -x.m_Order; // can't overflow
						ctx.Calculate();
						x *= ctx.Export();
						m_Order10 -= ctx.m_DecimalPwr;
					}
				}

				x.Round(m_Num);
				assert(m_Num);

				const uint32_t nPwr10 = Power::LogOf<(uint64_t) -1, 10>::N; // max num of digits
				m_NumDigits = nPwr10 + 1;

				for (uint64_t val = Power::PowerOf<10, nPwr10>::N; m_Num < val; val /= 10)
					m_NumDigits--;

				TrimLowDigits();
			}

			void LimitPrecision(uint32_t numDigits)
			{
				if (!numDigits)
					numDigits = 1;

				if (numDigits >= m_NumDigits)
					return;

				// add half and divide
				uint32_t delta = m_NumDigits - numDigits;

				uint64_t val = Power::Raise<uint64_t, 10>(delta);
				m_Num += (val >> 1);
				bool bOverflow = (m_Num < val);

				uint64_t numBeforeDiv = m_Num;
				m_Num /= val;

				if (bOverflow)
				{
					// results should be (2^64 + m_Num) / val
					m_Num += static_cast<uint64_t>(-1) / val;
					m_Num += ((static_cast<uint64_t>(-1) % val) + 1 + (numBeforeDiv % val)) / val; // at most 1
				}

				m_Order10 += delta;

				// was there a digit leak?
				val = Power::Raise<uint64_t, 10>(numDigits);
				if (m_Num >= val)
				{
					assert(m_Num == val);

					m_Num = 1;
					m_NumDigits = 1;
					m_Order10 += numDigits;
				}
				else
				{
					m_NumDigits = numDigits;
					TrimLowDigits();
				}
			}

			void TrimLowDigits()
			{
				if (!m_Num)
					return;

				while (!(m_Num % 10))
				{
					m_Num /= 10;
					m_NumDigits--;
					m_Order10++;
				}
			}

			struct PrintOptions {
				// can only expand. To reduce num digits use LimitPrecision()
				// If non-negative - the dot will be printed (even if no fractional part)
				int32_t m_DigitsAfterDot;
				bool m_PreferScientific;
				bool m_InsistSign;

				PrintOptions()
					:m_DigitsAfterDot(-1)
					,m_PreferScientific(false)
					,m_InsistSign(false)
				{
				}
			};

			uint32_t get_TextLenStd(const PrintOptions& po = PrintOptions()) const
			{
				Counter ctx;
				PrintStdInternal(ctx, po);
				return ctx.m_Len;
			}

			uint32_t PrintStd(char* sz, const PrintOptions& po = PrintOptions()) const
			{
				Writer ctx;
				ctx.m_szPos = sz;
				PrintStdInternal(ctx, po);
				*ctx.m_szPos = 0;
				return static_cast<uint32_t>(ctx.m_szPos - sz);
			}

			uint32_t PrintScientific(char* sz, const PrintOptions& po = PrintOptions()) const
			{
				Writer ctx;
				ctx.m_szPos = sz;
				PrintScientificInternal(ctx, po);
				*ctx.m_szPos = 0;
				return static_cast<uint32_t>(ctx.m_szPos - sz);
			}

			// attempt to print in standard notation, if fits the buffer.
			uint32_t PrintAuto(char* sz, uint32_t nBufLen = s_LenScientificMax, const PrintOptions& po = PrintOptions()) const
			{
				auto ret = get_TextLenStd(po);
				if (ret <= nBufLen)
				{
					PrintStd(sz, po);
					return ret;
				}

				return PrintScientific(sz, po);
			}

		private:

			struct Writer
			{
				char* m_szPos;
				void Fill0(uint32_t nLen)
				{
#ifdef HOST_BUILD
					memset(m_szPos, '0', nLen);
#else // HOST_BUILD
					Env::Memset(m_szPos, '0', nLen);
#endif // HOST_BUILD
					Move(nLen);
				}

				void Putc(char ch)
				{
					*m_szPos++ = ch;
				}

				void Move(uint32_t nOffs)
				{
					m_szPos += nOffs;
				}

				uint64_t PrintNumEx(uint64_t n, uint32_t nDigits, uint32_t nOffs)
				{
					return Utils::String::Decimal::PrintNoZTerm(m_szPos + nOffs, n, nDigits);
				}


				void PrintNum(uint64_t n, uint32_t nDigits)
				{
					PrintNumEx(n, nDigits, 0);
					Move(nDigits);
				}
			};

			struct Counter
			{
				uint32_t m_Len = 0;
				void Fill0(uint32_t nLen)
				{
					m_Len += nLen;
				}

				void Putc(char ch)
				{
					m_Len++;
				}

				void Move(uint32_t nOffs)
				{
					m_Len += nOffs;
				}

				uint64_t PrintNumEx(uint64_t n, uint32_t nDigits, uint32_t nOffs)
				{
					return n;
				}


				void PrintNum(uint64_t n, uint32_t nDigits)
				{
					m_Len += nDigits;
				}
			};

			template <typename TWriter>
			static void PrintSplit(TWriter& ctx, uint64_t num, uint32_t nBefore, uint32_t nAfter)
			{
				num = ctx.PrintNumEx(num, nAfter, nBefore + 1); // after dot
				ctx.PrintNum(num, nBefore); // before dot
				ctx.Putc('.');
				ctx.Move(nAfter);
			}

			template <typename TWriter>
			static void PrintExtendPrecision(TWriter& ctx, uint32_t nAfterDot, const PrintOptions& po)
			{
				if (po.m_DigitsAfterDot >= 0)
				{
					if (!nAfterDot)
						ctx.Putc('.');

					if (nAfterDot < static_cast<uint32_t>(po.m_DigitsAfterDot))
						ctx.Fill0(po.m_DigitsAfterDot - nAfterDot);
				}
			}

			template <typename TWriter>
			bool PrintPrefix(TWriter& ctx, const PrintOptions& po) const
			{
				switch (m_Kind)
				{
				case Kind::Positive:
					if (po.m_InsistSign)
						ctx.Putc('+');
					break;

				case Kind::Negative:
					ctx.Putc('-');
					break;

				case Kind::Zero:
					break;

				default:
					ctx.Putc('N');
					ctx.Putc('a');
					ctx.Putc('N');
					return false;
				}

				return true;
			}

			template <typename TWriter>
			void PrintStdInternal(TWriter& ctx, const PrintOptions& po) const
			{
				if (!PrintPrefix(ctx, po))
					return;

				int32_t nDelta = m_NumDigits + m_Order10;

				if (nDelta <= 0)
				{
					// 0.[0]num
					ctx.Putc('0');
					ctx.Putc('.');
					ctx.Fill0(-nDelta);
					ctx.PrintNum(m_Num, m_NumDigits);

				}
				else
				{
					if (m_Order10 >= 0)
					{
						// num[0]
						ctx.PrintNum(m_Num, m_NumDigits);
						ctx.Fill0(m_Order10);
					}
					else
						// xx.yyy digits before the dot: nDelta, digits after dot: -m_Order10.
						PrintSplit(ctx, m_Num, nDelta, -m_Order10);
				}

				PrintExtendPrecision(ctx, (m_Order10 >= 0) ? 0 : -m_Order10, po);
			}

			template <typename TWriter>
			void PrintScientificInternal(TWriter& ctx, const PrintOptions& po) const
			{
				if (!PrintPrefix(ctx, po))
					return;

				// print as x.yyy E +/- exp
				uint32_t nAfterDot = m_NumDigits - 1;
				if (nAfterDot)
					PrintSplit(ctx, m_Num, 1, nAfterDot);
				else
					ctx.PrintNum(m_Num, 1);

				PrintExtendPrecision(ctx, nAfterDot, po);

				int32_t ord10 = m_Order10 + nAfterDot;
				if (ord10)
				{
					ctx.Putc(' ');
					ctx.Putc('E');

					if (ord10 < 0)
					{
						ctx.Putc('-');
						ord10 = -ord10;
					}
					else
						ctx.Putc('+');

					uint32_t dummy;
					uint32_t pwr = Power::Log<uint32_t, 10>(dummy, (uint32_t) ord10);
					assert(dummy && (dummy <= (uint32_t) ord10));

					ctx.PrintNum(ord10, pwr + 1);
				}
			}

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
					(b);
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
					(b);
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

		DecimalForm get_Decimal() const
		{
			DecimalForm df;
			df.Assign(*this);
			return df;
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
		:public FloatBase
    {
		// value == (mantissa | hibit) << order
		// sign - hi bit (positive)

		static const int32_t s_Zero = TypeTraits<int32_t>::Min;
		static const int32_t s_NaN = TypeTraits<int32_t>::Max;

		void OrderSetInternal(int64_t n)
		{
			if (n < s_Zero)
				m_Order = s_Zero;
			else
			{
				if (n > s_NaN)
					m_Order = s_NaN;
				else
					m_Order = static_cast<int32_t>(n);
			}
		}

		template <typename T>
		void AssignUnsAsPositive(T val)
		{
			static_assert(sizeof(val) <= sizeof(m_Num), "");

			m_Order = 0;
			m_Num = val;
			if (!NormalizeInternal())
				Set0();
		}

		bool HaveHiBit() const
		{
			return !!(s_HiBit & m_Num);
		}

		uint64_t get_WithHiBit() const { return s_HiBit | m_Num; }

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
					m_Num |= s_HiBit;
					bFlip = !bFlip;
				}

				auto bVal = b.get_WithHiBit() >> d;

				if (b.HaveHiBit() == bNeg)
					bAdd = !bAdd;

				if (bAdd)
					AddInternal(bVal); // overflow. Can bring the number to inf, it's ok
				else
				{
					if (m_Num >= bVal)
						m_Num -= bVal;
					else
					{
						assert(!d);
						bFlip = !bFlip;
						m_Num = bVal - m_Num;
					}

					auto nOrder = m_Order;
					if (!NormalizeInternal() || (m_Order > nOrder)) // test for order overflow. Order shouldn't increase
						Set0();
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
			assert(!(s_HiBit & (m_Num ^ x.m_Num)));
			if (m_Order > x.m_Order)
				return 1;
			if (m_Order < x.m_Order)
				return -1;
			if (m_Num > x.m_Num)
				return 1;
			if (m_Num < x.m_Num)
				return -1;

			return 0;
		}

		template <bool bMul>
		void SetMulOrDiv(const FloatEx& __restrict__ a, const FloatEx& __restrict__ b)
		{
			auto aVal = a.get_WithHiBit();
			auto bVal = b.get_WithHiBit();
			if constexpr (bMul)
				SetMul(aVal, bVal);
			else
				SetDiv(aVal, bVal);

			m_Num ^= ((a.m_Num ^ b.m_Num) & s_HiBit); // sign

			int64_t nOrder = m_Order;
			nOrder += a.m_Order;

			if constexpr (bMul)
				nOrder += b.m_Order;
			else
				nOrder -= b.m_Order;

			OrderSetInternal(nOrder);
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

		void Negate() { m_Num ^= s_HiBit; } // safe even if 0/NaN

		void AddOrder(int32_t n)
		{
			if (IsNumberNnz())
			{
				int64_t nOrder = m_Order;
				nOrder += n;
				OrderSetInternal(nOrder);
			}
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
			x.set_1();
			return x;
		}

		static FloatEx get_Half()
		{
			FloatEx x;
			x.set_Half();
			return x;
		}

		static FloatEx get_1_minus_eps()
		{
			FloatEx x;
			x.set_1_minus_eps();
			return x;
		}

        template <typename T>
        bool RoundDown(T& ret) const
        {
            typedef TypeTraits<T> Type;
            static_assert(sizeof(T) <= sizeof(m_Num));

            constexpr int32_t nOrderMax = Type::Bits - !!Type::IsSigned - s_Bits;
			static_assert(nOrderMax <= 0);

			if (m_Order > nOrderMax)
			{
				// overflow. Also covers NaN/Inf
				ret = (IsNaN() || HaveHiBit()) ? Type::Max : Type::Min;
				return false;
			}

			assert(m_Order <= 0);
			uint32_t rs = -m_Order;

			if (rs >= s_Bits)
				ret = 0;
			else
			{
				if (HaveHiBit())
					ret = static_cast<T>(m_Num >> rs);
				else
				{
					if constexpr (Type::IsSigned)
						ret = -static_cast<T>(get_WithHiBit() >> rs);
					else
					{
						ret = 0;
						return false;
					}
				}
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

			FloatEx res;
			res.SetMulOrDiv<true>(*this, b);
            return res;
        }

        FloatEx operator / (FloatEx b) const
        {
			if (!b.IsNumberNnz())
				return get_NaN();
			if (!IsNumberNnz())
				return *this;

			FloatEx res;
			res.SetMulOrDiv<false>(*this, b);
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

		typedef Float::DecimalForm DecimalForm;

		DecimalForm get_Decimal() const
		{
			DecimalForm df;
			if (IsNaN())
			{
				df.m_Num = 0;
				df.m_Order10 = 0;
				df.m_NumDigits = 1;
				df.m_Kind = DecimalForm::Kind::NaN;
			}
			else
			{
				Float x;
				if (IsZero())
					x.Set0();
				else
				{
					x.m_Num = get_WithHiBit();
					x.m_Order = m_Order;
				}

				df.Assign(x);

				if (IsNegative())
					df.m_Kind = DecimalForm::Kind::Negative;
			}

			return df;
		}

    };


#pragma pack (pop)

	static_assert(sizeof(Float) == 12);
	static_assert(sizeof(FloatEx) == 12);
	static_assert(sizeof(FloatLegacy) == 16);

} // namespace MultiPrecision
