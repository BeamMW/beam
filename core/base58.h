#pragma once
#include "uintBig.h"

namespace beam
{
    template <uint32_t aRadix, const uint32_t aLen, uint32_t bRadix>
    struct RadixConverter
    {
        static constexpr uint32_t get_MaxLen()
        {
            uint32_t pW[aLen + 1] = { 0 };
            pW[0] = 1;
            uint32_t nW = 1;

            // calculate aRadix^aLen
            for (uint32_t i = 0; i < aLen; i++)
            {
                uint64_t carry = 0;
                for (uint32_t j = 0; j < nW; j++)
                {
                    carry += ((uint64_t)pW[j]) * aRadix;
                    pW[j] = (uint32_t)carry;
                    carry >>= 32;
                }
                if (carry)
                    pW[nW++] = (uint32_t)carry;
            }

            // subtract 1
            for (uint32_t j = 0; ; j++)
                if (pW[j]--)
                    break;

            // calculate log, round up (how many times to devide before it turns zero)
            uint32_t retVal = 0;

            while (nW)
            {
                if (pW[nW - 1])
                {
                    // divide by new radix
                    uint64_t carry = 0;
                    for (uint32_t j = nW; j--; )
                    {
                        carry |= pW[j];
                        pW[j] = (uint32_t)(carry / bRadix);
                        carry %= bRadix;
                        carry <<= 32;
                    }

                    retVal++;
                }
                else
                    nW--;
            }

            return retVal;
        }
    };


    struct Base58
    {
        static const uint32_t s_Radix = 58;

        struct Impl;

        template <uint32_t nBytes>
        static constexpr uint32_t get_MaxEnc() {
            return RadixConverter<0x100, nBytes, s_Radix>::get_MaxLen();
        }

        template <uint32_t nCharsEncoded>
        static constexpr uint32_t get_MaxDec() {
            return RadixConverter<s_Radix, nCharsEncoded, 0x100>::::get_MaxLen();
        }

        template <uint32_t nBytes>
        static constexpr uint32_t get_MaxWrk() {
            return (get_MaxEnc<nBytes>() + sizeof(uint32_t) - 1) / sizeof(uint32_t);
        }

        static uint32_t EncodeRaw(char* szEnc, uint32_t nEnc, uint32_t* pWrk, const uint8_t* p, uint32_t n); // returns the num of nnz characters.
        // Supplied string is always fully filled (perhaps with leading '0' characeters). Returns nEnc+1 if the buffer was not enough
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

    };

} // namespace beam
