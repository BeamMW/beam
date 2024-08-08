#pragma once
#include "uintBig.h"

namespace beam
{
    struct Base58
    {
        static const uint32_t s_Radix = 58;

        struct Impl;

        template <uint32_t nBytes>
        static constexpr uint32_t get_MaxEnc() {
            return MultiWord::Factorization::Decomposed<nBytes, 0x100, s_Radix>::nMaxLen;
        }

        template <uint32_t nCharsEncoded>
        static constexpr uint32_t get_MaxDec() {
            return MultiWord::Factorization::Decomposed<nCharsEncoded, s_Radix, 0x100>::nMaxLen;
        }

        static uint32_t EncodeEx(char* szEnc, uint32_t nEnc, const uint8_t* p, uint32_t n, bool bTrim, MultiWord::Slice sBuf);

        template <uint32_t n>
        static uint32_t Encode(char* szEnc, const uint8_t* p, bool bTrim = false)
        {
            typedef uintBig_t<n> Type;
            typename Type::Number buf;

            const uint32_t nEnc = get_MaxEnc<n>();
            return EncodeEx(szEnc, nEnc, p, n, bTrim, buf.get_Slice());
        }

        static uint32_t DecodeEx(uint8_t* p, uint32_t n, const char* szEnc, uint32_t nEnc, MultiWord::Slice sBuf); // returns number of chars processed

        template <uint32_t n>
        static bool Decode(uint8_t* p, const char* szEnc)
        {
            const uint32_t nEnc = get_MaxEnc<n>();

            typedef uintBig_t<n> Type;
            typename Type::Number buf;

            auto nDec = DecodeEx(p, n, szEnc, nEnc, buf.get_Slice());
            if (nDec == nEnc)
                return true;

            // return true if stopped on 0-term (i.e. encoded string was truncated)
            assert(nDec < nEnc);
            return !szEnc[nDec];
        }

        template <uint32_t n>
        static std::string to_string(const uint8_t* p, bool bTrim = false)
        {
            std::string s;
            const uint32_t nLen = get_MaxEnc<n>();
            if constexpr (nLen > 0)
            {
                s.resize(nLen);
                auto res = Encode<n>(&s.front(), p, bTrim);
                s.resize(res);
            }

            return s;
        }

        template <typename T>
        static std::string to_string(const T& x)
        {
            return to_string<sizeof(T)>(reinterpret_cast<const uint8_t*>(&x));
        }
    };

} // namespace beam
