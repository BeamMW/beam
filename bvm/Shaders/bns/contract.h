#pragma once

namespace NameService
{
    static const ShaderID s_SID   = { 0x7c,0x36,0x52,0x74,0x5c,0x0b,0xba,0x4b,0xb6,0x9e,0x8e,0x36,0x80,0x22,0x8b,0x54,0x13,0x47,0xae,0x6b,0x38,0x37,0xcf,0x23,0x45,0x50,0x85,0xaa,0xa2,0xf4,0x58,0x86 };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_Domain = 1;
    };

    struct Domain
    {
        static const uint32_t s_MaxLen = 64;
        static const uint32_t s_MinLen = 3;

        static bool IsValidChar(char c)
        {
            switch (c)
            {
            case '_':
            case '-':
            case '~':
                return true;
            }
            return
                ((c >= 'a') && (c <= 'z')) ||
                ((c >= '0') && (c <= '9'));
        }

        struct Key0
        {
            uint8_t m_Tag = Tags::s_Domain;
            char m_sz[s_MinLen];
        };

        struct KeyMax :public Key0
        {
            char m_szExtra[s_MaxLen - s_MinLen];
        };

        PubKey m_pkOwner;
        Height m_hExpire;

        static const Amount s_Price3 = g_Beam2Groth * 640;
        static const Amount s_Price4 = g_Beam2Groth * 16;
        static const Amount s_Price5 = g_Beam2Groth * 5;

        static const Height s_PeriodValidity = 1440 * 365;
        static const Height s_PeriodHold = 1440 * 90;
    };


    namespace Method
    {
        struct Register
        {
            static const uint32_t s_iMethod = 2;

            PubKey m_pkOwner;
            uint8_t m_NameLen;
            // followed by name
        };

        struct SetOwner
        {
            static const uint32_t s_iMethod = 3;

            PubKey m_pkNewOwner;
            uint8_t m_NameLen;
            // followed by name
        };

        struct Extend
        {
            static const uint32_t s_iMethod = 4;
            uint8_t m_NameLen;
            // followed by name
        };

    } // namespace Method
#pragma pack (pop)

} // namespace NameService
