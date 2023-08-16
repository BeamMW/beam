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
#include "blobmap.h"
#include "executor.h"
#include <exception>

#ifndef WIN32
#	include <unistd.h>
#	include <errno.h>
#else
#	include <dbghelp.h>
#	pragma comment (lib, "dbghelp")
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

	std::wstring Utf8toUtf16(const std::string& str)
	{
	    return Utf8toUtf16(str.c_str());
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

	void utoa(char* sz, uint32_t n)
	{
		uint32_t nDigits = 1;
		for (uint32_t x = n; ; nDigits++)
			if (!(x /= 10))
				break;

		for (sz[nDigits] = 0; nDigits--; n /= 10)
			sz[nDigits] = '0' + (n % 10);
	}

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

	int Blob::cmp(const Blob& x) const
	{
		int nRet = memcmp(p, x.p, std::min(n, x.n));
		if (nRet)
			return nRet;

		if (n < x.n)
			return -1;

		return (n > x.n);
	}

	///////////////////////
	// Executor
	thread_local Executor* Executor::s_pInstance = nullptr;

	void Executor::Context::get_Portion(uint32_t& i0, uint32_t& nCount, uint32_t nTotal)
	{
		uint32_t nThreads = m_pThis->get_Threads();
		assert(m_iThread < nThreads);

		if (nTotal)
		{
			i0 = get_Pos(nTotal, m_iThread, nThreads);
			nCount = get_Pos(nTotal, m_iThread + 1, nThreads) - i0;
		}
		else
		{
			i0 = 0;
			nCount = 0;
		}
	}

	uint32_t Executor::Context::get_Pos(uint32_t nTotal, uint32_t iThread, uint32_t nThreads)
	{
		uint64_t val = nTotal;
		val *= iThread;
		val /= nThreads;
		return static_cast<uint32_t>(val);
	}

	ExecutorMT::ExecutorMT()
	{
#if defined(EMSCRIPTEN)
		m_Threads = 2;
#else

		m_Threads = MyThread::hardware_concurrency();
#endif
		
	}

	void ExecutorMT::set_Threads(uint32_t nThreads)
	{
		Stop();
		m_Threads = nThreads;
	}

	uint32_t ExecutorMT::get_Threads()
	{
		return m_Threads;
	}

	void ExecutorMT::InitSafe()
	{
		if (!m_vThreads.empty())
			return;

		m_Run = true;
		m_pCtl = nullptr;
		m_InProgress = 0;
		m_FlushTarget = static_cast<uint32_t>(-1);

		uint32_t nThreads = get_Threads();
		m_vThreads.resize(nThreads);

		for (uint32_t i = 0; i < nThreads; i++)
			StartThread(m_vThreads[i], i);
	}

	void ExecutorMT::Push(TaskAsync::Ptr&& pTask)
	{
		assert(pTask);
		InitSafe();

		std::unique_lock<std::mutex> scope(m_Mutex);

		m_queTasks.push_back(*pTask.release());
		m_InProgress++;

		m_NewTask.notify_one();
	}

	uint32_t ExecutorMT::Flush(uint32_t nMaxTasks)
	{
		InitSafe();

		std::unique_lock<std::mutex> scope(m_Mutex);
		FlushLocked(scope, nMaxTasks);

		return m_InProgress;
	}

	void ExecutorMT::FlushLocked(std::unique_lock<std::mutex>& scope, uint32_t nMaxTasks)
	{
		m_FlushTarget = nMaxTasks;

		while (m_InProgress > nMaxTasks)
			m_Flushed.wait(scope);

		m_FlushTarget = static_cast<uint32_t>(-1);
	}

	void ExecutorMT::ExecAll(TaskSync& t)
	{
		InitSafe();

		std::unique_lock<std::mutex> scope(m_Mutex);
		FlushLocked(scope, 0);

		assert(!m_pCtl && !m_InProgress);
		m_pCtl = &t;
		m_InProgress = get_Threads();

		m_NewTask.notify_all();

		FlushLocked(scope, 0);
		assert(!m_pCtl);
	}

	void ExecutorMT::Stop()
	{
		if (m_vThreads.empty())
			return;

		{
			std::unique_lock<std::mutex> scope(m_Mutex);
			m_Run = false;
			m_NewTask.notify_all();
		}

		for (size_t i = 0; i < m_vThreads.size(); i++)
			if (m_vThreads[i].joinable())
				m_vThreads[i].join();

		m_vThreads.clear();

		while (!m_queTasks.empty())
		{
			TaskAsync::Ptr pGuard(&m_queTasks.front());
			m_queTasks.pop_front();
		}
	}

	void ExecutorMT::RunThreadCtx(Context& ctx)
	{
		ctx.m_pThis = this;

		while (true)
		{
			TaskAsync::Ptr pGuard;
			TaskSync* pTask;

			{
				std::unique_lock<std::mutex> scope(m_Mutex);
				while (true)
				{
					if (!m_Run)
						return;

					if (!m_queTasks.empty())
					{
						pGuard.reset(&m_queTasks.front());
						pTask = pGuard.get();
						m_queTasks.pop_front();
						break;
					}

					if (m_pCtl)
					{
						pTask = m_pCtl;
						break;
					}

					m_NewTask.wait(scope);
				}
			}

			assert(pTask && m_InProgress);
			pTask->Exec(ctx);

			std::unique_lock<std::mutex> scope(m_Mutex);

			assert(m_InProgress);
			m_InProgress--;

			if (pGuard)
			{
				// standard task
				if (m_InProgress == m_FlushTarget)
					m_Flushed.notify_one();
			}
			else
			{
				// control task
				if (m_InProgress)
					m_Flushed.wait(scope); // make sure we give other threads opportuinty to execute the control task
				else
				{
					m_pCtl = nullptr;
					m_Flushed.notify_all();
				}
			}

		}
	}

	///////////////////////
	// BlobMap
	BlobMap::Entry* BlobMap::Set::Find(const Blob& key)
	{
		auto it = find(key, Comparator());
		return (end() == it) ? nullptr : &*it;
	}

	BlobMap::Entry* BlobMap::Set::Create(const Blob& key)
	{
		Entry* pItem = new (key.n) Entry;
		pItem->m_Size = key.n;
		memcpy(pItem->m_pBuf, key.p, key.n);

		insert(*pItem);
		return pItem;
	}

	BlobMap::Entry* BlobMap::Set::FindVarEx(const Blob& key, bool bExact, bool bBigger)
	{
		auto it = lower_bound(key, BlobMap::Set::Comparator());

		if (end() == it)
		{
			// all elements are smaller than the key
			if (bBigger)
				return nullptr;

			auto it2 = rbegin();
			if (rend() == it2)
				return nullptr;

			return &(*it2);
		}

		assert(it->ToBlob() >= key);

		if (bExact)
		{
			if (bBigger || (it->ToBlob() == key))
				return &(*it); // ok
		}

		if (bBigger)
		{
			assert(!bExact);
			if (it->ToBlob() == key)
			{
				++it;
				if (end() == it)
					return nullptr;
			}
		}
		else
		{
			if (begin() == it)
				return nullptr;
			--it;

		}

		return &(*it);
	}


	///////////////////////
	// Checkpoint

	thread_local Exc::Checkpoint* Exc::Checkpoint::s_pTop = nullptr;

	Exc::Checkpoint::Checkpoint()
	{
		m_pNext = s_pTop;
		s_pTop = this;
	}

	Exc::Checkpoint::~Checkpoint()
	{
		s_pTop = m_pNext;
	}

	uint32_t Exc::Checkpoint::DumpAll(std::ostream& os)
	{
		uint32_t ret = 0;
		for (Checkpoint* p = s_pTop; p; p = p->m_pNext)
		{
			os << " <- ";
			p->Dump(os);

			if (!ret)
				ret = p->get_Type();
		}
		return ret;
	}

	void Exc::CheckpointTxt::Dump(std::ostream& os) {
		os << m_sz;
	}

	void Exc::Fail()
	{
		Fail("Error");
	}

	void Exc::Fail(const char* sz)
	{
		std::ostringstream os;
		os << sz << ": ";

		uint32_t nType = Checkpoint::DumpAll(os);

		Exc exc(os.str());
		exc.m_Type = nType;

		throw exc;
	}

} // namespace beam

namespace std
{
	void ThrowLastError()
	{
#ifdef WIN32
		LPSTR buffer;
		auto error = GetLastError();
		auto count = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS
			, nullptr
			, error
			, LocaleNameToLCID(L"en-US", 0)
			, (LPSTR)&buffer
			, 0
			, nullptr);
		if (count)
		{
			std::string message(buffer, buffer + count);
			::LocalFree(&buffer);
			throw runtime_error(message);
		}
		ThrowSystemError(error);
#else // WIN32
		throw runtime_error(strerror(errno));
#endif // WIN32
	}

	void ThrowSystemError(int nErrorCode)
	{
		char sz[0x20];
		snprintf(sz, _countof(sz), "System Error=%d", nErrorCode);
		throw runtime_error(sz);
	}

	void TestNoError(const ios& obj)
	{
		if (obj.fail())
			ThrowLastError();
	}

	FStream::FStream()
		:m_Remaining(0)
	{
	}

	bool FStream::Open(const char* sz, bool bRead, bool bStrict /* = false */, bool bAppend /* = false */)
	{
		m_Remaining = 0;

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
				ThrowLastError();
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
		{
			m_F.close();
			m_Remaining = 0;
		}
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

	void FStream::ensure_size(size_t s)
	{
		if (s > m_Remaining)
			throw runtime_error("underflow");
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

	uint64_t FStream::Tell()
	{
		if (!IsOpen())
			return 0;

		iostream::pos_type ret = m_F.tellg();
		if (iostream::pos_type(-1) == ret)
			ThrowLastError();

		return ret;
	}

} // namespace std

#if defined(BEAM_USE_STATIC)

#if defined(_MSC_VER) && (_MSC_VER >= 1900)

FILE _iob[] = { *stdin, *stdout, *stderr };
extern "C" FILE * __cdecl __iob_func(void) { return _iob; }

#endif

#endif

#ifdef WIN32

wchar_t g_szDumpPathTemplate[MAX_PATH];
uint32_t g_DumpIdx = 0;

void MiniDumpWriteGuarded(EXCEPTION_POINTERS* pExc)
{
	HANDLE hFile;

	wchar_t szPath[MAX_PATH];
	for ( ; ; g_DumpIdx++)
	{
		_snwprintf_s(szPath, _countof(szPath), _countof(szPath), L"%s%u.dmp", g_szDumpPathTemplate, g_DumpIdx);
		szPath[_countof(szPath) - 1] = 0; // for more safety

		hFile = CreateFileW(szPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (INVALID_HANDLE_VALUE != hFile)
			break; // ok

		if (GetLastError() != ERROR_FILE_EXISTS)
			return; // oops!
	}

	MINIDUMP_EXCEPTION_INFORMATION mdei = { 0 };
	mdei.ThreadId = GetCurrentThreadId();
	mdei.ExceptionPointers = pExc;

	MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mdei, NULL, NULL);

    BEAM_VERIFY(CloseHandle(hFile));

}

void MiniDumpWriteWrap(EXCEPTION_POINTERS* pExc)
{
	__try {
		MiniDumpWriteGuarded(pExc);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
}


void RaiseCustumExc()
{
	RaiseException(0xC20A1000, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

void MiniDumpWriteNoExc()
{
	__try {
		RaiseCustumExc();
	} __except (MiniDumpWriteGuarded(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) {
	}
}

DWORD WINAPI MiniDumpWriteInThread(PVOID pPtr)
{
	MiniDumpWriteWrap((EXCEPTION_POINTERS*) pPtr);
	return 0;
}

long WINAPI ExcFilter(EXCEPTION_POINTERS* pExc)
{
	switch (pExc->ExceptionRecord->ExceptionCode)
	{
	case STATUS_STACK_OVERFLOW:
		{
			DWORD dwThreadID;
			HANDLE hThread = CreateThread(NULL, 0, MiniDumpWriteInThread, pExc, 0, &dwThreadID);
			if (hThread)
			{
				WaitForSingleObject(hThread, INFINITE);
				CloseHandle(hThread);
			}
		}
		break;

	default:
		MiniDumpWriteWrap(pExc);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

//void CrtInvHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
//{
//	if (IsDebuggerPresent())
//		/*_invalid_parameter_handler(expression, function, file, line, pReserved)*/;
//	else
//		MiniDumpWriteNoExc();
//}

terminate_function g_pfnTerminate = NULL;

void TerminateHandler()
{
	MiniDumpWriteNoExc();
	g_pfnTerminate();
}

//_CRT_REPORT_HOOK g_pfnCrtReport = NULL;

int CrtReportHook(int n, char* sz, int* p)
{
	MiniDumpWriteNoExc();
	return 0;
}

void PureCallHandler()
{
	RaiseCustumExc(); // convert it to regular exc
}

void beam::Crash::InstallHandler(const char* szLocation)
{
	if (szLocation)
	{
		std::wstring s = beam::Utf8toUtf16(szLocation);
		size_t nLen = s.size();
		if (nLen >= _countof(g_szDumpPathTemplate))
			nLen = _countof(g_szDumpPathTemplate) - 1;

		memcpy(g_szDumpPathTemplate, s.c_str(), sizeof(wchar_t) * (nLen + 1));
	}
	else
	{
		GetModuleFileNameW(NULL, g_szDumpPathTemplate, _countof(g_szDumpPathTemplate));
		g_szDumpPathTemplate[_countof(g_szDumpPathTemplate) - 1] = 0;
	}

	SetUnhandledExceptionFilter(ExcFilter);

	// CRT-specific
	//_set_invalid_parameter_handler(CrtInvHandler);
	g_pfnTerminate = set_terminate(TerminateHandler);
	_CrtSetReportHook(CrtReportHook);
	_set_purecall_handler(PureCallHandler);
}

#else // WIN32

void beam::Crash::InstallHandler(const char*)
{
}

#endif // WIN32

void beam::Crash::Induce(Type type)
{
	switch (type)
	{
	case StlInvalid:
		// will invoke handler in checked version. Otherwise will just crash normally
		{
			std::vector<int> vv;
			vv[4] = 0;
		}
		break;

	case StackOverflow:
		{
			struct StackOverflow
			{
				// this is tricky: we need to prevent optimization of the buffer, and confuse the compiler and convience it that this code "might" work
				uint8_t m_pArr[0x400];
				uint8_t Do(uint8_t n)
				{
					m_pArr[0] = n ^ 1;

					if (n)
					{
						StackOverflow v;
						v.Do(n ^ 1);
						memxor(m_pArr, v.m_pArr, sizeof(m_pArr));
					}

					for (size_t i = 0; i < _countof(m_pArr); i++)
						n ^= m_pArr[i];

					return n;
				}
			};

			StackOverflow v;
			size_t val = v.Do(7);

			// make sure the retval is really needed, though this code shouldn't be reached
			volatile int* p = reinterpret_cast<int*>(val);
			*p = 0;

		}
		break;

	case PureCall:
		{
			struct Base {
				Base* m_pOther;

				~Base() {
					m_pOther->Func();
				}

				virtual void Func() = 0;
			};

			struct Derived :public Base {
				virtual void Func() override {}
			};

			Derived d;
			d.m_pOther = &d;
		}
		break;

	case Terminate:
		std::terminate();
		break;

	default:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
		// default crash
		*reinterpret_cast<int*>(0x48) = 15;
#pragma GCC diagnostic pop
	}
}
