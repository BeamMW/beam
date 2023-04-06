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

#include "base58.h"
#include "../utility/byteorder.h"

namespace beam {

    namespace Base58Impl {

        static const char s_pAlphabet[] = {
          '1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','J','K','L','M','N','P','Q','R','S','T','U','V',
          'W','X','Y','Z','a','b','c','d','e','f','g','h','i','j','k','m','n','o','p','q','r','s','t','u','v','w','x','y','z'
        };

        static_assert(_countof(s_pAlphabet) == Base58::s_Radix);

        static const char s_chRev0 = 49;

        static const uint8_t s_pReverseTbl[] = {
            0,1,2,3,4,5,6,7,8, 0xff,0xff,0xff,0xff,0xff,0xff,0xff, 9,10,11,12,13,14,15,16, 0xff, 17,18,19,20,21, 0xff, 22,23,24,25,26,27,28,29,30,31,32,
            0xff,0xff,0xff,0xff,0xff,0xff, 33,34,35,36,37,38,39,40,41,42,43, 0xff, 44,45,46,47,48,49,50,51,52,53,54,55,56,57
        };

#ifdef _DEBUG
        void VerifyReverseTable()
        {
            // vice
            for (uint32_t i = 0; i < _countof(s_pAlphabet); i++)
            {
                char ch = s_pAlphabet[i];
                uint8_t iIdx = ch - s_chRev0;
                assert(iIdx < _countof(s_pReverseTbl));
                assert(s_pReverseTbl[iIdx] == i);
            }

            // versa
            for (uint32_t i = 0; i < _countof(s_pReverseTbl); i++)
            {
                uint8_t num = s_pReverseTbl[i];
                if (0xff != num)
                {
                    assert(num < _countof(s_pAlphabet));
                    assert(s_pAlphabet[num] == (char) (s_chRev0 + i));
                }
            }
        }
#endif // _DEBUG

        uint8_t DecodeChar(char ch)
        {
            uint8_t iIdx = ch - s_chRev0;
            return (iIdx < _countof(s_pReverseTbl)) ? s_pReverseTbl[iIdx] : 0xff;
        }

        uint32_t DivideOnce(uint32_t* p, uint32_t n, uint32_t div)
        {
            assert(div && n && p[0]);

            uint64_t carry = 0;
            for (uint32_t i = 0; ; )
            {
                carry |= p[i];
                p[i] = (uint32_t)(carry / div);
                carry %= div;

                if (++i == n)
                    break;
                carry <<= 32;
            }

            return (uint32_t) carry;
        }

        uint32_t EncodeW(char* szEnc, uint32_t nEnc, uint32_t* pW, uint32_t nW)
        {
            char* szPos = szEnc + nEnc;

            const uint32_t ord1 = Base58::s_Radix;
            const uint32_t ord2 = ord1 * ord1;
            const uint32_t ord3 = ord2 * ord1;
            const uint32_t ord4 = ord3 * ord1;
            const uint32_t ord5 = ord4 * ord1;

            static_assert(_countof(s_pAlphabet) == 58, "");
            while (nW)
            {
                if (pW[0])
                {
                    uint32_t resid;

                    if (((nW > 1) || (*pW >= ord5)) && (szPos - 4 >= szEnc))
                    {
                        resid = DivideOnce(pW, nW, ord5);
                        for (int i = 0; i < 4; i++)
                        {
                            *(--szPos) = s_pAlphabet[resid % ord1];
                            resid /= ord1;
                        }
                    }
                    else
                        resid = DivideOnce(pW, nW, ord1);

                    if (szPos == szEnc)
                        return nEnc + 1; // overflow

                    *(--szPos) = s_pAlphabet[resid];
                }
                else
                {
                    pW++;
                    nW--;
                }
            }

            uint32_t retVal = (uint32_t) (szEnc + nEnc - szPos);

            while (szPos > szEnc)
                *(--szPos) = s_pAlphabet[0];

            return retVal;
        }



        uint32_t DecodeW(const char* szEnc, uint32_t nEnc, uint32_t* pW, uint32_t nW, uint32_t& nW_Nnz)
        {
            const uint32_t ord1 = Base58::s_Radix;

            nW_Nnz = nW;

            for (uint32_t i = 0; i < nEnc; )
            {
                uint32_t ord = 1, resid = 0;

                for (uint32_t nPortion = std::min(nEnc - i, 5u); nPortion--; )
                {
                    uint32_t val = DecodeChar(szEnc[i++]);
                    if (val >= ord1)
                    {
                        return i; // invalid char
                    }

                    resid = resid * ord1 + val;
                    ord *= ord1;
                }

                uint64_t carry = resid;
                // multiply and add
                for (uint32_t j = nW; ; )
                {
                    if (!j--)
                    {
                        if (carry)
                            return false; // overflow
                        break;
                    }

                    if (j >= nW_Nnz)
                        carry += ((uint64_t) ord) * pW[j];
                    else
                    {
                        if (!carry)
                            break;

                        nW_Nnz = j;
                    }

                    pW[j] = (uint32_t) carry;
                    carry >>= 32;
                }
            }

            return nEnc;
        }

    } // namespace Base58Impl


    uint32_t Base58::EncodeRaw(char* szEnc, uint32_t nEnc, uint32_t* pWrk, const uint8_t* p, uint32_t n)
    {
        uint32_t nResid = n % sizeof(uint32_t);
        if (nResid)
        {
            pWrk[0] = 0;
            memcpy(((uint8_t*) pWrk) + sizeof(uint32_t) - nResid, p, n);
            n /= sizeof(uint32_t);
            n++;
        }
        else
        {
            memcpy(pWrk, p, n);
            n /= sizeof(uint32_t);
        }

        for (uint32_t i = 0; i < n; i++)
            pWrk[i] = ByteOrder::to_be(pWrk[i]);

        return Base58Impl::EncodeW(szEnc, nEnc, pWrk, n);

    }

    uint32_t Base58::DecodeRaw(uint8_t* p, uint32_t n, uint32_t* pWrk, const char* szEnc, uint32_t nEnc)
    {
        uint32_t nW = n / sizeof(uint32_t);
        uint32_t nResid = n % sizeof(uint32_t);
        if (nResid)
            nW++;

        uint32_t nW_Nnz;
        uint32_t retVal = Base58Impl::DecodeW(szEnc, nEnc, pWrk, nW, nW_Nnz);

        for (uint32_t i = 0; i < nW_Nnz; i++)
            pWrk[i] = 0; // possibly remaining heading

        for (uint32_t i = nW_Nnz; i < nW; i++)
            pWrk[i] = ByteOrder::from_be(pWrk[i]);

        if (nResid)
        {
            const uint8_t* pPtr = (const uint8_t*) pWrk;
            for (uint32_t i = 0; i < nResid; i++)
                if (pPtr[i])
                    return false; // overflow

            memcpy(p, pPtr + sizeof(uint32_t) - nResid, n);
        }
        else
            memcpy(p, pWrk, n);

        return retVal;
    }




} // namespace beam
