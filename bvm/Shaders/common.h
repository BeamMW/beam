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

#ifndef _MSC_VER
#pragma clang section bss = "disable_bss_for_global_zeroinitialized_vars_use_standard_data_section"
#endif

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
typedef uint32_t size_t;

#pragma pack (push, 1)

template <uint32_t nBytes>
struct Opaque {
    uint8_t m_p[nBytes];
};

struct Secp_point_data {
    Opaque<32> m_X;
    uint8_t m_Y;
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
typedef Opaque<64> HashValue512;
typedef Opaque<32> Secp_scalar_data;

template <bool bToShader, typename T>
inline void ConvertOrd(T&) {}

#ifndef _countof
#   define _countof(x) (sizeof(x) / sizeof((x)[0]))
#endif // _countof

#ifndef _MSC_VER
#   define BEAM_EXPORT __attribute__( ( visibility( "default" ) ) ) extern "C"
#else
#   define BEAM_EXPORT extern "C"
#endif

#ifndef assert
#   define assert(expr) do {} while (false)
#endif // assert

#else // HOST_BUILD

template <uint32_t nBytes>
using Opaque = beam::uintBig_t<nBytes>;

#endif // HOST_BUILD

#ifdef _MSC_VER
#   define __restrict__ __restrict
#endif // 

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

    template <typename TKey, typename TVal>
    inline uint32_t EmitLog_T(const TKey& key, const TVal& val, uint8_t nType = KeyTag::Internal)
    {
        return EmitLog(&key, sizeof(key), &val, sizeof(val), nType);
    }

    inline void Halt_if(bool b)
    {
        if (b)
            Halt();
    }

    struct KeyID
        :public SigRequest
    {
        KeyID() {}
        KeyID(const void* p, uint32_t n) {
            m_pID = p;
            m_nID = n;
        }

        template <typename T>
        KeyID(const T& x) : KeyID(&x, sizeof(x)) {}

        void get_Pk(PubKey& pk) const {
            DerivePk(pk, m_pID, m_nID);
        }

        void get_Pk(Secp_point& pk) const {
            Env::get_Pk(pk, m_pID, m_nID);
        }

        void get_Blind(Secp_scalar& s, const Secp_scalar& mul, uint32_t iNonceSlot) const {
            Env::get_BlindSk(s, m_pID, m_nID, mul, iNonceSlot);
        }

        void Comm_Listen(uint32_t nCookie) const {
            Env::Comm_Listen(m_pID, m_nID, nCookie);
        }
    };
	
    inline void Comm_WaitMsg()
    {
        Comm_WaitMsg(static_cast<uint32_t>(-1)); // wait forever
    }

    template <typename T>
    inline T* StackAlloc_T(uint32_t n) {
        return (T*) StackAlloc(sizeof(T) * n);
    }

    template <typename T>
    inline void StackFree_T(uint32_t n) {
        // not mandatory to call, but sometimes usefull before calling other heavy functions
        StackFree(sizeof(T) * n);
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

    template <uint32_t n>
    void WriteStr(const char(&sz)[n], uint32_t iStream)
    {
        Env::Write(sz, n - 1, iStream);
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

    template <bool bFlexible = false>
    class VarReaderEx
    {
        uint32_t m_Handle;

        void EnumInternal(const void* pKey0, uint32_t nKey0, const void* pKey1, uint32_t nKey1)
        {
            m_Handle = Vars_Enum(pKey0, nKey0, pKey1, nKey1);
        }

        void CloseInternal()
        {
            if constexpr (bFlexible)
            {
                if (!m_Handle)
                    return;
            }
            Vars_Close(m_Handle);
        }

    public:

        VarReaderEx(const void* pKey0, uint32_t nKey0, const void* pKey1, uint32_t nKey1) {
            EnumInternal(pKey0, nKey0, pKey1, nKey1);
        }

        template <typename TKey1, typename TKey2>
        VarReaderEx(const TKey1& key1, const TKey2& key2)
        {
            EnumInternal(&key1, sizeof(key1), &key2, sizeof(key2));
        }

        VarReaderEx()
        {
            static_assert(bFlexible);
            m_Handle = 0;
        }

        ~VarReaderEx()
        {
            CloseInternal();
        }

        void Enum(const void* pKey0, uint32_t nKey0, const void* pKey1, uint32_t nKey1)
        {
            CloseInternal();
            EnumInternal(pKey0, nKey0, pKey1, nKey1);
        }

        template <typename TKey1, typename TKey2>
        void Enum_T(const TKey1& key1, const TKey2& key2)
        {
            Enum(&key1, sizeof(key1), &key2, sizeof(key2));
        }

        bool MoveNext(void* pKey, uint32_t& nKey, void* pVal, uint32_t& nVal, uint8_t nRepeat)
        {
            return Vars_MoveNext(m_Handle, pKey, nKey, pVal, nVal, nRepeat);
        }

        template <typename TKey, typename TValue>
        bool MoveNext_T(TKey& key, TValue& val)
        {
            while (true)
            {
                uint32_t nKey = sizeof(key), nVal = sizeof(val);
                if (!MoveNext(&key, nKey, &val, nVal, 0))
                    return false;

                if ((sizeof(key) == nKey) && (sizeof(val) == nVal))
                    break;
            }
            return true;
        }

        template <typename TKey, typename TValue>
        static bool Read_T(const TKey& key, TValue& val)
        {
            VarReaderEx<false> r(key, key);

            uint32_t nKey = 0, nVal = sizeof(val);
            return
                r.MoveNext(nullptr, nKey, &val, nVal, 0) &&
                (sizeof(val) == nVal);
        }

    };

    typedef VarReaderEx<false> VarReader;

    class LogReader
    {
        uint32_t m_Handle = 0;
    public:

        HeightPos m_Pos;

        LogReader(const void* pKey0, uint32_t nKey0, const void* pKey1, uint32_t nKey1, const HeightPos* pPosMin, const HeightPos* pPosMax) {
            m_Handle = Logs_Enum(pKey0, nKey0, pKey1, nKey1, pPosMin, pPosMax);
        }

        template <typename TKey1, typename TKey2>
        LogReader(const TKey1& key1, const TKey2& key2, const HeightPos* pPosMin = nullptr, const HeightPos* pPosMax = nullptr)
            :LogReader(&key1, sizeof(key1), &key2, sizeof(key2), pPosMin, pPosMax)
        {
        }

        ~LogReader() {
            Logs_Close(m_Handle);
        }

        bool MoveNext(void* pKey, uint32_t& nKey, void* pVal, uint32_t& nVal, uint8_t nRepeat)
        {
            return Logs_MoveNext(m_Handle, pKey, nKey, pVal, nVal, m_Pos, nRepeat);
        }

        template <typename TKey, typename TValue>
        bool MoveNext_T(TKey& key, TValue& val)
        {
            while (true)
            {
                uint32_t nKey = sizeof(key), nVal = sizeof(val);
                if (!MoveNext(&key, nKey, &val, nVal, 0))
                    return false;

                if ((sizeof(key) == nKey) && (sizeof(val) == nVal))
                    break;
            }
            return true;
        }
    };

} // namespace Env

#ifndef HOST_BUILD

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

#endif // HOST_BUILD

#if defined(_MSC_VER) && !defined(HOST_BUILD)
#include <stdlib.h>
#endif

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
#ifdef _MSC_VER
        return _byteswap_ushort(x);
#else
        return __builtin_bswap16(x);
#endif
    }

    inline uint32_t FromBE(uint32_t x) {
#ifdef _MSC_VER
        return _byteswap_ulong(x);
#else
        return __builtin_bswap32(x);
#endif
    }

    inline uint64_t FromBE(uint64_t x) {
#ifdef _MSC_VER
        return _byteswap_uint64(x);
#else
        return __builtin_bswap64(x);
#endif
    }

    template <typename T>
    inline T FromLE(T x) {
        return x;
    }
#endif // HOST_BUILD

    template <typename T, uint32_t nMinAlloc = 16U>
    struct Vector
    {
        T* m_p = nullptr;
        uint32_t m_Count = 0;
        uint32_t m_Alloc = 0;

        ~Vector()
        {
            if (m_p)
                Env::Heap_Free(m_p);
        }

        void Prepare(uint32_t n)
        {
            if (m_Alloc >= n)
                return;

            n = std::max(n, m_Alloc * 2);
            n = std::max(n, nMinAlloc);

            T* pOld = m_p;
            m_p = (T*) Env::Heap_Alloc(sizeof(T) * n);
        
            if (pOld)
            {
                Env::Memcpy(m_p, pOld, sizeof(T) * m_Count);
                Env::Heap_Free(pOld);
            }

            m_Alloc = n;
        }

        T& emplace_back()
        {
            Prepare(m_Count + 1);
            return m_p[m_Count++];
        }
    };

    namespace details {

        template <uint64_t x> struct Number {
            template <uint32_t nRadix> struct Radix {
                static const uint32_t nDigits = 1 + Number<x / nRadix>::template Radix<nRadix>::nDigits;
            };
        };

        template <> struct Number<0> {
            template <uint32_t nRadix> struct Radix {
                static const uint32_t nDigits = 0;
            };
        };
    }

    struct String
    {
        template <uint32_t nRadix>
        struct Radix
        {
            static uint32_t Print(char* sz, uint64_t val)
            {
                uint32_t nDigs = 1;
                for (uint64_t val0 = val; ; nDigs++)
                    if (!(val0 /= nRadix))
                        break;

                Print(sz, val, nDigs);
                return nDigs;
            }

            static void Print(char* sz, uint64_t val, uint32_t nDigs)
            {
                for (sz[nDigs] = 0; ; val /= nRadix)
                {
                    sz[--nDigs] = '0' + (val % nRadix);
                    if (!nDigs)
                        break;
                }
            }

            template <uint64_t x> struct Digits {
                static const uint32_t N = x ? details::Number<x>::template Radix<nRadix>::nDigits : 1;
            };

            template <typename T> struct DigitsMax {
                static const uint32_t N = Digits<static_cast<T>(-1)>::N;
            };
        };

        typedef Radix<10> Decimal;
    };

} // namespace Utils

template <typename T> Utils::BlobOf<T> _POD_(T& x) {
    return Utils::BlobOf<T>(x);
}

namespace Cast
{
	template <typename T> inline T& NotConst(const T& x) { return (T&) x; }
	template <typename T> inline T* NotConst(const T* p) { return (T*) p; }

	template <typename TT, typename T> inline const TT& Up(const T& x)
	{
		const TT& ret = (const TT&) x;
        [[maybe_unused]] const T& unused = ret;
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
        [[maybe_unused]] const T* unused = ret;
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

        template <uint32_t nBytes>
        void Write(const Opaque<nBytes>& x)
        {
            Write(&x, sizeof(x));
        }

        void Write(const Secp_point_data& pd)
        {
            Write(&pd, sizeof(pd));
        }

        void Read(void* p, uint32_t n)
        {
            Env::HashGetValue(m_p, p, n);
        }

        template <typename T>
        void operator >> (T& res)
        {
            Read(&res, sizeof(res));
        }

        Base& get_Challenge(Secp_scalar& s)
        {
            while (true)
            {
                Secp_scalar_data sd;
                *this >> sd;
                Write(&sd, sizeof(sd));

                if (!_POD_(sd).IsZero() && Env::Secp_Scalar_import(s, sd))
                    break;
            }
            return *this;
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

    inline void get_ContractLogHash(HashValue& hv, const ContractID& cid, const void* pKey, uint32_t nKey, const void* pVal, uint32_t nVal, uint32_t nPos)
    {
        HashProcessor::Sha256 hp;

        uint32_t nSizeKey = sizeof(cid) + nKey;

        hp
            << "beam.contract.log"
            << nPos
            << nSizeKey;

        hp.Write(cid);
        hp.Write(pKey, nKey);
        hp.Write(nVal);
        hp.Write(pVal, nVal);

        hp >> hv;
    }
}

namespace Secp
{
    template <typename T>
    struct UnaryMinus {
        const T& m_Src;
        UnaryMinus(const T& x) :m_Src(x) {}
    };

    struct Scalar
    {
        Secp_scalar* m_p;

        Scalar() { m_p = Env::Secp_Scalar_alloc(); }
        ~Scalar() { Env::Secp_Scalar_free(*m_p); }

        operator Secp_scalar& () const {
            return *m_p;
        }

        uint8_t Import(const Secp_scalar_data& k) const {
            return Env::Secp_Scalar_import(*m_p, k);
        }

        void Export(Secp_scalar_data& k) const {
            Env::Secp_Scalar_export(*m_p, k);
        }

        void operator += (const Scalar& x) {
            Env::Secp_Scalar_add(*m_p, *m_p, x);
        }

        UnaryMinus<Scalar> operator - () {
            return UnaryMinus<Scalar>(*this);
        }

        void operator = (const UnaryMinus<Scalar>& x) {
            Env::Secp_Scalar_neg(*m_p, x.m_Src);
        }

    };

    struct Point
    {
        Secp_point* m_p;

        Point() { m_p = Env::Secp_Point_alloc(); }
        ~Point() { Env::Secp_Point_free(*m_p); }

        operator Secp_point& () const {
            return *m_p;
        }

        uint8_t Import(const PubKey& pk) {
            return Env::Secp_Point_Import(*m_p, pk);
        }

        void FromSlot(uint32_t iSlot)
        {
            Env::get_SlotImage(*m_p, iSlot);
        }

        void Export(PubKey& pk) const {
            Env::Secp_Point_Export(*m_p, pk);
        }

        void operator += (const Point& x) {
            Env::Secp_Point_add(*m_p, *m_p, x);
        }

        UnaryMinus<Point> operator - () {
            return UnaryMinus<Point>(*this);
        }

        void operator = (const UnaryMinus<Point>& x) {
            Env::Secp_Point_neg(*m_p, x.m_Src);
        }

        bool IsZero() const {
            return !!Env::Secp_Point_IsZero(*m_p);
        }
    };

    typedef HashProcessor::Base Oracle;

    struct Signature
    {
#pragma pack (push, 1)
        Secp_point_data m_NoncePub;
        Secp_scalar_data m_kSig;
#pragma pack (pop)

        void Sign(Oracle& o, const Env::KeyID& kid)
        {
            const uint32_t iNonceSlot = s_NonceSlots - 1;

            {
                // opt: derive strong seed for nonce
                HashProcessor::Base hp;
                hp.m_p = Env::HashClone(o.m_p);
                hp.Write(kid.m_pID, kid.m_nID); // add key material

                HashValue hv;
                hp >> hv;

                Env::SlotInit(&hv, sizeof(hv), iNonceSlot);
            }

            Point ptN;
            Env::get_SlotImage(ptN, iNonceSlot);
            ptN.Export(m_NoncePub);

            o << m_NoncePub;

            Scalar e;
            o.get_Challenge(e);

            kid.get_Blind(e, e, iNonceSlot);
            e = -e;
            e.Export(m_kSig);
        }

        bool IsValid(Oracle& o, const PubKey& pk) const
        {
            Secp::Point p0, p1;
            if (!p0.Import(pk) ||
                !p1.Import(m_NoncePub))
                return false;

            o << m_NoncePub;

            Scalar e;
            o.get_Challenge(e);

            Env::Secp_Point_mul(p0, p0, e);
            p0 += p1;

            if (!e.Import(m_kSig))
                return false;

            Env::Secp_Point_mul_G(p1, e);
            p0 += p1;

            return p0.IsZero();
        }
    };
}

static const Amount g_Beam2Groth = 100000000;
