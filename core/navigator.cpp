#include "navigator.h"

#ifndef WIN32
#	include <errno.h>
#	include <sys/stat.h>
#	include <fcntl.h>
#	include <sys/mman.h>
#	include <sys/types.h>
#	include <unistd.h>
#endif // WIN32

namespace beam
{
	void test_SysRet(bool bFail, const char* str)
	{
		if (bFail)
		{
#ifdef WIN32
			int nErrorCode = GetLastError();
#else // WIN32
			int nErrorCode = errno;
#endif // WIN32

			char sz[0x100];
			snprintf(sz, _countof(sz), "Error=%d (%s)", nErrorCode, str);
			throw std::runtime_error(sz);
		}
	}

	template <typename T>
	T AlignUp(T x, uint32_t nAlignment)
	{
		uint32_t msk = nAlignment - 1;
		assert(!(msk & nAlignment)); // alignment must be power-2
		return (x + T(msk)) & ~T(msk);
	}

	////////////////////////////////////////
	// MappedFile
	uint32_t MappedFile::s_PageSize = 0;

	MappedFile::MappedFile()
	{
		ResetVarsFile();
		ResetVarsMapping();
	}

	void MappedFile::ResetVarsFile()
	{
#ifdef WIN32
		m_hFile = INVALID_HANDLE_VALUE;
#else // WIN32
		m_hFile = -1;
#endif // WIN32

		m_nBanks = 0;
	}

	void MappedFile::ResetVarsMapping()
	{
#ifdef WIN32
		m_hMapping = NULL;
#endif // WIN32

		m_pMapping = NULL;
		m_nMapping = 0;
	}

	MappedFile::~MappedFile()
	{
		Close();
	}

	void MappedFile::CloseMapping()
	{
#ifdef WIN32
		if (m_pMapping)
			verify(UnmapViewOfFile(m_pMapping));
		if (m_hMapping)
			verify(CloseHandle(m_hMapping));
#else // WIN32
		if (m_pMapping)
			verify(!munmap(m_pMapping, m_nMapping));
#endif // WIN32

		ResetVarsMapping();
	}

	void MappedFile::Close()
	{
		CloseMapping();

#ifdef WIN32
		if (INVALID_HANDLE_VALUE != m_hFile)
			verify(CloseHandle(m_hFile));
#else // WIN32
		if (-1 != m_hFile)
			verify(!close(m_hFile));
#endif // WIN32

		ResetVarsFile();
	}

	void MappedFile::OpenMapping()
	{
		assert(!m_pMapping);

#ifdef WIN32

		test_SysRet(!GetFileSizeEx(m_hFile, (LARGE_INTEGER*) &m_nMapping), "GetFileSizeEx");
		if (m_nMapping)
		{
			m_hMapping = CreateFileMapping(m_hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
			test_SysRet(!m_hMapping, "CreateFileMapping");

			m_pMapping = (uint8_t*) MapViewOfFile(m_hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, (size_t) m_nMapping);
			test_SysRet(!m_pMapping, "MapViewOfFile");
		}

#else // WIN32

		struct stat stats;
		test_SysRet(fstat(m_hFile, &stats) != 0, "fstat");
		m_nMapping = stats.st_size;

		if (m_nMapping)
		{
			uint8_t* pPtr = (uint8_t*) mmap(NULL, m_nMapping, PROT_READ | PROT_WRITE, MAP_SHARED, m_hFile, 0);
			test_SysRet(MAP_FAILED == pPtr, "mmap");

			m_pMapping = pPtr;
		}

#endif // WIN32
	}

	uint32_t MappedFile::Defs::get_Bank0() const
	{
		return AlignUp(m_nSizeSig, sizeof(Offset));
	}

	uint32_t MappedFile::Defs::get_SizeMin() const
	{
		return
			get_Bank0() +
			m_nBanks * sizeof(Bank) + 
			AlignUp(m_nFixedHdr, sizeof(Offset));
	}

//	void MappedFile::Write(const void* p, uint32_t n)
//	{
//#ifdef WIN32
//		DWORD dw;
//		test_SysRet(!WriteFile(m_hFile, p, n, &dw, NULL));
//#else // WIN32
//		size_t nRet = write(m_hFile, p, n);
//		Test::SysRet(nRet != n);
//#endif // WIN32
//	}

	//void MappedFile::WriteZero(uint32_t n)
	//{
	//	uint8_t pBuf[0x400];
	//	if (n <= sizeof(pBuf))
	//		memset(pBuf, 0, n);
	//	else
	//	{
	//		memset(pBuf, 0, sizeof(pBuf));
	//		do
	//		{
	//			Write(pBuf, sizeof(pBuf));
	//			n -= sizeof(pBuf);
	//		} while (n > sizeof(pBuf));
	//	}

	//	Write(pBuf, n);
	//}

	void MappedFile::Resize(Offset n)
	{
#ifdef WIN32
		test_SysRet(!SetFilePointerEx(m_hFile, (const LARGE_INTEGER&) n, NULL, FILE_BEGIN), "SetFilePointerEx");
		test_SysRet(!SetEndOfFile(m_hFile), "SetEndOfFile");
#else // WIN32
		test_SysRet(ftruncate(m_hFile, n) != 0, "ftruncate");
#endif // WIN32
	}

	void MappedFile::Open(const char* sz, const Defs& d)
	{
		Close();

		if (!s_PageSize)
		{
#ifdef WIN32
			SYSTEM_INFO sysInfo;
			GetSystemInfo(&sysInfo);
			s_PageSize = sysInfo.dwPageSize;
#else // WIN32
			s_PageSize = getpagesize();
#endif // WIN32
		}

#ifdef WIN32
		m_hFile = CreateFileA(sz, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
		test_SysRet(INVALID_HANDLE_VALUE == m_hFile, "CreateFile");
#else // WIN32
		m_hFile = open(sz, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
		test_SysRet(-1 == m_hFile, "open");
#endif // WIN32

		OpenMapping();

		uint32_t nSizeMin = d.get_SizeMin();
		if ((m_nMapping < nSizeMin) || memcmp(d.m_pSig, m_pMapping, d.m_nSizeSig))
		{
			bool bShouldZeroInit = (m_nMapping > d.m_nSizeSig);
			CloseMapping();

			if (bShouldZeroInit)
				Resize(d.m_nSizeSig); // will zero-init all this data

			Resize(nSizeMin);

			OpenMapping();
			memcpy(m_pMapping, d.m_pSig, d.m_nSizeSig);
		}

		m_nBank0 = d.get_Bank0();
		m_nBanks = d.m_nBanks;
	}

	void* MappedFile::get_FixedHdr() const
	{
		return m_pMapping + m_nBank0 + m_nBanks * sizeof(Bank);
	}

	MappedFile::Offset MappedFile::get_Offset(const void* p) const
	{
		Offset x = ((const uint8_t*) p) - m_pMapping;
		assert(x < m_nMapping);
		return x;
	}

	MappedFile::Bank& MappedFile::get_Bank(uint32_t iBank)
	{
		assert(m_pMapping && (iBank < m_nBanks));
		return ((Bank*) (m_pMapping + m_nBank0))[iBank];
	}

	void* MappedFile::Allocate(uint32_t iBank, uint32_t nSize)
	{
		assert(nSize >= sizeof(Offset));
		Bank* pBank = &get_Bank(iBank);
		if (!pBank->m_Tail)
		{
			// grow
			Offset n0 = m_nMapping;
			Offset n1 = AlignUp(n0, s_PageSize) + s_PageSize;

			nSize = AlignUp(nSize, sizeof(Offset));

			CloseMapping();
			Resize(n1);
			OpenMapping();

			pBank = &get_Bank(iBank);
			Offset* p = &pBank->m_Tail;

			while (true)
			{
				Offset n0_ = n0 + nSize;
				if (n0_ > m_nMapping)
					break;

				assert(! *p);
				*p = n0;
				p = &get_At<Offset>(n0);

				n0 = n0_;
			}
		}

		Offset& ret = get_At<Offset>(pBank->m_Tail);
		pBank->m_Tail = ret;

		return &ret;
	}

	void MappedFile::Free(uint32_t iBank, void* p)
	{
		assert(p);
		Bank& b = get_Bank(iBank);

		*((Offset*) p) = b.m_Tail;

		b.m_Tail = ((uint8_t*) p) - m_pMapping;
		assert(b.m_Tail < m_nMapping);
	}

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

		for (int i = 0; i < _countof(node.m_Links.p); i++)
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

		for (int i = 0; i < _countof(m_Tag.m_pData); i++)
			m_Tag.m_pData[i] ^= ti.m_Tag.m_pData[i];
	}

	void ChainNavigator::assert_valid() const
	{
		const FixedHdr& hdr = get_Hdr();

		const uint8_t* p = (uint8_t*) &hdr.m_Root.m_Diff;
		for (int i = 0; i < sizeof(hdr.m_Root.m_Diff); i++)
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