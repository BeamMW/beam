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
            return NumericUtils::RadixConverter<0x100, nBytes, s_Radix>::get_MaxLen();
        }

        template <uint32_t nCharsEncoded>
        static constexpr uint32_t get_MaxDec() {
            return NumericUtils::RadixConverter<s_Radix, nCharsEncoded, 0x100>::get_MaxLen();
        }

        template <uint32_t nBytes>
        static constexpr uint32_t get_MaxWrk() {
            return (get_MaxEnc<nBytes>() + sizeof(uint32_t) - 1) / sizeof(uint32_t);
        }

        static uint32_t EncodeRaw(char* szEnc, uint32_t nEnc, uint32_t* pWrk, const uint8_t* p, uint32_t n); // returns the num of nnz characters.
        // Supplied string is always fully filled (perhaps with leading '1' characeters). Returns nEnc+1 if the buffer was not enough
        // does NOT 0-terminate the result

        static uint32_t DecodeRaw(uint8_t* p, uint32_t n, uint32_t* pWrk, const char* szEnc, uint32_t nEnc); // returns num of processed characters

        template <uint32_t n>
        static void Encode(char* szEnc, const uint8_t* p)
        {
            uint32_t pWrk[get_MaxWrk<n>()];
            EncodeRaw(szEnc, get_MaxEnc<n>(), pWrk, p, n);
        }

        template <uint32_t n>
        static bool Decode(uint8_t* p, const char* szEnc)
        {
            uint32_t pWrk[get_MaxWrk<n>()];
            uint32_t nExpLen = get_MaxEnc<n>();
            return DecodeRaw(p, n, pWrk, szEnc, nExpLen) == nExpLen;
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

    };

} // namespace beam
