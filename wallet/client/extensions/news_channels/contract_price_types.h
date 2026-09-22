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

#pragma once
#include "core/block_crypt.h"   // beam::Asset::ID, beam::Amount, beam::Height

namespace beam::wallet
{
    struct PoolData
    {
        Asset::ID m_Aid1 = 0;
        Asset::ID m_Aid2 = 0;   // invariant: m_Aid1 < m_Aid2 (BEAM == 0 sorts first)
        Amount    m_Reserve1 = 0;
        Amount    m_Reserve2 = 0;
    };

    struct OracleMedian
    {
        double m_UsdPerBeam = 0.0;
        Height m_hEnd = 0;       // median valid while m_hEnd >= tip
        bool   m_Ok = false;     // false => parse failed / no data
    };
}
