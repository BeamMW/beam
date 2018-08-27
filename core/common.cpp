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

#include <utility> // std::swap
#include <algorithm>
#include <ctime>
#include "block_crypt.h"
#include "storage.h"
#include "../utility/serialize.h"
#include "../core/serialization_adapters.h"

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

	bool FStream::Open(const char* sz, bool bRead, bool bStrict /* = false */)
	{
		int mode = ios_base::binary;
		mode |= (bRead ? (ios_base::in | ios_base::ate) : (ios_base::out | ios_base::trunc));

		m_F.open(sz, (ios_base::openmode) mode);
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

	bool FStream::IsDataRemaining() const
	{
		return m_Remaining > 0;
	}

	void FStream::Restart()
	{
		m_Remaining += m_F.tellg();
		m_F.seekg(0);
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
		return 0;
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
