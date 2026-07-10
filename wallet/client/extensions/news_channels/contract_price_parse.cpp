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

namespace beam::wallet
{
    OracleMedian ParseOracleMedianValue(const uint8_t* v, size_t n)
    {
        OracleMedian om;
        if (n < 24) return om;                     // m_Ok stays false
        uint64_t num;  int32_t order;  uint64_t hEnd;
        std::memcpy(&num,   v + 0,  8);
        std::memcpy(&order, v + 8,  4);
        std::memcpy(&hEnd,  v + 16, 8);            // skip 4-byte m_Dummy at [12..16]
        om.m_UsdPerBeam = num ? std::ldexp(double(num), order) : 0.0;
        om.m_hEnd = hEnd;
        om.m_Ok = true;
        return om;
    }

    bool ParsePool(const uint8_t* key, size_t nKey, const uint8_t* val, size_t nVal, PoolData& out)
    {
        // key = cid(32) || KeyTag::Internal(1) || s_Pool(1) || aid1(4) || aid2(4) || ...
        if (nKey < 42 || nVal < 16) return false;
        uint32_t a1, a2;
        std::memcpy(&a1, key + 34, 4);
        std::memcpy(&a2, key + 38, 4);
        uint64_t r1, r2;
        std::memcpy(&r1, val + 0, 8);
        std::memcpy(&r2, val + 8, 8);
        out.m_Aid1 = a1; out.m_Aid2 = a2;
        out.m_Reserve1 = r1; out.m_Reserve2 = r2;
        return true;
    }
}
