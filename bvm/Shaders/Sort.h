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

// Merge sort. Written (supposedly) in a wasm-friendly way

template <typename T>
class MergeSort
{
	static void Copy(T* pDst, const T* p, const T* pe)
	{
		while (p < pe)
			*pDst++ = *p++;
	}

	static void DoStep(T* pDst, const T* pSrc, uint32_t nCount, uint32_t nW)
	{
		const T* pEnd = pSrc + nCount;

		for (const T* p0 = pSrc; ; )
		{
			const T* p1 = p0 + nW;
			if (p1 >= pEnd)
			{
				// just copy what's left
				Copy(pDst, p0, pEnd);
				break;
			}

			const T* p1e = p1 + nW;
			bool bFin = (p1e >= pEnd);
			if (bFin)
				p1e = pEnd;

			MergeOnce(pDst, p0, p1, p1e);

			if (bFin)
				break;

			p0 = p1e;
			pDst += nW * 2;
		}
	}

	static void MergeOnce(T* pDst, const T* p0, const T* p1, const T* p1e)
	{
		const T* p0e = p1;

		while (true)
		{
			if (*p1 < *p0)
			{
				*pDst++ = *p1++;
				if (p1 == p1e)
				{
					Copy(pDst, p0, p0e);
					break;
				}
			}
			else
			{
				*pDst++ = *p0++;
				if (p0 == p0e)
				{
					Copy(pDst, p1, p1e);
					break;
				}
			}
		}
	}

public:

	static T* Do(T* pSrc, T* pTmp, uint32_t nCount)
	{
		for (uint32_t nW = 1; nW < nCount; nW <<= 1)
		{
			DoStep(pTmp, pSrc, nCount, nW);
			std::swap(pSrc, pTmp);
		}

		return pSrc;
	}
};

template <typename T, typename TPivot>
inline uint32_t PivotSplit(T* p, uint32_t n, TPivot pivot)
{
	for (uint32_t i = 0; i < n; )
	{
		if (p[i] < pivot)
			i++;
		else
		{
			do
			{
				if (p[--n] < pivot)
				{
					std::swap(p[i], p[n]);
					i++;
					break;
				}
			} while (i < n);
		}
	}

	return n;
}

// assuming provided elements are not descending (a[i] <= a[i+1])
// returns the lowest index s.t. p[ret] >= x, or n if no such an element
template <typename T, typename TPivot>
inline uint32_t MedianSearch(const T* p, uint32_t n, const TPivot& x)
{
	uint32_t i0 = 0;
	while (n)
	{
		uint32_t nHalf = n / 2;
		if (p[i0 + nHalf] < x)
		{
			nHalf++;
			i0 += nHalf;
			n -= nHalf;
		}
		else
			n = nHalf;
	}

	return i0;
}

template <typename TX, typename TY>
inline TY LutCalculate(const TX* pX, const TY* pY, uint32_t nLut, const TX& x)
{
	assert(nLut);
	uint32_t n = MedianSearch(pX, nLut, x);

	if (n == nLut)
		return pY[nLut - 1];
	if (!n)
		return pY[0];

	const TY& y0 = pY[n - 1];

	// y0,y1 are not necessarily in ascending order. Use signed arithmetics
	int64_t val =
		((int64_t) (pY[n] - y0)) *
		((int64_t) (x - pX[n - 1])) /
		((int64_t) (pX[n] - pX[n - 1]));

	return static_cast<TY>(y0 + val);
}
