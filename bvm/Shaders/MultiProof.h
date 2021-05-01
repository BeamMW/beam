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
#include "Sort.h"

struct MultiProof
{
	template <typename TCount>
	static TCount get_FirstHalf(TCount nTotalSize)
	{
		// evaluate target range. Beware of overflow (for large m_Count)
		TCount nLast = 0;
		while (nLast < nTotalSize)
			nLast = (nLast << 1) | 1;

		return (nLast >> 1) + 1;
	}

	template <class Base>
	struct Verifier
		:public Base
	{
		typedef typename Base::THash THash; // hash value. May be truncated
		typedef typename Base::TCount TCount; // 32/64 
		typedef typename Base::TElement TElement;

		struct Item
		{
			TCount m_Index;
			TElement m_Element;

			bool operator < (TCount i) const
			{
				return m_Index < i;
			}
		};

		void EvaluateRoot(THash& hv, Item* pItems, uint32_t nItems, TCount nTotalSize)
		{
			if (nTotalSize)
			{
				m_Count = nTotalSize;
				EvaluatePart(pItems, nItems, 0, get_FirstHalf(nTotalSize), hv);
			}
			else
				_POD_(hv).SetZero();
		}

	private:

		TCount m_Count; // total number of elements in the set

		bool EvaluatePart(Item* pItems, uint32_t nItems, TCount n, TCount nHalf, THash& hv)
		{
			if (!nItems)
			{
				Base::get_NextProofHash(hv);
			}
			else
			{
				// can't be out, contains elements
				if (!nHalf)
				{
					const TElement& x = pItems->m_Element;
					Base::Evaluate(hv, x);

					for (uint32_t i = 1; i < nItems; i++)
						Base::TestEqual(x, pItems[i].m_Element); // duplicated elements. This is allowed
				}
				else
				{

					TCount nMid = n + nHalf;
					nHalf >>= 1;

					uint32_t n0 = PivotSplit(pItems, nItems, nMid);

					EvaluatePart(pItems, n0, n, nHalf, hv);

					if (nMid < m_Count)
					{
						THash hv2;
						EvaluatePart(pItems + n0, nItems - n0, nMid, nHalf, hv2);
						Base::InterpretHash(hv, hv2);
					}
				}
			}

			return true;
		}
	};


	template <class Base>
	struct Builder
		:public Base
	{
		typedef typename Base::THash THash; // hash value. May be truncated
		typedef typename Base::TCount TCount; // 32/64 
		typedef typename Base::TElement TElement;

		void Build(TCount* pIndices, uint32_t nIndices, TCount nTotalSize)
		{
			if (nTotalSize)
			{
				m_Count = nTotalSize;
				BuildPart(pIndices, nIndices, 0, get_FirstHalf(nTotalSize), false);
			}
			else
				Base::ProofPushZero();
		}

	private:

		TCount m_Count; // total number of elements in the set

		void BuildPart(TCount* pIndices, uint32_t nIndices, TCount n, TCount nHalf, bool bFull)
		{
			if (!bFull)
			{
				if (nHalf)
				{
					TCount n1 = n + (nHalf << 1);
					if ((n1 <= m_Count) && (n1 > n)) // care of overflow
						bFull = true;
				}
				else
					bFull = true;
			}

			if (!nIndices && bFull)
			{
				// push the appropriate merkle tree element
				if (Base::ProofPush(n, nHalf))
					return; // ok

				// Can happen if lower-level elements were not stored (to reduce the storage size).
				assert(nHalf);
			}

			if (!nHalf)
			{
				assert(bFull && nIndices);
				return; // reached the element(s) being-prooven
			}

			TCount nMid = n + nHalf;
			nHalf >>= 1;

			uint32_t n0 = PivotSplit(pIndices, nIndices, nMid);

			BuildPart(pIndices, n0, n, nHalf, bFull);

			if (nMid < m_Count)
			{
				BuildPart(pIndices + n0, nIndices - n0, nMid, nHalf, bFull);

				if (!nIndices)
					Base::ProofMerge();
			}
		}

	};

};
