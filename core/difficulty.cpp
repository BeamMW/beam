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

#include "difficulty.h"
#include <cmath>

namespace beam
{
	void Difficulty::Pack(uint32_t order, uint32_t mantissa)
	{
		if (order <= s_MaxOrder)
		{
			assert((mantissa >> s_MantissaBits) == 1U);
			mantissa &= (1U << s_MantissaBits) - 1;

			m_Packed = mantissa | (order << s_MantissaBits);
		}
		else
			m_Packed = s_Inf;
	}

	void Difficulty::Unpack(uint32_t& order, uint32_t& mantissa) const
	{
		order = (m_Packed >> s_MantissaBits);

		const uint32_t nLeadingBit = 1U << s_MantissaBits;
		mantissa = nLeadingBit | (m_Packed & (nLeadingBit - 1));
	}

	bool Difficulty::IsTargetReached(const ECC::uintBig& hv) const
	{
		if (m_Packed > s_Inf)
			return false; // invalid

		// multiply by (raw) difficulty, check if the result fits wrt normalization.
		Raw val;
		Unpack(val);

		auto a = hv * val; // would be 512 bits

		static_assert(!(s_MantissaBits & 7), ""); // fix the following code lines to support non-byte-aligned mantissa size

		return memis0(a.m_pData, Raw::nBytes - (s_MantissaBits >> 3));
	}

	void Difficulty::Unpack(Raw& res) const
	{
		res = Zero;
		if (m_Packed < s_Inf)
		{
			uint32_t order, mantissa;
			Unpack(order, mantissa);
			res.AssignSafe(mantissa, order);

		}
		else
			res.Inv();
	}

	Difficulty::Raw operator + (const Difficulty::Raw& base, const Difficulty& d)
	{
		Difficulty::Raw res;
		d.Unpack(res);
		res += base;
		return res;
	}

	Difficulty::Raw& operator += (Difficulty::Raw& res, const Difficulty& d)
	{
		Difficulty::Raw base;
		d.Unpack(base);
		res += base;
		return res;
	}

	Difficulty::Raw operator - (const Difficulty::Raw& base, const Difficulty& d)
	{
		Difficulty::Raw res;
		d.Unpack(res);
		res.Negate();
		res += base;
		return res;
	}

	Difficulty::Raw& operator -= (Difficulty::Raw& res, const Difficulty& d)
	{
		Difficulty::Raw base;
		d.Unpack(base);
		base.Negate();
		res += base;
		return res;
	}

	struct Difficulty::BigFloat
	{
		int m_Order; // signed
		uint32_t m_Value;
		static const uint32_t nBits = sizeof(uint32_t) << 3;

		template <uint32_t nBytes_>
		void operator = (const uintBig_t<nBytes_>& val)
		{
			uint32_t nOrder = val.get_Order();
			if (nOrder)
			{
				m_Order = nOrder - nBits;

				uintBigFor<uint32_t>::Type x;
				if (m_Order > 0)
					val.ShiftRight(m_Order, x);
				else
					val.ShiftLeft(-m_Order, x);

				x.Export(m_Value);
			}
			else
			{
				m_Order = 0;
				m_Value = 0;
			}
		}

		template <typename T>
		void operator = (T val)
		{
			*this = typename uintBigFor<T>::Type(val);
		}

		BigFloat operator * (const BigFloat& x) const
		{
			uint64_t val = m_Value;
			val *= x.m_Value;

			BigFloat res(val);
			res.m_Order += m_Order + x.m_Order;

			return res;
		}

		BigFloat operator / (const BigFloat& x) const
		{
			assert(x.m_Value); // otherwise div-by-zero exc

			uint64_t val = m_Value;
			val <<= nBits;
			val /= x.m_Value;

			BigFloat res(val);
			res.m_Order += m_Order - (x.m_Order + nBits);
			return res;
		}

		template <typename T>
		BigFloat(const T& x) { *this = x; }
	};

	void Difficulty::Calculate(const Raw& ref, uint32_t dh, uint32_t dtTrg_s, uint32_t dtSrc_s)
	{
		uint64_t div = dtSrc_s;
		div *= dh;

		BigFloat x = BigFloat(ref) * BigFloat(dtTrg_s) / BigFloat(div);

		// to packed.
		m_Packed = 0;

		if (x.m_Value)
		{
			assert(1 & (x.m_Value >> (BigFloat::nBits - 1)));
			x.m_Order += (BigFloat::nBits - 1 - s_MantissaBits);
			if (x.m_Order >= 0)
			{
				x.m_Value >>= (BigFloat::nBits - 1 - s_MantissaBits);
				Pack(x.m_Order, x.m_Value);
			}
		}
	}

	double Difficulty::ToFloat() const
	{
		uint32_t order, mantissa;
		Unpack(order, mantissa);

		int nOrderCorrected = order - s_MantissaBits; // must be signed
		return ldexp(mantissa, nOrderCorrected);
	}

	double Difficulty::ToFloat(Raw& x)
	{
		double res = 0;

		int nOrder = x.nBits - 8 - s_MantissaBits;
		for (uint32_t i = 0; i < x.nBytes; i++, nOrder -= 8)
		{
			uint8_t n = x.m_pData[i];
			if (n)
				res += ldexp(n, nOrder);
		}

		return res;
	}

	std::ostream& operator << (std::ostream& s, const Difficulty& d)
	{
		typedef uintBig_t<sizeof(Difficulty) - Difficulty::s_MantissaBits/8> uintOrder;
		typedef uintBig_t<Difficulty::s_MantissaBits/8> uintMantissa;

		uintOrder n0;
		n0.AssignSafe(d.m_Packed >> Difficulty::s_MantissaBits, 0);
		char sz0[uintOrder::nTxtLen + 1];
		n0.Print(sz0);

		uintMantissa n1;
		n1.AssignSafe(d.m_Packed & ((1U << Difficulty::s_MantissaBits) - 1), 0);
		char sz1[uintMantissa::nTxtLen + 1];
		n1.Print(sz1);

		s << sz0 << '-' << sz1 << '(' << d.ToFloat() << ')';

		return s;
	}

} // namespace beam
