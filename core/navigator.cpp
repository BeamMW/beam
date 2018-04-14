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
	void test_SysRet(bool bFail)
	{
		if (bFail)
		{
#ifdef WIN32
			int nErrorCode = GetLastError();
#else // WIN32
			int nErrorCode = errno;
#endif // WIN32

			char sz[0x20];
			sprintf(sz, "Error=%d", nErrorCode);
			throw std::runtime_error(sz);
		}
	}

	template <typename T>
	T AlignUp(T x, uint32_t nAlignment)
	{
		uint32_t msk = nAlignment - 1;
		assert(!(msk & nAlignment)); // alignment must be power-2
		return (x + msk) & ~msk;
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

		test_SysRet(!GetFileSizeEx(m_hFile, (LARGE_INTEGER*) &m_nMapping));
		if (m_nMapping)
		{
			m_hMapping = CreateFileMapping(m_hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
			test_SysRet(!m_hMapping);

			m_pMapping = (uint8_t*) MapViewOfFile(m_hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, (size_t) m_nMapping);
			test_SysRet(!m_pMapping);
		}

#else // WIN32

		struct stat stats;
		test_SysRet(fstat(m_hFile, &stats) != 0);
		m_nMapping = stats.st_size;

		if (m_nMapping)
		{
			uint8_t* pPtr = (uint8_t*) mmap(NULL, m_nMapping, PROT_READ | PROT_WRITE, MAP_SHARED, m_hFile, 0);
			test_SysRet(MAP_FAILED == pPtr);

			m_pMapping = pPtr;
		}

#endif // WIN32
	}

	bool MappedFile::TestSig(const uint8_t* pSig, unsigned int nSizeSig)
	{
		if ((nSizeSig > m_nMapping) || memcmp(m_pMapping, pSig, nSizeSig))
			return false;

		m_nBank0 = AlignUp(nSizeSig, sizeof(Offset));
		if (m_nBank0 + m_nBanks * sizeof(Bank) > m_nMapping)
			return false;

		return true;
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
		test_SysRet(!SetFilePointerEx(m_hFile, (const LARGE_INTEGER&) n, NULL, FILE_BEGIN));
		test_SysRet(!SetEndOfFile(m_hFile));
#else // WIN32
		test_SysRet(ftruncate(m_hFile, n) != 0);
#endif // WIN32
	}

	void MappedFile::Open(const char* sz, const uint8_t* pSig, uint32_t nSizeSig, uint32_t nBanks)
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
		test_SysRet(INVALID_HANDLE_VALUE == m_hFile);
#else // WIN32
		m_hFile = open(sz, O_RDWR | O_CREAT);
		test_SysRet(-1 == m_hFile);
#endif // WIN32

		m_nBanks = nBanks;

		OpenMapping();

		if (!TestSig(pSig, nSizeSig))
		{
			CloseMapping();

			if (m_nMapping > nSizeSig)
				Resize(nSizeSig); // will zero-init all this data

			m_nBank0 = AlignUp(nSizeSig, sizeof(Offset));
			Resize(m_nBank0 + sizeof(Bank) * m_nBanks);

			OpenMapping();
			memcpy(m_pMapping, pSig, nSizeSig);
		}
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
				p = (Offset*) (m_pMapping + n0);

				n0 = n0_;
			}
		}

		Offset* pRet = (Offset*) (m_pMapping + pBank->m_Tail);
		pBank->m_Tail = *pRet;

		return pRet;
	}

	void MappedFile::Free(uint32_t iBank, void* p)
	{
		assert(p);
		Bank& b = get_Bank(iBank);

		*((Offset*) p) = b.m_Tail;

		b.m_Tail = ((uint8_t*) p) - m_pMapping;
		assert(b.m_Tail < m_nMapping);
	}


} // namespace beam
