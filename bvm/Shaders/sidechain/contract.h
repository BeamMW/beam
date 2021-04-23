#pragma once
#include "../BeamDifficulty.h"

namespace Sidechain
{
    static const ShaderID s_SID = { 0xb7,0xc6,0xa5,0xcc,0x4a,0x7a,0xf9,0xff,0x7a,0x99,0x67,0xfa,0xc2,0x5c,0xb4,0x53,0x91,0xd1,0x99,0x2c,0xfe,0x24,0xce,0x16,0x1f,0xff,0xdd,0xb7,0x76,0x65,0x12,0x8c };

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
    };

    template <uint32_t nHdrs>
    struct Grow
    {
        static const uint32_t s_iMethod = 2;

        PubKey m_Contributor;
        uint32_t m_nSequence;
        BlockHeader::Prefix m_Prefix;
        BlockHeader::Element m_pSequence[nHdrs];
    };

    template <uint32_t nNodes>
    struct VerifyProof
    {
        static const uint32_t s_iMethod = 3;

        Height m_Height;
        uint32_t m_nProof;
        HashValue m_KernelID;
        Merkle::Node m_pProof[nNodes];
    };

    struct WithdrawComission
    {
        static const uint32_t s_iMethod = 4;

        PubKey m_Contributor;
        Amount m_Amount;
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
