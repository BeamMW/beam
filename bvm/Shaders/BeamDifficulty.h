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
#include "Math.h"

struct BeamDifficulty
{
    typedef MultiPrecision::UInt<2> Short;
    typedef MultiPrecision::UInt<8> Raw;

    static const uint32_t s_MantissaBits = 24;

    struct Data
    {
        uint32_t m_Order;
        uint32_t m_Mantissa;

        Data(uint32_t nPacked)
        {
            m_Order = (nPacked >> s_MantissaBits);

            const uint32_t nLeadingBit = 1U << s_MantissaBits;
            m_Mantissa = nLeadingBit | (nPacked & (nLeadingBit - 1));
        }
    };

    static void Unpack(Raw& res, uint32_t nPacked)
    {
        const uint32_t nWordBits = sizeof(MultiPrecision::Word) * 8;
        const uint32_t nMaxOrder = Raw::nWords * nWordBits - s_MantissaBits - 1;
        const uint32_t nInf = (nMaxOrder + 1) << s_MantissaBits;

        if (nPacked < nInf)
        {
            Data d(nPacked);
            MultiPrecision::UInt<1> x(d.m_Mantissa);
            res.Set(x, d.m_Order);
        }
        else
        {
            Utils::SetObject(res, static_cast<uint8_t>(-1)); // inf
        }

    }

    static uint32_t Unpack(Short& res, uint32_t nPacked)
    {
        const uint32_t nWordBits = sizeof(MultiPrecision::Word) * 8;
        static_assert(nWordBits > s_MantissaBits, "");

        Data d(nPacked);

        uint32_t nWords = d.m_Order / nWordBits;
        d.m_Order %= nWordBits;

        res = ((uint64_t) d.m_Mantissa) << (d.m_Order + nWordBits - s_MantissaBits);

        return nWords + 1;
    }

    template <typename T>
    static bool IsTargetReached(const T& val, uint32_t nPacked)
    {
        static_assert(!(sizeof(T) % sizeof(MultiPrecision::Word)), "");
        const uint32_t nSrcWords = sizeof(T) / sizeof(MultiPrecision::Word);

        MultiPrecision::UInt<nSrcWords> src;
        src.FromBE_T(val);

        Short diffShort;
        uint32_t nZeroWords = Unpack(diffShort, nPacked);

        auto a = src * diffShort; // would be src+2 words long

        if (nZeroWords > a.nWords)
            nZeroWords = a.nWords;

        return Env::Memis0(a.get_AsArr() + a.nWords - nZeroWords, sizeof(MultiPrecision::Word) * nZeroWords);
    }

};
