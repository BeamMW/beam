#pragma once
#include "../BeamDifficulty.h"

namespace Sidechain
{
    static const ShaderID s_SID = { 0x41,0x37,0xb1,0xee,0x8c,0x55,0xf1,0xa3,0xf8,0x17,0xcf,0x7b,0x1c,0x07,0x95,0xa1,0xd9,0xfd,0x07,0x53,0xe8,0x49,0xe7,0x7d,0x9b,0xef,0xf0,0x94,0xcd,0x28,0x15,0x3e };

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
