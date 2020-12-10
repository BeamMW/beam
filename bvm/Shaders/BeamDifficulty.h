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

    static const uint32_t s_MantissaBits = 24;

    static uint32_t Unpack(Short& res, uint32_t nPacked)
    {
        const uint32_t nWordBits = sizeof(MultiPrecision::Word) * 8;
        static_assert(nWordBits > s_MantissaBits, "");

        uint32_t order = (nPacked >> s_MantissaBits);

        uint32_t nWords = order / nWordBits;
        order %= nWordBits;

        const uint32_t nLeadingBit = 1U << s_MantissaBits;
        uint32_t mantissa = nLeadingBit | (nPacked & (nLeadingBit - 1));

        res = ((uint64_t) mantissa) << (order + nWordBits - s_MantissaBits);

        return nWords + 1;
    }

    template <typename T>
    static bool IsTargetReached(const T& val, uint32_t nPacked)
    {
        static_assert(!(sizeof(T) % sizeof(MultiPrecision::Word)), "");
        const uint32_t nSrcWords = sizeof(T) / sizeof(MultiPrecision::Word);

        MultiPrecision::UInt<nSrcWords> src;
        src.FromBE((const MultiPrecision::Word*) &val);

        Short diffShort;
        uint32_t nZeroWords = Unpack(diffShort, nPacked);

        auto a = src * diffShort; // would be src+2 words long

        if (nZeroWords > a.nWords)
            nZeroWords = a.nWords;

        return Env::Memis0(a.get_AsArr() + a.nWords - nZeroWords, sizeof(MultiPrecision::Word) * nZeroWords);
    }

};
