#pragma once
#include "../BeamHeader.h"
#include "../Math.h"

namespace Sidechain
{
#pragma pack (push, 1)

    struct Init
    {
        static const uint32_t s_iMethod = 0;

        HashValue m_Rules; // host determines it w.r.t. header height. Make it a param, to make contract more flexible
        BeamHeaderFull m_Hdr0;
        uint8_t m_VerifyPoW;

        template <bool bToShader>
        void Convert()
        {
            m_Hdr0.Convert<bToShader>();
        }
    };

    template <uint32_t nHdrs>
    struct Grow
    {
        static const uint32_t s_iMethod = 2;

        PubKey m_Contributor;
        uint32_t m_nSequence;
        BeamHeaderPrefix m_Prefix;
        BeamHeaderSequence m_pSequence[nHdrs];

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_nSequence);
            m_Prefix.Convert<bToShader>();

            for (uint32_t i = 0; i < nHdrs; i++)
                m_pSequence[i].template Convert<bToShader>();
        }
    };

    struct Global
    {
        HashValue m_Rules;
        BeamDifficulty::Raw m_Chainwork;
        Height m_Height;
        uint8_t m_VerifyPoW;
    };

    struct PerHeight
    {
        HashValue m_Hash;
        HashValue m_Kernels;
        //HashValue m_Definition;
        Timestamp m_TimeStamp;
        uint32_t m_Difficulty;
        PubKey m_Contributor;
    };

#pragma pack (pop)

}
