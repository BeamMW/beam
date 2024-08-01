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

#include "numeric_utils.h"

namespace beam {

namespace NumericUtils {

	namespace details
	{
		template <uint32_t nBits>
		struct PerBits {

			template <typename T>
			static uint32_t clz(T x)
			{
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
	uint32_t clz(T x)
	{
		static_assert(std::is_integral_v<T>, "");
		static_assert(!std::is_signed_v<T>, "");
		return details::PerBits<sizeof(T) * 8>::clz(x);
	}

	template uint32_t clz<uint64_t>(uint64_t);
	template uint32_t clz<uint32_t>(uint32_t);
	template uint32_t clz<uint16_t>(uint16_t);
	template uint32_t clz<uint8_t>(uint8_t);

} // namespace NumericUtils


namespace MultiWord {

void Slice::Copy(ConstSlice x) const
{
	// should not overlap
	if (x.m_n >= m_n)
		CopyInternal(x.get_TailPtr(m_n));
	else
	{
		auto h = *this;
		auto t = Cast::Up<Slice>(h.CutTail(x.m_n));
		h.Set0();
		t.CopyInternal(x.m_p);
	}
}

void Slice::CopyInternal(const Word* p) const
{
	for (uint32_t i = 0; i < m_n; i++)
		m_p[i] = p[i];
}

void Slice::LShiftRaw(Word* pDst, const Word* pSrc, uint32_t nLen, uint32_t nShiftL, Word& w)
{
	assert(nShiftL && (nShiftL < nWordBits));
	uint32_t nShiftR = nWordBits - nShiftL;

	while (nLen--)
	{
		Word w1 = pSrc[nLen];
		pDst[nLen] = (w1 << nShiftL) | w;
		w = w1 >> nShiftR;
	}
}

void Slice::RShiftRaw(Word* pDst, const Word* pSrc, uint32_t nLen, uint32_t nShiftR, Word& w)
{
	assert(nShiftR && (nShiftR < nWordBits));
	uint32_t nShiftL = nWordBits - nShiftR;

	for (uint32_t i = 0; i < nLen; i++)
	{
		Word w1 = pSrc[i];
		pDst[i] = w | (w1 >> nShiftR);
		w = w1 << nShiftL;
	}
}

void Slice::LShiftNormalized(uint32_t nShiftL, ConstSlice src) const
{
	if (nShiftL)
		LShiftNormalized_nnz(nShiftL, src);
	else
		Copy(src);
}

void Slice::LShiftNormalized_nnz(uint32_t nShiftL, ConstSlice src) const
{
	Word w = 0;

	if (m_n <= src.m_n)
		LShiftRaw(m_p, src.get_TailPtr(m_n), m_n, nShiftL, w);
	else
	{
		auto x = *this;
		LShiftRaw(x.CutTail(src.m_n).m_p, src.m_p, src.m_n, nShiftL, w);

		assert(x.m_n);
		x.m_p[--x.m_n] = w;

		x.Set0();
	}
}

void Slice::RShiftNormalized(uint32_t nShiftR, ConstSlice src) const
{
	if (nShiftR)
		RShiftNormalized_nnz(nShiftR, src);
	else
		Copy(src);
}

void Slice::RShiftNormalized_nnz(uint32_t nShiftR, ConstSlice src) const
{
	Word w = 0;

	if (m_n <= src.m_n)
	{
		if (m_n < src.m_n)
			w = src.m_p[src.m_n - m_n - 1] << (nWordBits - nShiftR);
		RShiftRaw(m_p, src.get_TailPtr(m_n), m_n, nShiftR, w);
	}
	else
	{
		auto x = *this;
		RShiftRaw(x.CutTail(src.m_n).m_p, src.m_p, src.m_n, nShiftR, w);
		x.Set0();
	}
}

void Slice::LShift(ConstSlice a, uint32_t nBits) const
{
	uint32_t nWords = nBits / nWordBits;
	if (nWords)
	{
		nBits %= nWordBits;

		if (nWords >= m_n)
			Set0();
		else
		{
			auto x = *this;
			Cast::Up<Slice>(x.CutTail(nWords)).Set0();
			x.LShiftNormalized(nBits, a);
		}
	}
	else
		LShiftNormalized(nBits, a);
}

void Slice::RShift(ConstSlice a, uint32_t nBits) const
{
	uint32_t nWords = nBits / nWordBits;
	if (nWords)
	{
		nBits %= nWordBits;

		if (nWords >= a.m_n)
			Set0();
		else
		{
			a.m_n -= nWords;
			RShiftNormalized(nBits, a);
		}

	}
	else
		RShiftNormalized(nBits, a);
}

template <bool bAdd>
void Slice::AddOrSub(ConstSlice b, DWord& carry) const
{
	if (b.m_n >= m_n)
		AddOrSub_Internal<bAdd, true>(b.get_TailPtr(m_n), carry);
	else
	{
		auto x = *this;

		Cast::Up<Slice>(x.CutTail(b.m_n)).AddOrSub_Internal<bAdd, true>(b.m_p, carry);
		x.AddOrSub_Internal<bAdd, false>(nullptr, carry);
	}
}

template void Slice::AddOrSub<true>(ConstSlice b, DWord& carry) const;
template void Slice::AddOrSub<false>(ConstSlice b, DWord& carry) const;

template <bool bAdd>
void Slice::AddOrSub_Mul(ConstSlice a, ConstSlice b) const
{
	if (!m_n)
		return;

	a.Trim();
	b.Trim();

	if (a.m_n < b.m_n)
		std::swap(a, b);

	if (!b.m_n)
		return;

	if (m_n >= a.m_n + b.m_n)
		AddOrSubMul_Internal<bAdd, true>(a, b);
	else
	{
		auto x = *this;

		if (a.m_n > m_n)
		{
			a = a.get_Tail(m_n);

			if (b.m_n > m_n)
				b = b.get_Tail(m_n);
		}
		else
		{
			if (m_n > a.m_n)
			{
				AddOrSubMul_Internal<bAdd, true>(a, b.CutTail(m_n - a.m_n));
				x.m_n = a.m_n;
			}
		}

		x.AddOrSubMul_Internal<bAdd, false>(a, b);
	}
}

template void Slice::AddOrSub_Mul<true>(ConstSlice a, ConstSlice b) const;
template void Slice::AddOrSub_Mul<false>(ConstSlice a, ConstSlice b) const;

template <bool bAdd, bool bOp2>
void Slice::AddOrSub_Internal(const Word* b, DWord& carry) const
{
	for (uint32_t i = m_n; i; )
	{
		if constexpr (!bOp2)
		{
			if (!carry)
				break;
		}

		i--;

		if constexpr (bOp2)
		{
			if constexpr (bAdd)
				carry += b[i];
			else
				carry -= b[i];
		}

		AddToWord(m_p[i], carry);
		MoveCarry<bAdd>(carry);
	}
}

template <bool bAdd>
void Slice::AddOrSub_Mul_Once(ConstSlice a, Word mul, DWord& carry) const
{
	assert(m_n >= a.m_n);
	auto x = *this;
	auto y = x.CutTail(a.m_n);

	for (uint32_t i = a.m_n; i--; )
	{
		auto val = static_cast<DWord>(a.m_p[i]) * mul;
		if constexpr (bAdd)
			carry += val;
		else
			carry -= val;

		AddToWord(y.m_p[i], carry);

		if constexpr (bAdd)
			MoveCarry<true>(carry);
		else
		{
			// do it carefully. Carry may be negative, even though msb may not be set
			// consider it negative if hiword is nnz
			MoveCarry<true>(carry);
			if (carry)
				carry |= static_cast<DWord>(static_cast<Word>(-1)) << nWordBits;
		}
	}

	x.AddOrSub_Internal<bAdd, false>(nullptr, carry);
}

template <bool bAdd, bool bFit>
void Slice::AddOrSubMul_Internal(ConstSlice a, ConstSlice b) const
{
	assert(a.m_n >= b.m_n);
	assert(b.m_n);

	Slice x = *this;
	if constexpr (bFit)
		assert(m_n >= a.m_n + b.m_n);
	else
		assert(m_n == a.m_n);

	while (true)
	{
		DWord carry = 0;
		Word mul = b.m_p[--b.m_n];

		x.AddOrSub_Mul_Once<bAdd>(a, mul, carry);

		if (!b.m_n)
			break;

		x.m_n--;

		if constexpr (!bFit)
		{
			a.m_p++;
			a.m_n--;
		}
	}
}

void Slice::SetDivResid(Slice resid, ConstSlice div, Word* __restrict__ pBufResid, Word* __restrict__ pBufDiv) const
{
	// me = resid / div
	// resid -= me * div (residual)
	//
	// the result is truncated is this obj is too short, yet resid will be correct

	div.Trim();
	if (div.m_n)
	{
		uint32_t nz = NumericUtils::clz(div.m_p[0]);
		assert(nz < nWordBits);

		if (nz)
		{
			Slice div2;
			div2.m_p = pBufDiv;
			div2.m_n = div.m_n;
			div2.LShiftNormalized_nnz(nz, div);


			Slice resid2;
			resid2.m_p = pBufResid;
			resid2.m_n = resid.m_n + 1;
			resid2.LShiftNormalized_nnz(nz, resid.get_Const());

			SetDivResidNormalized(resid2, div2.get_Const());

			resid.RShiftNormalized_nnz(nz, resid2.get_Const());
		}
		else
			SetDivResidNormalized(resid, div);
	}
	else
		SetMax();
}

void Slice::SetDivResidNormalized(Slice resid, ConstSlice div) const
{
	// divisor is normalized, i.e. msb is set
	if (resid.m_n < div.m_n)
		Set0();
	else
	{
		uint32_t nResMax = resid.m_n - div.m_n + 1;

		if (m_n > nResMax)
			Cast::Up<Slice>(get_Head(m_n - nResMax)).Set0();

		Word hiDenom = div.m_p[0];
		Word hiNom = 0;
		DWord nom = resid.m_p[0];

		resid.m_n = div.m_n;

		for (uint32_t i = 0; ; )
		{
			// init-guess
			Word res = static_cast<Word>(nom / hiDenom);
			//
			// the actual result is within: Nom/(hiDenom+1) < guess < (Nom+1)/hiDenom
			//
			// since denom is normalized (msb is set), the result is actually limited within: (guess-2) <= res <= guess

			if (!res)
			{
				// could be overflow
				if (hiNom)
				{
					assert(hiNom == hiDenom);
					res = static_cast<Word>(-1);
				}
			}

			if (res)
			{
				// multiply & subtract
				DWord carry = 0;
				resid.AddOrSub_Mul_Once<false>(div, res, carry);

				if (carry)
				{
					// overflow
					res--;
					carry = 0;
					resid.AddOrSub<true>(div, carry);

					if (!carry)
					{
						// overflow (back) wasn't reached. Do it again
						res--;
						carry = 0;
						resid.AddOrSub<true>(div, carry);

						assert(carry);
					}
				}

			}

			// store result
			if (m_n + i >= nResMax)
				m_p[m_n + i - nResMax] = res;

			// advance
			if (i)
			{
				assert(!resid.m_p[0]);
				resid.m_p++;
			}
			else
				resid.m_n++;

			if (++i == nResMax)
				break;

			hiNom = resid.m_p[0];
			nom = (static_cast<DWord>(hiNom) << nWordBits) | resid.m_p[1];
		}

	}
}

DWord Slice::Mul(Word w) const
{
	DWord carry = 0;
	for (uint32_t i = m_n; i--; )
	{
		auto& x = m_p[i];
		carry += static_cast<DWord>(x) * w;

		x = static_cast<Word>(carry);
		MoveCarry<true>(carry);
	}
	return carry;
}

void MyMul(Slice& sRes, Slice& sTmp, uint32_t nMaxLen, ConstSlice a, ConstSlice b)
{
	uint32_t nMax = std::min(a.m_n + b.m_n, nMaxLen);
	sTmp.m_p += static_cast<int32_t>(sTmp.m_n - nMax); // should be signed, and promoted to ptrdiff_t
	sTmp.m_n = nMax;

	sTmp.SetMul(a, b);
	sTmp.Trim();

	std::swap(sRes, sTmp);
}

void Slice::Power(ConstSlice s, uint32_t n, Word* __restrict__ pBuf1, Word* __restrict__ pBuf2) const
{
	if (!m_n)
		return;

	Set0();

	if (!n)
	{
		m_p[m_n - 1] = 1;
		return;
	}

	s.Trim();
	if (!s.m_n)
		return; // zero

	if (s.m_n > m_n)
		s = s.get_Tail(m_n);

	Slice sRes = Cast::Up<Slice>(get_Tail(0));
	Slice sFree{ pBuf1, m_n };
	Slice sPwr{ pBuf2, m_n };

	while (true)
	{
		if (1 & n)
		{
			// multiply result by current pwr
			if (sRes.m_n)
				MyMul(sRes, sFree, m_n, sRes.get_Const(), s);
			else
			{
				// 1st time
				sRes = Cast::Up<Slice>(get_Tail(s.m_n));
				sRes.CopyInternal(s.m_p);
			}

			if (!sRes.m_n)
				break; // turned into 0, can happen due to overflow/truncation
		}

		n >>= 1;
		if (!n)
			break;

		// squre
		MyMul(sPwr, sFree, m_n, s, s);
		s = sPwr.get_Const();
	}

	if (sRes.get_TailPtr(0) == get_TailPtr(0))
		// already in our obj. Just zero heading
		Cast::Up<Slice>(get_Head(m_n - sRes.m_n)).Set0();
	else
		Copy(sRes.get_Const());
}

Word Slice::SetDiv(Word div)
{
	if (!div)
	{
		SetMax();
		return 0;
	}

	Trim();
	if (!m_n)
		return 0;

	DWord nom = m_p[0];
	Word resid;

	for (uint32_t i = 0; ; )
	{
		auto res = static_cast<Word>(nom / div);
		resid = static_cast<Word>(nom - res * div); // should not overflow

		m_p[i] = res;

		if (++i == m_n)
			break;

		nom = (static_cast<DWord>(resid) << nWordBits) | m_p[i];
	}

	Trim(); // can trim up to 1 word
	return resid;
}

bool Factorization::Composer::PowerNext(Word radix)
{
	auto carry = m_sPwr.Mul(radix);
	if (carry && (m_sPwr.m_n < m_sRes.m_n))
	{
		m_sPwr.m_n++;
		m_sPwr.m_p--;
		m_sPwr.m_p[0] = static_cast<Word>(carry);
	}
	else
	{
		m_sPwr.Trim();
		if (!m_sPwr.m_n)
			return false; // can happen due to overflow
	}

	return true;
}

} // namespace MultiWord
} // namespace beam
