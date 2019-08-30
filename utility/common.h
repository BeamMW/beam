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

#include <assert.h>

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdelete-non-virtual-dtor"
#endif

#include <vector>

#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

#include <array>
#include <list>
#include <map>
#include <utility>
#include <cstdint>
#include <memory>
#include <functional>
#include <iostream>
#include <fstream>
#include <stdint.h>
#include <string.h> // memcmp
#include <ostream>

#ifdef WIN32
#	include <winsock2.h>
#endif // WIN32

#ifndef BEAM_VERIFY
#	ifdef  NDEBUG
#		define BEAM_VERIFY(x) ((void)(x))
#	else //  NDEBUG
#		define BEAM_VERIFY(x) assert(x)
#	endif //  NDEBUG
#endif // verify

#define IMPLEMENT_GET_PARENT_OBJ(parent_class, this_var) \
	parent_class& get_ParentObj() const { \
		parent_class*  p = (parent_class*) (((uint8_t*) this) + 1 - (uint8_t*) (&((parent_class*) 1)->this_var)); \
		assert(this == &p->this_var); /* this also tests that the variable of the correct type */ \
		return *p; \
	}

#ifndef _countof
#	define _countof(_Array) (sizeof(_Array) / sizeof(_Array[0]))
#endif // _countof

inline void memset0(void* p, size_t n) { memset(p, 0, n); }
bool memis0(const void* p, size_t n); // Not "secure", not constant-time guarantee. Must not be used for secret datas
void memxor(uint8_t* pDst, const uint8_t* pSrc, size_t n);


template <typename T>
inline void ZeroObject(T& x)
{
	memset0(&x, sizeof(x));
}

#define COMPARISON_VIA_CMP \
	template <typename T> bool operator < (const T& x) const { return cmp(x) < 0; } \
	template <typename T> bool operator > (const T& x) const { return cmp(x) > 0; } \
	template <typename T> bool operator <= (const T& x) const { return cmp(x) <= 0; } \
	template <typename T> bool operator >= (const T& x) const { return cmp(x) >= 0; } \
	template <typename T> bool operator == (const T& x) const { return cmp(x) == 0; } \
	template <typename T> bool operator != (const T& x) const { return cmp(x) != 0; }


namespace Cast
{
	template <typename T> inline T& NotConst(const T& x) { return (T&) x; }
	template <typename T> inline T* NotConst(const T* p) { return (T*) p; }

	template <typename TT, typename T> inline const TT& Up(const T& x)
	{
		const TT& ret = (const TT&) x;
		const T& unused = ret; unused;
		return ret;
	}

	template <typename TT, typename T> inline TT& Up(T& x)
	{
		TT& ret = (TT&) x;
		T& unused = ret; unused;
		return ret;
	}

	template <typename TT, typename T> inline TT* Up(T* p)
	{
		TT* ret = (TT*) p;
		T* unused = ret; unused;
		return ret;
	}

	template <typename TT, typename T> inline const TT* Up(const T* p)
	{
		const TT* ret = (const TT*) p;
		const T* unused = ret; unused;
		return ret;
	}

	template <typename TT, typename T> inline TT& Down(T& x)
	{
		return x;
	}

	template <typename TT, typename T> inline const TT& Down(const T& x)
	{
		return x;
	}
} // namespace Cast



namespace beam
{
	typedef uint64_t Timestamp;
	typedef uint64_t Height;
	typedef uint64_t Amount;
    typedef std::vector<Amount> AmountList;
	typedef std::vector<uint8_t> ByteBuffer;

	template <uint32_t nBits_>
	struct uintBig_t;

#ifdef WIN32
	std::wstring Utf8toUtf16(const char*);
	std::wstring Utf8toUtf16(const std::string&);
#endif // WIN32

	bool DeleteFile(const char*);

	struct CorruptionException
	{
		std::string m_sErr;
		// indicates critical unrecoverable corruption. Not derived from std::exception, and should not be caught in the indermediate scopes.
		// Should trigger a controlled shutdown of the app
		static void Throw(const char*);
	};

	struct Blob {
		const void* p;
		uint32_t n;

		Blob() {}
		Blob(const void* p_, uint32_t n_) :p(p_), n(n_) {}
		Blob(const ByteBuffer& bb);

		template <uint32_t nBytes_>
		Blob(const uintBig_t<nBytes_>& x) :p(x.m_pData), n(x.nBytes) {}

		void Export(ByteBuffer&) const;
	};

	template <typename T>
	struct TemporarySwap
	{
		T& m_var0;
		T& m_var1;

		TemporarySwap(T& v0, T& v1)
			:m_var0(v0)
			,m_var1(v1)
		{
			std::swap(m_var0, m_var1); // std::swap has specializations for many types that have internal swap(), such as unique_ptr, shared_ptr
		}

		~TemporarySwap()
		{
			std::swap(m_var0, m_var1);
		}
	};

	namespace Crash
	{
		void InstallHandler(const char* szLocation);

		enum Type {

			BadPtr,
			StlInvalid,
			StackOverflow,
			PureCall,
			Terminate,

			count
		};

		void Induce(Type);
	}
}

namespace std
{
	void ThrowLastError();
	void TestNoError(const ios& obj);
	void ThrowSystemError(int);

	// wrapper for std::fstream, with semantics suitable for serialization
	class FStream
	{
		std::fstream m_F;
		uint64_t m_Remaining; // used in read-stream, to indicate the EOF before trying to deserialize something

		static void NotImpl();

	public:
		FStream();
		bool Open(const char*, bool bRead, bool bStrict = false, bool bAppend = false); // strict - throw exc if error
		bool IsOpen() const { return m_F.is_open(); }
		void Close();
		uint64_t get_Remaining() const { return m_Remaining; }

		void Restart(); // for read-stream - jump to the beginning of the file
		void Seek(uint64_t);
		uint64_t Tell();

		// read/write always return the size requested. Exception is thrown if underflow or error
		size_t read(void* pPtr, size_t nSize);
		size_t write(const void* pPtr, size_t nSize);
		void Flush();

		char getch();
		char peekch() const;
		void ungetch(char);
	};
}
