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

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Value);
            ConvertOrd<bToShader>(m_Rate);
            ConvertOrd<bToShader>(m_Factor);
            ConvertOrd<bToShader>(m_Try);
            ConvertOrd<bToShader>(m_IsOk);
        }
    };

    struct DivTest1
    {
        static const uint32_t s_iMethod = 4;
        uint64_t m_Nom;
        uint64_t m_Denom;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Nom);
            ConvertOrd<bToShader>(m_Denom);
        }
    };

    struct InfCycle
    {
        static const uint32_t s_iMethod = 5;

        uint32_t m_Val;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Val);
        }
    };

    struct Hash1
    {
        static const uint32_t s_iMethod = 6;

        uint8_t m_pInp[10];
        uint8_t m_pRes[32];

        template <bool bToShader>
        void Convert()
        {
        }
    };

    struct Hash2
    {
        static const uint32_t s_iMethod = 7;

        uint8_t m_pInp[12];
        uint8_t m_pRes[64];

        template <bool bToShader>
        void Convert()
        {
        }
    };

    struct Hash3
    {
        static const uint32_t s_iMethod = 8;

        uint8_t m_pInp[15];
        uint8_t m_pRes[32];

        template <bool bToShader>
        void Convert()
        {
        }
    };

    struct VerifyBeamHeader
    {
        static const uint32_t s_iMethod = 9;

        struct Hdr
        {
            Height m_Height;
            HashValue m_Prev;
            HashValue m_ChainWork; // not hash, just same size
            HashValue m_Kernels;
            HashValue m_Definition;
            Timestamp m_TimeStamp;
            // pow
            uint8_t m_pIndices[104];
            uint8_t m_pNonce[8];
            uint32_t m_DifficultyPacked;

            template <bool bToShader>
            void Convert()
            {
                ConvertOrd<bToShader>(m_Height);
                ConvertOrd<bToShader>(m_TimeStamp);
                ConvertOrd<bToShader>(m_DifficultyPacked);
            }
        };

        Hdr m_Hdr;
        HashValue m_RulesCfg; // host determines it w.r.t. header height. Make it a param, to make contract more flexible
        HashValue m_HashForPoW;
        HashValue m_Hash;

        template <bool bToShader>
        void Convert()
        {
            m_Hdr.Convert<bToShader>();
        }
    };

#pragma pack (pop)

}
