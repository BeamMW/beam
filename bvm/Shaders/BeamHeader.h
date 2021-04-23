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
#include "BeamDifficulty.h"
#include "BeamHashIII.h"


inline void BlockHeader::Full::get_Hash(HashValue& out, const HashValue* pRules) const
{
    get_HashInternal(out, true, pRules);
}

inline bool BlockHeader::Full::IsValid(const HashValue* pRules) const
{
    if (!TestDifficulty())
        return false;

    HashValue hv;
    get_HashInternal(hv, false, pRules);

    return BeamHashIII::Verify(
        &hv, sizeof(hv),
        m_PoW.m_pNonce, sizeof(m_PoW.m_pNonce),
        m_PoW.m_pIndices, sizeof(m_PoW.m_pIndices));
}

inline void BlockHeader::Full::get_HashInternal(HashValue& out, bool bFull, const HashValue* pRules) const
{
    HashProcessor::Sha256 hp;
    hp
        << m_Height
        << m_Prev
        << m_ChainWork
        << m_Kernels
        << m_Definition
        << m_Timestamp
        << m_PoW.m_Difficulty;

    if (pRules)
        hp << *pRules;

    if (bFull)
    {
        hp.Write(m_PoW.m_pIndices, sizeof(m_PoW.m_pIndices));
        hp.Write(m_PoW.m_pNonce, sizeof(m_PoW.m_pNonce));
    }

    hp >> out;
}

inline bool BlockHeader::Full::TestDifficulty() const
{
    HashProcessor::Sha256 hp;
    hp.Write(m_PoW.m_pIndices, sizeof(m_PoW.m_pIndices));

    HashValue hv;
    hp >> hv;

    return BeamDifficulty::IsTargetReached(hv, m_PoW.m_Difficulty);
}
