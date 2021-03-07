#pragma once
#include "../BeamDifficulty.h"

namespace Sidechain
{
    static const ShaderID s_SID = { 0xbd,0xbb,0x2a,0xa0,0x02,0x2c,0x9a,0xca,0xd4,0xf7,0xc1,0x1f,0x4a,0x26,0x37,0xee,0x2c,0xaa,0x01,0x8a,0xc5,0xdc,0x3e,0x8c,0x6c,0xc7,0x8c,0xa1,0x5e,0x01,0xd9,0x38 };

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
