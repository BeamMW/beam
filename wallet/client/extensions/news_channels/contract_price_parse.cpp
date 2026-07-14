// Copyright 2026 The Beam Team
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

#include "contract_price_parse.h"
#include <cstring>
#include <cmath>

namespace
{
    // The contract ABI stores integers little-endian; read them explicitly so
    // parsing is byte-order independent regardless of the host platform.
    uint32_t rdU32le(const uint8_t* p)
    {
        return  uint32_t(p[0])
             | (uint32_t(p[1]) << 8)
             | (uint32_t(p[2]) << 16)
             | (uint32_t(p[3]) << 24);
    }

    uint64_t rdU64le(const uint8_t* p)
    {
        return  uint64_t(rdU32le(p))
             | (uint64_t(rdU32le(p + 4)) << 32);
    }
}

namespace beam::wallet
{
    OracleMedian ParseOracleMedianValue(const uint8_t* v, size_t n)
    {
        OracleMedian om;
        if (n < 24) return om;                     // m_Ok stays false
        const uint64_t num   = rdU64le(v + 0);
        const int32_t  order = int32_t(rdU32le(v + 8));
        const uint64_t hEnd  = rdU64le(v + 16);    // skip 4-byte m_Dummy at [12..16]
        const double val = num ? std::ldexp(double(num), order) : 0.0;
        om.m_UsdPerBeam = std::isfinite(val) ? val : 0.0;   // absurd order -> blank over wrong
        om.m_hEnd = hEnd;
        om.m_Ok = true;
        return om;
    }

    bool ParsePool(const uint8_t* key, size_t nKey, const uint8_t* val, size_t nVal, PoolData& out)
    {
        // key = cid(32) || KeyTag::Internal(1) || Amm::Tags::s_Pool(1) || aid1(4 LE) || aid2(4 LE) || FeeSettings(1)
        if (nKey != 43 || nVal < 16) return false;
        if (key[32] != 0 /*KeyTag::Internal*/ || key[33] != 1 /*Amm::Tags::s_Pool*/) return false;
        const uint32_t a1 = rdU32le(key + 34), a2 = rdU32le(key + 38);
        if (a1 >= a2) return false;   // contract requires distinct, well-ordered aids
        out.m_Aid1 = a1; out.m_Aid2 = a2;
        out.m_Reserve1 = rdU64le(val + 0);
        out.m_Reserve2 = rdU64le(val + 8);
        return true;
    }
}
