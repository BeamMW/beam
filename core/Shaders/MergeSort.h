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
