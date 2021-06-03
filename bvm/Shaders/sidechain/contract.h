#pragma once
#include "../BeamDifficulty.h"

namespace Sidechain
{
    static const ShaderID s_SID = { 0xea,0x2f,0xa3,0x54,0x9f,0x7a,0xcd,0x72,0x79,0xe1,0x36,0x81,0x2d,0xc7,0xc2,0x30,0x6a,0x70,0xa0,0xd1,0x47,0xef,0x39,0xbe,0xaf,0x5b,0xdd,0x87,0x2c,0x7c,0xc1,0x40 };

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
