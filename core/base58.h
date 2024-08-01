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
            return MultiWord::Factorization::get_MaxLen<nBytes>(0x100, s_Radix);
        }

        template <uint32_t nCharsEncoded>
        static constexpr uint32_t get_MaxDec() {
            return MultiWord::Factorization::get_MaxLen<nCharsEncoded>(s_Radix, 0x100);
        }

        template <uint32_t n>
        static void Encode(char* szEnc, const uint8_t* p)
        {
            typedef uintBig_t<n> Type;
            const Type* pVal = reinterpret_cast<const Type*>(p);

            Type::Number num;
            pVal->ToNumber(num);

            const uint32_t nEnc = get_MaxEnc<n>();
            
            num.DecomposeEx<s_Radix>(szEnc, nEnc); // directly into szEnc, then encode in-place
            EncodeSymbols(szEnc, reinterpret_cast<const uint8_t*>(szEnc), nEnc);
        }

        template <uint32_t n>
        static bool Decode(uint8_t* p, const char* szEnc)
        {
            const uint32_t nEnc = get_MaxEnc<n>();

            uint8_t pDec[nEnc];
            auto nDec = DecodeSymbols(pDec, szEnc, nEnc);

            typedef uintBig_t<n> Type;
            Type::Number num;
            num.ComposeEx<s_Radix>(pDec, nDec);

            Type* pVal = reinterpret_cast<Type*>(p);
            pVal->FromNumber(num);

            if (nDec == nEnc)
                return true;

            // return true if stopped on 0-term (i.e. encoded string was truncated)
            return !szEnc[nDec];
        }

        template <uint32_t n>
        static std::string to_string(const uint8_t* p)
        {
            std::string s;
            const uint32_t nLen = get_MaxEnc<n>();
            if constexpr (nLen > 0)
            {
                s.resize(nLen);
                Encode<n>(&s.front(), p);
            }

            return s;
        }

        template <typename T>
        static std::string to_string(const T& x)
        {
            return to_string<sizeof(T)>(reinterpret_cast<const uint8_t*>(&x));
        }

    private:
        static void EncodeSymbols(char* szEnc, const uint8_t* pRaw, uint32_t nLen);
        static uint32_t DecodeSymbols(uint8_t* pRaw, const char* szEnc, uint32_t nLen);
    };

} // namespace beam
