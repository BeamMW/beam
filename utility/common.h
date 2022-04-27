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
#include <sstream>

#ifdef WIN32
#	include <WinSock2.h>
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

// it should be used in cases when we are realy sure that zeroing memory is safe, but compiler is disagreed
template <typename T>
inline void ZeroObjectUnchecked(T& x)
{
	memset0(&x, sizeof(x));
}

template <typename T>
inline void ZeroObject(T& x)
{
	// TODO: uncomment and fix
	//static_assert(std::is_standard_layout_v<T>);
	static_assert(std::is_trivially_destructible_v<T>);
	ZeroObjectUnchecked(x);
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
		[[maybe_unused]] T& unused = ret;
		return ret;
	}

	template <typename TT, typename T> inline TT* Up(T* p)
	{
		TT* ret = (TT*) p;
		[[maybe_unused]] T* unused = ret;
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

	template <typename TT, typename T> inline TT& Reinterpret(T& x)
	{
		// type are unrelated. But must have the same size
		static_assert(sizeof(TT) == sizeof(T));
		return (TT&)x;
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

	// shame, utoa, ultoa - non-standard!
	void utoa(char* sz, uint32_t n);

	struct CorruptionException
	{
		std::string m_sErr;
		// indicates critical unrecoverable corruption. Not derived from std::exception, and should not be caught in the indermediate scopes.
		// Should trigger a controlled shutdown of the app
		static void Throw(const char*);
	};

	struct Blob
	{
		const void* p = nullptr;
		uint32_t n = 0;

		Blob() = default;
		Blob(const void* p_, uint32_t n_) :p(p_), n(n_) {}
		Blob(const ByteBuffer& bb);
		template <size_t nBytes_>
		Blob(const std::array<uint8_t, nBytes_>& x) : p(x.data()), n(static_cast<uint32_t>(x.size())) {}


		template <uint32_t nBytes_>
		Blob(const uintBig_t<nBytes_>& x) :p(x.m_pData), n(x.nBytes) {}

		void Export(ByteBuffer&) const;

		int cmp(const Blob&) const;
		COMPARISON_VIA_CMP
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

	// compile-time calculation of x^n
	template <uint32_t n> struct Power {
		template <uint32_t x> struct Of {
			static const uint32_t V = Power<n - 1>::template Of<x>::V * x;
		};
	};

	template <> struct Power<0> {
		template <uint32_t x> struct Of {
			static const uint32_t V = 1;
		};
	};
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

		void ensure_size(size_t s);
		// read/write always return the size requested. Exception is thrown if underflow or error
		size_t read(void* pPtr, size_t nSize);
		size_t write(const void* pPtr, size_t nSize);
		void Flush();

		char getch();
		char peekch() const;
		void ungetch(char);
	};

	// for the following: receive the 2nd parameter by value, not by const reference. Otherwise could be linker error with static integral constants
	template <typename TDst, typename TSrc>
	inline void setmax(TDst& a, TSrc b) {
		if (a < b)
			a = b;
	}

	template <typename TDst, typename TSrc>
	inline void setmin(TDst& a, TSrc b) {
		if (a > b)
			a = b;
	}
}
