#pragma once
#include "../BeamDifficulty.h"

namespace Sidechain
{
    static const ShaderID s_SID = { 0x95,0xdb,0x47,0x0b,0xad,0xcc,0xfc,0x42,0x76,0x88,0xa5,0xa7,0xfb,0x12,0xc4,0x55,0x63,0xa7,0xf3,0x8f,0x69,0x34,0xb0,0x07,0x0f,0x25,0x11,0x47,0xbe,0xb5,0x7a,0x75, };

#pragma pack (push, 1)

    struct Immutable
    {
        HashValue m_Rules; // host determines it w.r.t. header height. Make it a param, to make contract more flexible
        uint8_t m_VerifyPoW; // for tests
        Amount m_ComissionForProof;
    };

    struct Init
        :public Immutable
    {
        static const uint32_t s_iMethod = 0;

        BlockHeader::Full m_Hdr0;

        template <bool bToShader>
        void Convert()
        {
            m_Hdr0.Convert<bToShader>();
            ConvertOrd<bToShader>(m_ComissionForProof);
        }
    };

    template <uint32_t nHdrs>
    struct Grow
    {
        static const uint32_t s_iMethod = 2;

        PubKey m_Contributor;
        uint32_t m_nSequence;
        BlockHeader::Prefix m_Prefix;
        BlockHeader::Element m_pSequence[nHdrs];

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_nSequence);
            m_Prefix.Convert<bToShader>();

            for (uint32_t i = 0; i < nHdrs; i++)
                m_pSequence[i].template Convert<bToShader>();
        }
    };

    template <uint32_t nNodes>
    struct VerifyProof
    {
        static const uint32_t s_iMethod = 3;

        Height m_Height;
        uint32_t m_nProof;
        HashValue m_KernelID;
        Merkle::Node m_pProof[nNodes];

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Height);
            ConvertOrd<bToShader>(m_nProof);
        }
    };

    struct WithdrawComission
    {
        static const uint32_t s_iMethod = 4;

        PubKey m_Contributor;
        Amount m_Amount;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Amount);
        }
    };

    struct Global
        :public Immutable
    {
        BeamDifficulty::Raw m_Chainwork;
        Height m_Height;
    };

    struct PerHeight
    {
        HashValue m_Hash;
        HashValue m_Kernels;
        //HashValue m_Definition;
        Timestamp m_Timestamp;
        uint32_t m_Difficulty;
        PubKey m_Contributor;
    };

#pragma pack (pop)

}
