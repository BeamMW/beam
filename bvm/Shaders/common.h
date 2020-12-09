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

#pragma pack (pop)

typedef Opaque<33> PubKey;
typedef Opaque<32> ContractID;
typedef Opaque<32> ShaderID;
typedef Opaque<32> HashValue;

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
    inline bool LoadVar_T(const TKey& key, TVal& val)
    {
        return LoadVar(&key, sizeof(key), &val, sizeof(val)) == sizeof(val);
    }

    template <typename TKey, typename TVal>
    inline void SaveVar_T(const TKey& key, const TVal& val)
    {
        SaveVar(&key, sizeof(key), &val, sizeof(val));
    }

    template <typename TKey>
    inline void DelVar_T(const TKey& key)
    {
        SaveVar(&key, sizeof(key), nullptr, 0);
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
    void CallFar_T(const ContractID& cid, T& args)
    {
        CallFar(cid, args.s_iMethod, &args);
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

} // namespace Env

namespace Utils {

    template <typename T>
    inline void SetObject(T& x, uint8_t nFill) {
        Env::Memset(&x, nFill, sizeof(x));
    }

    template <typename T>
    inline void ZeroObject(T& x) {
        SetObject(x, 0);
    }

    template <typename TDst, typename TSrc>
    inline void Copy(TDst& dst, const TSrc& src)
    {
        static_assert(sizeof(dst) == sizeof(src), "operands must have equal size");
        Env::Memcpy(&dst, &src, sizeof(dst));
    }

    template <typename TSrc0, typename TSrc1>
    inline int32_t Cmp(const TSrc0& src0, const TSrc1& src1)
    {
        static_assert(sizeof(src0) == sizeof(src1), "operands must have equal size");
        return Env::Memcmp(&src0, &src1, sizeof(src0));
    }

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
    inline uint16_t FromBE(uint16_t x) {
        return __builtin_bswap16(x);
    }

    inline uint32_t FromBE(uint32_t x) {
        return __builtin_bswap32(x);
    }

    inline uint64_t FromBE(uint64_t x) {
        return __builtin_bswap64(x);
    }

    inline uint32_t FromLE(uint32_t x) {
        return x;
    }
#endif // HOST_BUILD

} // namespace Utils

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
}
