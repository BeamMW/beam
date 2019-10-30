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

#include "mapped_file.h"

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
            BEAM_VERIFY(UnmapViewOfFile(m_pMapping));
		if (m_hMapping)
            BEAM_VERIFY(CloseHandle(m_hMapping));
#else // WIN32
		if (m_pMapping)
            BEAM_VERIFY(!munmap(m_pMapping, m_nMapping));
#endif // WIN32

		ResetVarsMapping();
	}

	void MappedFile::Close()
	{
		CloseMapping();

#ifdef WIN32
		if (INVALID_HANDLE_VALUE != m_hFile)
            BEAM_VERIFY(CloseHandle(m_hFile));
#else // WIN32
		if (-1 != m_hFile)
            BEAM_VERIFY(!close(m_hFile));
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
		static_assert(!(sizeof(Bank) % sizeof(Offset)), "");

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

	void MappedFile::Open(const char* sz, const Defs& d, bool bReset /* = false */)
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
		m_hFile = CreateFileW(Utf8toUtf16(sz).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
		test_SysRet(INVALID_HANDLE_VALUE == m_hFile, "CreateFile");
#else // WIN32
		m_hFile = open(sz, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
		test_SysRet(-1 == m_hFile, "open");
#endif // WIN32

		OpenMapping();

		uint32_t nSizeMin = d.get_SizeMin();
		if (bReset || (m_nMapping < nSizeMin) || memcmp(d.m_pSig, m_pMapping, d.m_nSizeSig))
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

	void MappedFile::EnsureReserve(uint32_t iBank, uint32_t nSize, uint32_t nMinFree)
	{
		while (get_Bank(iBank).m_Free < nMinFree)
		{
			// grow
			Offset n0 = m_nMapping;
			Offset n1 = AlignUp(n0, s_PageSize) + s_PageSize;

			nSize = AlignUp(nSize, sizeof(Offset));

			CloseMapping();
			Resize(n1);
			OpenMapping();

			Bank& b = get_Bank(iBank);
			Offset* p = &b.m_Tail;

			while (true)
			{
				Offset n0_ = n0 + nSize;
				if (n0_ > m_nMapping)
					break;

				assert(!*p);
				*p = n0;
				p = &get_At<Offset>(n0);

				b.m_Total++;
				b.m_Free++;

				n0 = n0_;
			}
		}
	}

	void* MappedFile::Allocate(uint32_t iBank, uint32_t nSize)
	{
		assert(nSize >= sizeof(Offset));
		EnsureReserve(iBank, nSize, 1);

		Bank& b = get_Bank(iBank);

		Offset& ret = get_At<Offset>(b.m_Tail);
		b.m_Tail = ret;
		b.m_Free--;

		return &ret;
	}

	void MappedFile::Free(uint32_t iBank, void* p)
	{
		assert(p);
		Bank& b = get_Bank(iBank);

		*((Offset*) p) = b.m_Tail;

		b.m_Tail = ((uint8_t*) p) - m_pMapping;
		assert(b.m_Tail < m_nMapping);

		b.m_Free++;
	}

} // namespace beam
