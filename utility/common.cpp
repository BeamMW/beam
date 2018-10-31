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

#include "common.h"

#ifndef WIN32
#	include <unistd.h>
#	include <errno.h>
#endif // WIN32

// misc
bool memis0(const void* p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (((const uint8_t*)p)[i])
			return false;
	return true;
}

void memxor(uint8_t* pDst, const uint8_t* pSrc, size_t n)
{
	for (size_t i = 0; i < n; i++)
		pDst[i] ^= pSrc[i];
}

namespace beam
{

#ifdef WIN32

	std::wstring Utf8toUtf16(const char* sz)
	{
		std::wstring sRet;

		int nVal = MultiByteToWideChar(CP_UTF8, 0, sz, -1, NULL, 0);
		if (nVal > 1)
		{
			sRet.resize(nVal - 1);
			MultiByteToWideChar(CP_UTF8, 0, sz, -1, sRet.data(), nVal);
		}

		return sRet;
	}

	bool DeleteFile(const char* sz)
	{
		return ::DeleteFileW(Utf8toUtf16(sz).c_str()) != FALSE;
	}

#else // WIN32

	bool DeleteFile(const char* sz)
	{
		return !unlink(sz);
	}


#endif // WIN32

	Blob::Blob(const ByteBuffer& bb)
	{
		if ((n = (uint32_t)bb.size()) != 0)
			p = &bb.at(0);
	}

	void Blob::Export(ByteBuffer& x) const
	{
		if (n)
		{
			x.resize(n);
			memcpy(&x.at(0), p, n);
		}
		else
			x.clear();
	}
}

namespace std
{
	void ThrowIoError()
	{
#ifdef WIN32
		int nErrorCode = GetLastError();
#else // WIN32
		int nErrorCode = errno;
#endif // WIN32

		char sz[0x20];
		snprintf(sz, _countof(sz), "I/O Error=%d", nErrorCode);
		throw runtime_error(sz);
	}

	void TestNoError(const ios& obj)
	{
		if (obj.fail())
			ThrowIoError();
	}

	bool FStream::Open(const char* sz, bool bRead, bool bStrict /* = false */, bool bAppend /* = false */)
	{
		int mode = ios_base::binary;
		mode |= bRead ? ios_base::ate : bAppend ? ios_base::app : ios_base::trunc;
		mode |= bRead ? ios_base::in : ios_base::out;

#ifdef WIN32
		std::wstring sPathArg = beam::Utf8toUtf16(sz);
#else // WIN32
		const char* sPathArg = sz;
#endif // WIN32

		m_F.open(sPathArg, (ios_base::openmode) mode);

		if (m_F.fail())
		{
			if (bStrict)
				ThrowIoError();
			return false;
		}

		if (bRead)
		{
			m_Remaining = m_F.tellg();
			m_F.seekg(0);
		}

		return true;
	}

	void FStream::Close()
	{
		if (m_F.is_open())
			m_F.close();
	}

	void FStream::Restart()
	{
		m_Remaining += m_F.tellg();
		m_F.seekg(0);
	}

	void FStream::Seek(uint64_t n)
	{
		m_Remaining += m_F.tellg();
		m_F.seekg(n);
		m_Remaining -= m_F.tellg();
	}

	void FStream::NotImpl()
	{
		throw runtime_error("not impl");
	}

	size_t FStream::read(void* pPtr, size_t nSize)
	{
		m_F.read((char*)pPtr, nSize);
		size_t ret = m_F.gcount();
		m_Remaining -= ret;

		if (ret != nSize)
			throw runtime_error("underflow");

		return ret;
	}

	size_t FStream::write(const void* pPtr, size_t nSize)
	{
		m_F.write((char*) pPtr, nSize);
		TestNoError(m_F);

		return nSize;
	}

	char FStream::getch()
	{
		char ch;
		read(&ch, 1);
		return ch;
	}

	char FStream::peekch() const
	{
		NotImpl();
#if !(defined(_MSC_VER) && defined(NDEBUG))
        return 0;
#endif
	}

	void FStream::ungetch(char)
	{
		NotImpl();
	}

	void FStream::Flush()
	{
		m_F.flush();
		TestNoError(m_F);
	}

} // namespace std

#if defined(BEAM_USE_STATIC)

#if defined(_MSC_VER) && (_MSC_VER >= 1900)

FILE _iob[] = { *stdin, *stdout, *stderr };
extern "C" FILE * __cdecl __iob_func(void) { return _iob; }

#endif

#endif