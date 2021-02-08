#pragma once

namespace Dummy
{
#pragma pack (push, 1)

    struct MathTest1
    {
        static const uint32_t s_iMethod = 3;
        uint64_t m_Value; // retval
        uint64_t m_Rate; // point is after 32 bits
        uint64_t m_Factor; // point is after 32 bits

        uint64_t m_Try;
        uint8_t m_IsOk; // m_Try <(?)= m_Value * m_Rate * m_Factor / 2^64
    };

    struct DivTest1
    {
        static const uint32_t s_iMethod = 4;
        uint64_t m_Nom;
        uint64_t m_Denom;
    };

    struct InfCycle
    {
        static const uint32_t s_iMethod = 5;
        uint32_t m_Val;
    };

    struct Hash1
    {
        static const uint32_t s_iMethod = 6;

        uint8_t m_pInp[10];
        uint8_t m_pRes[32];
    };

    struct Hash2
    {
        static const uint32_t s_iMethod = 7;

        uint8_t m_pInp[12];
        uint8_t m_pRes[64];
    };

    struct Hash3
    {
        static const uint32_t s_iMethod = 8;

        uint8_t m_pInp[15];
        uint8_t m_pRes[32];
    };

    struct VerifyBeamHeader
    {
        static const uint32_t s_iMethod = 9;

        BlockHeader::Full m_Hdr;

        HashValue m_RulesCfg; // host determines it w.r.t. header height. Make it a param, to make contract more flexible
        HashValue m_Hash;
        HashValue m_ChainWork0;
    };

    struct TestFarCallStack
    {
        static const uint32_t s_iMethod = 10;

        uint32_t m_iCaller;
        HashValue m_Cid;
    };

    struct TestRingSig
    {
        static const uint32_t s_iMethod = 11;

        static const uint32_t s_Ring = 5;

        PubKey m_pPks[s_Ring];
        HashValue m_Msg;

        // signature
        Secp_scalar_data m_e;
        Secp_scalar_data m_pK[s_Ring];
    };

#pragma pack (pop)

}
