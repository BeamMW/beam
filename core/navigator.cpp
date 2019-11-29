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

#include "navigator.h"

namespace beam
{
	////////////////////////////////////////
	// ChainNavigator
	void ChainNavigator::Open(const char* sz)
	{
		MappedFile::Defs d;
		d.m_pSig = NULL;
		d.m_nSizeSig = 0;
		d.m_nFixedHdr = sizeof(FixedHdr);
		d.m_nBanks = Type::count;

		AdjustDefs(d);

		m_Mapping.Open(sz, d);

		FixedHdr& hdr = get_Hdr_();
		if (!hdr.m_TagCursor)
			hdr.m_TagCursor = m_Mapping.get_Offset(&hdr.m_Root);

		OnOpen();
	}

	void ChainNavigator::Close()
	{
		OnClose();
		m_Mapping.Close();
	}

	ChainNavigator::TagMarker& ChainNavigator::get_Tag_(Offset x) const
	{
		assert(x);
		return m_Mapping.get_At<TagMarker>(x);
	}

	ChainNavigator::Patch& ChainNavigator::get_Patch_(Offset x) const
	{
		assert(x);
		return m_Mapping.get_At<Patch>(x);
	}

	ChainNavigator::Offset ChainNavigator::get_ChildTag(Offset x) const
	{
		return get_Tag(x).m_Child0;
	}

	ChainNavigator::Offset ChainNavigator::get_ChildTag() const
	{
		return get_ChildTag(get_Hdr().m_TagCursor);
	}

	ChainNavigator::Offset ChainNavigator::get_NextTag(Offset x) const
	{
		return get_Tag(x).m_Links.p[0];
	}

	void ChainNavigator::ApplyTagChanges(Offset nOffs, bool bFwd)
	{
		FixedHdr& hdr = get_Hdr_();
		const TagMarker& tag = get_Tag(nOffs);

		if (bFwd)
		{
			assert(hdr.m_TagCursor == tag.m_Parent);
			hdr.m_TagCursor = nOffs;
		} else
		{
			assert(hdr.m_TagCursor == nOffs);
			hdr.m_TagCursor = tag.m_Parent;
			assert(hdr.m_TagCursor);
		}

		hdr.m_TagInfo.ModifyBy(tag.m_Diff, bFwd);

		int iDir = !bFwd;
		for (nOffs = tag.m_Patches.p[iDir]; nOffs; )
		{
			const Patch& patch = get_Patch_(nOffs);
			nOffs = patch.m_Links.p[iDir];
			Apply(patch, bFwd);
		}
	}

	void ChainNavigator::MoveFwd(Offset x)
	{
		ApplyTagChanges(x, true);
	}

	bool ChainNavigator::MoveBwd()
	{
		FixedHdr& hdr = get_Hdr_();
		assert(hdr.m_TagCursor);
		if (hdr.m_TagCursor == m_Mapping.get_Offset(&hdr.m_Root))
			return false;

		ApplyTagChanges(hdr.m_TagCursor, false);
		return true;
	}

	template <class T>
	void ChainNavigator::ListOperate(T& node, bool bAdd, Offset* pE, int nE)
	{
		Offset x = m_Mapping.get_Offset(&node);

		for (int i = 0; i < static_cast<int>(_countof(node.m_Links.p)); i++)
			ListOperateDir(node, x, i, bAdd, pE, nE);
	}

	template <class T>
	void ChainNavigator::ListOperateDir(T& node, Offset n, int iDir, bool bAdd, Offset* pE, int nE)
	{
		Offset* pTrg;
		if (node.m_Links.p[iDir])
			pTrg = m_Mapping.get_At<T>(node.m_Links.p[iDir]).m_Links.p + !iDir;
		else
		{
			if (int(!iDir) >= nE)
				return;
			pTrg = pE + !iDir;
		}

		if (bAdd)
		{
			assert(*pTrg == node.m_Links.p[!iDir]);
			*pTrg = n;
		} else
		{
			assert(*pTrg == n);
			*pTrg = node.m_Links.p[!iDir];
		}
	}

	void ChainNavigator::PatchListOperate(TagMarker& tag, Patch& patch, bool bAdd)
	{
		ListOperate(patch, bAdd, tag.m_Patches.p, _countof(tag.m_Patches.p));
	}

	void ChainNavigator::Commit(Patch& patch, bool bApply /* = true */)
	{
		TagMarker& tag = get_Tag_(get_Hdr().m_TagCursor);
		patch.m_Links.p[0] = 0;
		patch.m_Links.p[1] = tag.m_Patches.p[1];

		PatchListOperate(tag, patch, true);

		if (bApply)
			Apply(patch, true);
	}

	void ChainNavigator::CreateTag(const TagInfo& ti, bool bMoveTo /* = true */)
	{
		TagMarker* pVal = (TagMarker*) m_Mapping.Allocate(Type::Tag, sizeof(TagMarker));
		Offset xVal = m_Mapping.get_Offset(pVal);

		ZeroObject(*pVal);

		FixedHdr& hdr = get_Hdr_();
		TagMarker& tag = get_Tag_(hdr.m_TagCursor);

		pVal->m_Links.p[0] = tag.m_Child0;
		ListOperate(*pVal, true, &tag.m_Child0, 1);

		pVal->m_Parent = hdr.m_TagCursor;

		pVal->m_Diff = ti;
		pVal->m_Diff.ModifyBy(hdr.m_TagInfo, false);

		if (bMoveTo)
			MoveFwd(xVal);
	}

	void ChainNavigator::DeleteTag(Offset x)
	{
		assert(x);

		assert(&get_Hdr_().m_Root != &get_Tag_(x));
		if (get_Hdr_().m_TagCursor == x)
			MoveBwd();

		MovePatchesToChildren(x);

		// move children to the parent
		TagMarker& t = get_Tag_(x);
		TagMarker& tParent = get_Tag_(t.m_Parent);

		ListOperate(t, false, &tParent.m_Child0, 1);

		while (t.m_Child0)
		{
			TagMarker& tC = get_Tag_(t.m_Child0);
			assert(tC.m_Parent == x);
			ListOperate(tC, false, &t.m_Child0, 1);

			assert(!tC.m_Links.p[1]);
			tC.m_Links.p[0] = tParent.m_Child0;
			ListOperate(tC, true, &tParent.m_Child0, 1);
			tC.m_Parent = t.m_Parent;
		}

		m_Mapping.Free(Type::Tag, &t);
	}

	void ChainNavigator::MovePatchesToChildren(Offset xTag)
	{
		TagMarker& tag = get_Tag_(xTag);
		Links patches = tag.m_Patches;

		if (!patches.p[0])
			return;

		for (Offset xC1 = tag.m_Child0; xC1; )
		{
			Offset xC = xC1;
			TagMarker& c = get_Tag_(xC);
			xC1 = c.m_Links.p[0];

			Offset xFirst = c.m_Patches.p[0];

			if (!xC1)
			{
				if (xFirst)
				{
					// append
					get_Patch_(patches.p[1]).m_Links.p[0] = xFirst;
					get_Patch_(xFirst).m_Links.p[1] = patches.p[1];

					c.m_Patches.p[0] = patches.p[0];

				} else
					// assign
					c.m_Patches = patches;

				return;
			}

			// clone (multiple children)
			for (Offset xP = patches.p[1]; xP; )
			{
				Offset xP0 = xP;
				Patch* pPatch = &get_Patch_(xP);
				xP = pPatch->m_Links.p[1];

				pPatch = Clone(xP0);
				Offset xNew = m_Mapping.get_Offset(pPatch);

				pPatch->m_Links.p[0] = xFirst;
				pPatch->m_Links.p[1] = 0;
				PatchListOperate(get_Tag_(xC), *pPatch, true);

				xFirst = xNew;
			}
		}

		// not consumed - delete them
		for (Offset xP = patches.p[1]; xP; )
		{
			Patch* pPatch = &get_Patch_(xP);
			xP = pPatch->m_Links.p[1];

			Delete(*pPatch);
		}
	}

	void ChainNavigator::TagInfo::ModifyBy(const TagInfo& ti, bool bFwd)
	{
		if (bFwd)
		{
			m_Height += ti.m_Height;
		} else
		{
			m_Height += ti.m_Height; //?
		}

		for (size_t i = 0; i < m_Tag.nBytes; i++)
			m_Tag.m_pData[i] ^= ti.m_Tag.m_pData[i];
	}

	void ChainNavigator::assert_valid() const
	{
		const FixedHdr& hdr = get_Hdr();

		const uint8_t* p = (uint8_t*) &hdr.m_Root.m_Diff;
		for (size_t i = 0; i < sizeof(hdr.m_Root.m_Diff); i++)
			assert_valid(!p[i]); // 1st tag is zero

		assert_valid(!hdr.m_Root.m_Parent);

		// no branching as well
		assert_valid(!hdr.m_Root.m_Links.p[0]);
		assert_valid(!hdr.m_Root.m_Links.p[1]);

		bool bCursorHit = false;
		assert_valid(hdr.m_Root, bCursorHit);
		assert_valid(bCursorHit);
	}

	void ChainNavigator::assert_valid(const TagMarker& t, bool& bCursorHit) const
	{
		if (m_Mapping.get_Offset(&t) == get_Hdr().m_TagCursor)
			bCursorHit = true;

		// patch list
		Offset xPrev = 0;
		for (Offset x = t.m_Patches.p[0]; x; )
		{
			const Patch& patch = get_Patch_(x);
			assert_valid(patch.m_Links.p[1] == xPrev);

			xPrev = x;
			x = patch.m_Links.p[0];
		}
		assert_valid(t.m_Patches.p[1] == xPrev);

		// children
		xPrev = 0;

		for (Offset x = t.m_Child0; x; )
		{
			const TagMarker& tag = get_Tag(x);
			assert_valid(tag.m_Parent == m_Mapping.get_Offset(&t));

			assert_valid(tag.m_Links.p[1] == xPrev);

			assert_valid(tag, bCursorHit);

			xPrev = x;
			x = tag.m_Links.p[0];
		}
	}

} // namespace beam