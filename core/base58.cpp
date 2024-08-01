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


    } // namespace Base58Impl

    void Base58::EncodeEx(char* szEnc, uint32_t nEnc, const uint8_t* p, uint32_t n, MultiWord::Slice sBuf)
    {
        uintBigImpl::_ToNum(sBuf, p, n);
        sBuf.Trim();

        MultiWord::Factorization::Decomposer ctx;
        ctx.m_s = sBuf;

        struct MyOut
            :public MultiWord::Factorization::DefaultOut<char>
        {
            void PushBack(MultiWord::Word w)
            {
                assert(w < _countof(Base58Impl::s_pAlphabet));
                PushBackInternal(Base58Impl::s_pAlphabet[w]);
            }
        } out;

        out.m_pB = szEnc;
        out.m_pE = szEnc + nEnc;

        ctx.Process_T<s_Radix>(out);
    }

    uint32_t Base58::DecodeEx(uint8_t* p, uint32_t n, const char* szEnc, uint32_t nEnc, MultiWord::Slice sBuf)
    {
        MultiWord::Factorization::Composer ctx;
        ctx.m_sRes = sBuf;
        ctx.Init();

        struct MyIn
            :public MultiWord::Factorization::DefaultIn<char>
        {
            bool PopFront(MultiWord::Word& w)
            {
                if (m_pB == m_pE)
                    return false;

                auto val = Base58Impl::DecodeChar(*m_pB);
                if (val >= s_Radix)
                    return false;

                w = val;
                m_pB++;
                return true;
            }
        } in;

        in.m_pB = szEnc;
        in.m_pE = szEnc + nEnc;

        ctx.Process_T<s_Radix>(in);

        uintBigImpl::_FromNum(sBuf.get_Const(), p, n);

        return static_cast<uint32_t>(in.m_pB - szEnc);
    }

} // namespace beam
