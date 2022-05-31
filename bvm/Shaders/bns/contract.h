#pragma once

namespace NameService
{
    static const ShaderID s_SID   = { 0x76,0xc2,0x1d,0x18,0x04,0x67,0x3c,0x46,0x22,0x96,0x8a,0xf9,0x48,0x61,0xcd,0xa7,0x87,0x4a,0xbe,0x16,0x0d,0xac,0xc7,0xb7,0x34,0xf5,0x53,0xb2,0x76,0x81,0x04,0x7e };

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
