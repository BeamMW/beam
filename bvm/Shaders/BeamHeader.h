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

#pragma pack (push, 1)
#pragma pack (pop)

struct BeamHeaderPrefix
{
    Height m_Height;
    HashValue m_Prev;
    HashValue m_ChainWork; // not hash, just same size

    template <bool bToShader>
    void Convert()
    {
        ConvertOrd<bToShader>(m_Height);
    }
};

struct BeamHeaderSequence
{
    HashValue m_Kernels;
    HashValue m_Definition;
    Timestamp m_TimeStamp;

    struct PoW
    {
        uint8_t m_pIndices[104];
        uint8_t m_pNonce[8];
        uint32_t m_Difficulty;

    } m_PoW;

    template <bool bToShader>
    void Convert()
    {
        ConvertOrd<bToShader>(m_TimeStamp);
        ConvertOrd<bToShader>(m_PoW.m_Difficulty);
    }
};

struct BeamHeaderFull
    :public BeamHeaderPrefix
    ,public BeamHeaderSequence
{

    template <bool bToShader>
    void Convert()
    {
        BeamHeaderPrefix::Convert<bToShader>();
        BeamHeaderSequence::Convert<bToShader>();
    }

    void get_Hash(HashValue& out, const HashValue* pRules) const
    {
        get_HashInternal(out, true, pRules);
    }

    bool IsValid(const HashValue* pRules) const
    {
        if (!TestDifficulty())
            return false;

        HashValue hv;
        get_HashInternal(hv, false, pRules);
        return BeamHashIII::Verify(hv, m_PoW.m_pNonce, sizeof(m_PoW.m_pNonce), m_PoW.m_pIndices, sizeof(m_PoW.m_pIndices));
    }

private:

    void get_HashInternal(HashValue& out, bool bFull, const HashValue* pRules) const
    {
        HashProcessor hp;
        hp.m_p = Env::HashCreateSha256();

        hp
            << m_Height
            << m_Prev
            << m_ChainWork
            << m_Kernels
            << m_Definition
            << m_TimeStamp
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

    bool TestDifficulty() const
    {
        HashProcessor hp;
        hp.m_p = Env::HashCreateSha256();
        hp.Write(m_PoW.m_pIndices, sizeof(m_PoW.m_pIndices));

        HashValue hv;
        hp >> hv;

        return BeamDifficulty::IsTargetReached(hv, m_PoW.m_Difficulty);
    }

};
