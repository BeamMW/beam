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

#include "uintBig.h"
#include "../utility/byteorder.h"

namespace beam {


	char ChFromHex(uint8_t v)
	{
		return v + ((v < 10) ? '0' : ('a' - 10));
	}

	uint8_t Ch2Hex(uint8_t c)
	{
		if (c >= 'a' && c <= 'f')
			return 0xa + (c - 'a');

		if (c >= 'A' && c <= 'F')
			return 0xa + (c - 'A');

		return c - '0';
	}

	void uintBigImpl::_ToNum(MultiWord::Slice s, const uint8_t* p, uint32_t n)
	{
		uint32_t nSizeDst = s.m_n * sizeof(MultiWord::Word);

		if (n >= nSizeDst)
			memcpy(s.m_p, p + n - nSizeDst, nSizeDst);
		else
		{
			uint32_t nZero = nSizeDst - n;
			memset0(s.m_p, nZero);

			memcpy(reinterpret_cast<uint8_t*>(s.m_p) + nZero, p, n);

			nZero /= sizeof(MultiWord::Word);
			s.m_n -= nZero;
			s.m_p += nZero;
		}

		for (uint32_t i = 0; i < s.m_n; i++)
			s.m_p[i] = ByteOrder::from_be(s.m_p[i]);

	}

	void uintBigImpl::_FromNum(MultiWord::ConstSlice s, uint8_t* p, uint32_t n)
	{
		while (n)
		{
			if (!s.m_n)
			{
				memset0(p, n);
				break;
			}

			auto val = ByteOrder::to_be(s.m_p[--s.m_n]);
			if (n < sizeof(val))
			{
				memcpy(p, reinterpret_cast<const uint8_t*>(&val) + sizeof(val) - n, n);
				break;
			}

			n -= sizeof(val);
			memcpy(p + n, &val, sizeof(val));
		}
	}

	std::string uintBigImpl::_Str(const uint8_t* pDst, uint32_t nDst)
    {
		std::string sRes;
		if (nDst)
		{
			sRes.resize(nDst * 2);
			_Print(pDst, nDst, &sRes.front());
		}
	    return sRes;
    }

	void uintBigImpl::_Print(const uint8_t* pDst, uint32_t nDst, std::ostream& s)
	{
		const uint32_t nDigitsMax = 8;
		if (nDst > nDigitsMax)
			nDst = nDigitsMax; // truncate

		char sz[nDigitsMax * 2 + 1];

		_Print(pDst, nDst, sz);
		s << sz;
	}

	void uintBigImpl::_PrintFull(const uint8_t* pDst, uint32_t nDst, std::ostream& s)
	{
		const uint32_t nNaggle = 8;

		char sz[nNaggle * 2 + 1];
		while (nDst)
		{
			bool bLast = (nDst <= nNaggle);

			_Print(pDst, bLast ? nDst : nNaggle, sz);
			s << sz;

			if (bLast)
				break;

			pDst += nNaggle;
			nDst -= nNaggle;
		}

	}

	void uintBigImpl::_Print(const uint8_t* pDst, uint32_t nDst, char* sz)
	{
		for (uint32_t i = 0; i < nDst; i++)
		{
			sz[i * 2] = ChFromHex(pDst[i] >> 4);
			sz[i * 2 + 1] = ChFromHex(pDst[i] & 0xf);
		}

		sz[nDst << 1] = 0;
	}

	uint32_t uintBigImpl::_Scan(uint8_t* pDst, const char* sz, uint32_t nTxtLen)
	{
		uint32_t ret = 0;
		for (; ret < nTxtLen; ret++)
		{
			uint8_t x = Ch2Hex(sz[ret]);
			if (x > 0xf)
				break;

			if (1 & ret)
				*pDst++ |= x;
			else
				*pDst = (x << 4);
		}

		return ret;
	}

	void uintBigImpl::_Assign(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc)
	{
		if (nSrc >= nDst)
			memcpy(pDst, pSrc + nSrc - nDst, nDst);
		else
		{
			memset0(pDst, nDst - nSrc);
			memcpy(pDst + nDst - nSrc, pSrc, nSrc);
		}
	}

	uint8_t uintBigImpl::_Inc(uint8_t* pDst, uint32_t nDst)
	{
		for (uint32_t i = nDst; i--; )
			if (++pDst[i])
				return 0;

		return 1;
	}

	uint8_t uintBigImpl::_Inc(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc)
	{
		uint16_t carry = 0;
		for (uint32_t i = nDst; i--; )
		{
			carry += pDst[i];
			carry += pSrc[i];

			pDst[i] = (uint8_t) carry;
			carry >>= 8;
		}

		return (uint8_t) carry;
	}

	uint8_t uintBigImpl::_Inc(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc)
	{
		if (nDst <= nSrc)
			return _Inc(pDst, nDst, pSrc + nSrc - nDst); // src is at least our size

		if (!_Inc(pDst + nDst - nSrc, nSrc, pSrc))
			return 0;

		// propagete carry
		return _Inc(pDst, nDst - nSrc);
	}

	void uintBigImpl::_Inv(uint8_t* pDst, uint32_t nDst)
	{
		for (uint32_t i = nDst; i--; )
			pDst[i] ^= 0xff;
	}

	void uintBigImpl::_Xor(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc)
	{
		for (uint32_t i = nDst; i--; )
			pDst[i] ^= pSrc[i];
	}

	void uintBigImpl::_Xor(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc)
	{
		if (nDst <= nSrc)
			_Xor(pDst, nDst, pSrc + nSrc - nDst); // src is at least our size
		else
			_Xor(pDst + nDst - nSrc, nSrc, pSrc);
	}

	int uintBigImpl::_Cmp(const uint8_t* pSrc0, uint32_t nSrc0, const uint8_t* pSrc1, uint32_t nSrc1)
	{
		if (nSrc0 > nSrc1)
		{
			uint32_t diff = nSrc0 - nSrc1;
			if (!memis0(pSrc0, diff))
				return 1;

			pSrc0 += diff;
			nSrc0 = nSrc1;
		} else
			if (nSrc0 < nSrc1)
			{
				uint32_t diff = nSrc1 - nSrc0;
				if (!memis0(pSrc1, diff))
					return -1;

				pSrc1 += diff;
			}

		return memcmp(pSrc0, pSrc1, nSrc0);
	}

	uint32_t uintBigImpl::_GetOrderBytes(const uint8_t* pDst, uint32_t nDst)
	{
		for (uint32_t nByte = 0; nByte < nDst; nByte++)
			if (pDst[nByte])
				return nDst - nByte;

		return 0;
	}

	uint32_t uintBigImpl::_GetOrder(const uint8_t* pDst, uint32_t nDst)
	{
		auto ret = _GetOrderBytes(pDst, nDst);
		if (ret)
		{
			uint8_t x = pDst[nDst - ret];
			assert(x);

			ret = (ret << 3) - 7;
			while (x >>= 1)
				ret++;
		}

		return ret;
	}

	bool uintBigImpl::_Accept(uint8_t* pDst, const uint8_t* pThr, uint32_t nDst, uint32_t nThrOrder)
	{
		if (!nThrOrder)
			return false;

		nThrOrder--;
		uint32_t nOffs = nDst - 1 - (nThrOrder >> 3);
		uint8_t msk = uint8_t(2 << (7 & nThrOrder)) - 1;
		assert(msk);

		pDst[nOffs] &= msk;

		if (memcmp(pDst + nOffs, pThr + nOffs, nDst - nOffs) >= 0)
			return false;

		memset0(pDst, nOffs);
		return true;
	}

	FourCC::Text::Text(uint32_t n)
	{
		reinterpret_cast<uintBigFor<uint32_t>::Type&>(m_sz) = n; // convertion
		m_sz[_countof(m_sz) - 1] = 0;

		// fix illegal chars
		for (size_t i = 0; i < _countof(m_sz) - 1; i++)
		{
			char& c = m_sz[i];
			if ((c < ' ') || (c > '~'))
				c = ' ';
		}
	}

	std::ostream& operator << (std::ostream& s, const FourCC& x)
	{
		s << FourCC::Text(x);
		return s;
	}

	std::ostream& operator << (std::ostream& s, const FourCC::Text& x)
	{
		s << x.m_sz;
		return s;
	}

	void uintBigImpl::_ShiftRight(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc, uint32_t nBits)
	{
		// assuming pDst and pSrc may be the same

		uint32_t nBytes = nBits >> 3;
		if (nBytes >= nSrc)
			nSrc = nBits = 0;
		else
		{
			nSrc -= nBytes;
			nBits &= 7;
		}

		uint8_t* pDst0 = pDst;

		if (nDst > nSrc)
		{
			pDst += nDst - nSrc;
			nDst = nSrc;
		}
		else
			pSrc += nSrc - nDst;

		if (nBits)
		{
			uint32_t nLShift = 8 - nBits;

			for (uint32_t i = nDst; i--; )
			{
				// pSrc and pDst may be the same
				pDst[i] = pSrc[i] >> nBits;
				if (nSrc + i > nDst)
					pDst[i] |= (pSrc[int32_t(i - 1)] << nLShift);
			}
		}
		else
			memmove(pDst, pSrc, nDst);

		memset0(pDst0, pDst - pDst0);
	}

	void uintBigImpl::_ShiftLeft(uint8_t* pDst, uint32_t nDst, const uint8_t* pSrc, uint32_t nSrc, uint32_t nBits)
	{
		// assuming pDst and pSrc may be the same

		uint32_t nBytes = nBits >> 3;
		if (nBytes >= nDst)
		{
			nBytes = nDst;
			nDst = nBits = 0;
		}
		else
		{
			nBits &= 7;
			nDst -= nBytes;
		}

		uint8_t* pDst0 = pDst;

		if (nSrc > nDst)
		{
			pSrc += nSrc - nDst;
			nSrc = nDst;
		}
		else
		{
			memset0(pDst, nDst - nSrc);
			pDst += nDst - nSrc;
		}

		if (nBits)
		{
			if (nSrc)
			{
				uint32_t nRShift = 8 - nBits;

				if (nDst > nSrc)
					pDst[-1] = pSrc[0] >> nRShift;

				for (size_t i = 0; i < nSrc; i++)
				{
					pDst[i] = pSrc[i] << nBits;
					if (i + 1 < nSrc)
						pDst[i] |= pSrc[i + 1] >> nRShift;
				}
			}
		}
		else
			memmove(pDst, pSrc, nSrc);

		memset0(pDst0 + nDst, nBytes);
	}

} // namespace beam
