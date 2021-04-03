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

#ifndef HOST_BUILD

// Common ord types
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed int int32_t;
typedef signed short int16_t;
typedef signed long long int64_t;

typedef uint64_t Height;
typedef uint64_t Amount;
typedef uint32_t AssetID;
typedef uint64_t Timestamp;

#pragma pack (push, 1)

template <uint32_t nBytes>
struct Opaque {
    uint8_t m_p[nBytes];
};

struct Secp_point_data {
    Opaque<32> X;
    uint8_t Y;
};

#pragma pack (pop)

struct HeightPos
{
    Height m_Height;
    uint32_t m_Pos;
};

typedef Secp_point_data PubKey;
typedef Opaque<32> ContractID;
typedef Opaque<32> ShaderID;
typedef Opaque<32> HashValue;
typedef Opaque<32> Secp_scalar_data;

template <bool bToShader, typename T>
inline void ConvertOrd(T&) {}

#ifndef _countof
#   define _countof(x) (sizeof(x) / sizeof((x)[0]))
#endif // _countof

#   define export __attribute__( ( visibility( "default" ) ) ) extern "C"

#ifndef assert
#   define assert(expr) do {} while (false)
#endif // assert

#endif // HOST_BUILD

// environment functions
#include "../bvm2_shared.h"
#include "../bvm2_opcodes.h"

namespace Env {

    extern "C" {

#define PAR_DECL(type, name) type name
#define COMMA ,
#define THE_MACRO(id, ret, name) \
        ret name(BVMOp_##name(PAR_DECL, COMMA));

        BVMOpsAll_Common(THE_MACRO)
        BVMOpsAll_Contract(THE_MACRO)
        BVMOpsAll_Manager(THE_MACRO)

#undef THE_MACRO
#undef COMMA
#undef PAR_DECL
    } // extern "C"

    // Template wrapper that returns true if the size of loaded variables matches the provided parameter and false otherwise
    template <typename TKey, typename TVal>
    inline bool LoadVar_T(const TKey& key, TVal& val, uint8_t nType = KeyTag::Internal)
    {
        return LoadVar(&key, sizeof(key), &val, sizeof(val), nType) == sizeof(val);
    }

    template <typename TKey, typename TVal>
    inline bool SaveVar_T(const TKey& key, const TVal& val, uint8_t nType = KeyTag::Internal)
    {
        return SaveVar(&key, sizeof(key), &val, sizeof(val), nType) == sizeof(val);
    }

    template <typename TKey>
    inline bool DelVar_T(const TKey& key, uint8_t nType = KeyTag::Internal)
    {
        return SaveVar(&key, sizeof(key), nullptr, 0, nType) != 0;
    }

    inline void Halt_if(bool b)
    {
        if (b)
            Halt();
    }

    template <typename T>
    inline T* StackAlloc_T(uint32_t n) {
        return (T*) StackAlloc(sizeof(T) * n);
    }

    template <typename T>
    inline T* StackFree_T(uint32_t n) {
        // not mandatory to call, but sometimes usefull before calling other heavy functions
        return StackFree(sizeof(T) * n);
    }

#ifndef HOST_BUILD
    template <typename T>
    void CallFar_T(const ContractID& cid, T& args, uint8_t bInheritContext = 0)
    {
        CallFar(cid, args.s_iMethod, &args, sizeof(args), bInheritContext);
    }
#endif // HOST_BUILD

    struct DocArray {
        DocArray(const char* sz) { DocAddArray(sz); }
        ~DocArray() { DocCloseArray(); }
    };

    struct DocGroup {
        DocGroup(const char* sz) { DocAddGroup(sz); }
        ~DocGroup() { DocCloseGroup(); }
    };

    inline void DocAddNum(const char* szID, uint32_t val) {
        DocAddNum32(szID, val);
    }
    inline void DocAddNum(const char* szID, uint64_t val) {
        DocAddNum64(szID, val);
    }
    template <typename T>
    inline void DocAddBlob_T(const char* szID, const T& val) {
        DocAddBlob(szID, &val, sizeof(val));
    }

    inline bool DocGet(const char* szID, uint32_t& val) {
        val = 0;
        return DocGetNum32(szID, &val) == sizeof(val);
    }
    inline bool DocGet(const char* szID, uint64_t& val) {
        val = 0;
        return DocGetNum64(szID, &val) == sizeof(val);
    }
    inline bool DocGetBlobEx(const char* szID, void* p, uint32_t n)
    {
        if (DocGetBlob(szID, p, n) == n)
            return true;

        Memset(p, 0, n);
        return false;
    }
    inline bool DocGet(const char* szID, ContractID& val) {
        return DocGetBlobEx(szID, &val, sizeof(val));
    }
    inline bool DocGet(const char* szID, PubKey& val) {
        return DocGetBlobEx(szID, &val, sizeof(val));
    }

    // variable enum/read wrappers
#pragma pack (push, 1)
    struct KeyPrefix
    {
        ContractID m_Cid;
        uint8_t m_Tag = KeyTag::Internal; // used to differentiate between keys used by the virtual machine and those used by the contract
    };

    template <typename T>
    struct Key_T
    {
        KeyPrefix m_Prefix;
        T m_KeyInContract;
    };

    template <typename T>
    struct Log_T
    {
        ContractID m_Cid;
        T m_Key;
    };
#pragma pack (pop)

    template <typename TKey, typename TValue>
    inline bool VarsMoveNext_T(const TKey*& pKey, const TValue*& pValue)
    {
        while (true)
        {
            uint32_t nKey, nVal;
            if (!VarsMoveNext((const void**) &pKey, &nKey, (const void**) &pValue, &nVal))
                return false;

            if ((sizeof(TKey) == nKey) && (sizeof(TValue) == nVal))
                break;
        }

        return true;
    }

    template <typename TKey0, typename TKey1>
    inline void VarsEnum_T(const TKey0& key0, const TKey1& key1)
    {
        VarsEnum(&key0, sizeof(key0), &key1, sizeof(key1));
    }

    template <typename TKey, typename TValue>
    inline bool VarRead_T(const TKey& key, const TValue*& pValue)
    {
        VarsEnum_T(key, key);

        const TKey* pKey;
        return VarsMoveNext_T(pKey, pValue);
    }

    template <typename TValue, typename TKey>
    inline const TValue* VarRead_T(const TKey& key)
    {
        const TValue* pValue;
        return VarRead_T(key, pValue) ? pValue : nullptr;

    }

    template <typename TKey, typename TValue>
    inline bool LogsMoveNext_T(const TKey*& pKey, const TValue*& pValue, HeightPos& pos)
    {
        while (true)
        {
            uint32_t nKey, nVal;
            if (!LogsMoveNext((const void**) &pKey, &nKey, (const void**) &pValue, &nVal, &pos))
                return false;

            if ((sizeof(TKey) == nKey) && (sizeof(TValue) == nVal))
                break;
        }

        return true;
    }

} // namespace Env

namespace Utils {

    template <typename T> struct BlobOf
    {
        T& m_Obj;
        explicit BlobOf(T& x) :m_Obj(x) {}

        template <typename TT> static void TestOperand(const TT&) {
            static_assert(sizeof(T) == sizeof(TT), "operands must have equal size");
        }
        template <typename TT> const BlobOf& operator = (const TT& v) const {
            TestOperand(v);
            Env::Memcpy(&m_Obj, &v, sizeof(T));
            return *this;
        }
        template <typename TT> int32_t Cmp(const TT& v) const {
            TestOperand(v);
            return Env::Memcmp(&m_Obj, &v, sizeof(T));
        }
        template <typename TT> uint8_t operator == (const TT& v) const {
            return !Cmp(v);
        }
        template <typename TT> uint8_t operator != (const TT& v) const {
            return !!Cmp(v);
        }
        void SetObject(uint8_t nFill) const {
            Env::Memset(&m_Obj, nFill, sizeof(T));
        }
        void SetZero() const {
            SetObject(0);
        }
        uint8_t IsZero() const {
            return Env::Memis0(&m_Obj, sizeof(T));
        }
    };

#ifdef HOST_BUILD

    template <typename T>
    inline T FromBE(T x) {
        return beam::ByteOrder::from_be(x);
    }

    template <typename T>
    inline T FromLE(T x) {
        return beam::ByteOrder::from_le(x);
    }

#else // HOST_BUILD
    inline uint8_t FromBE(uint8_t x) {
        return x;
    }

    inline uint16_t FromBE(uint16_t x) {
        return __builtin_bswap16(x);
    }

    inline uint32_t FromBE(uint32_t x) {
        return __builtin_bswap32(x);
    }

    inline uint64_t FromBE(uint64_t x) {
        return __builtin_bswap64(x);
    }

    template <typename T>
    inline T FromLE(T x) {
        return x;
    }
#endif // HOST_BUILD

} // namespace Utils

template <typename T> Utils::BlobOf<T> _POD_(T& x) {
    return Utils::BlobOf<T>(x);
}

namespace std {
    
    template <typename T>
    T&& move(T& x) {
        return (T&&) x;
    }

    template <typename T>
    void swap(T& a, T& b) {
        T tmp(move(a));
        a = move(b);
        b = move(tmp);
    }

    template <typename T> const T& min(const T& a, const T& b) {
        return (a <= b) ? a : b;
    }
    template <typename T> const T& max(const T& a, const T& b) {
        return (a >= b) ? a : b;
    }
}

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

	template <typename TT, typename T> inline TT& Reinterpret(T& x)
	{
		// type are unrelated. But must have the same size
		static_assert(sizeof(TT) == sizeof(T));
		return (TT&)x;
	}

} // namespace Cast

struct HashProcessor
{
    struct Base
    {
        HashObj* m_p;

        Base() :m_p(nullptr) {}

        ~Base()
        {
            if (m_p)
                Env::HashFree(m_p);
        }

        template <typename T>
        Base& operator << (const T& x)
        {
            Write(x);
            return *this;
        }

        void Write(uint8_t x) {
            Env::HashWrite(m_p, &x, sizeof(x));
        }

        template <uint32_t n>
        void Write(const char(&sz)[n])
        {
            Write(sz, n);
        }


        template <typename T>
        void Write(T v)
        {
            // Must be independent of the endian-ness
            // Must prevent ambiguities (different inputs should be properly distinguished)
            // Make it also independent of the actual type width, so that size_t (and friends) will be treated the same on all the platforms
            static_assert(T(-1) > 0, "must be unsigned");

            uint8_t pBuf[sizeof(T) * 8 / 7 + 1];
            uint32_t n = 0;

            for (; v >= 0x80; v >>= 7)
                pBuf[n++] = uint8_t(v) | 0x80;

            pBuf[n] = uint8_t(v);
            Write(pBuf, n + 1);
        }

        void Write(const void* p, uint32_t n)
        {
            Env::HashWrite(m_p, p, n);
        }

        void Write(const HashValue& hv)
        {
            Write(&hv, sizeof(hv));
        }

        void Write(const Secp_point_data& pd)
        {
            Write(&pd, sizeof(pd));
        }

        template <typename T>
        void operator >> (T& res)
        {
            Env::HashGetValue(m_p, &res, sizeof(res));
        }
    };

    struct Sha256
        :public Base
    {
        Sha256() {
            m_p = Env::HashCreateSha256();
        }
    };

    struct Blake2b
        :public Base
    {
        Blake2b(const void* pPersonal, uint32_t nPersonal, uint32_t nResultSize) {
            m_p = Env::HashCreateBlake2b(pPersonal, nPersonal, nResultSize);
        }
    };
};

namespace Merkle
{
    inline void Interpret(HashValue& hv, const HashValue& hvL, const HashValue& hvR)
    {
        HashProcessor::Sha256 hp;
        hp << hvL << hvR >> hv;
    }

    inline void Interpret(HashValue& hv, const HashValue& hvNew, bool bOnRight)
    {
        if (bOnRight)
            Interpret(hv, hv, hvNew);
        else
            Interpret(hv, hvNew, hv);
    }

    inline void Interpret(HashValue& hv, const Node& n)
    {
        Interpret(hv, n.m_Value, n.m_OnRight);
    }

    inline void Interpret(HashValue& hv, const Node* pN, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++)
            Interpret(hv, pN[i]);
    }

    inline void get_ContractVarHash(HashValue& hv, const ContractID& cid, uint8_t nKeyTag, const void* pKey, uint32_t nKey, const void* pVal, uint32_t nVal)
    {
        HashProcessor::Sha256 hp;

        uint32_t nSizeKey = sizeof(cid) + 1 + nKey;

        hp
            << "beam.contract.val"
            << nSizeKey;

        hp.Write(cid);
        hp.Write(nKeyTag);
        hp.Write(pKey, nKey);
        hp.Write(nVal);
        hp.Write(pVal, nVal);

        hp >> hv;
    }
}
