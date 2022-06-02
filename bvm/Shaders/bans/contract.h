#pragma once

namespace NameService
{
    static const ShaderID s_SID   = { 0x45,0xfd,0xa9,0xe1,0x4f,0xa7,0x1b,0x0d,0x45,0x1c,0x7c,0xc9,0x62,0x50,0x24,0x68,0x2a,0xab,0x54,0x22,0x62,0xf7,0x3b,0x84,0x0c,0xef,0x73,0x17,0xfa,0xa1,0x44,0x1c };

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

        bool IsExpired(Height h) const
        {
            return m_hExpire + s_PeriodHold <= h;
        }

        static const Amount s_Price3 = g_Beam2Groth * 640;
        static const Amount s_Price4 = g_Beam2Groth * 16;
        static const Amount s_Price5 = g_Beam2Groth * 5;

        static Amount get_Price(uint32_t nNameLen)
        {
            return
                (nNameLen <= 3) ? s_Price3 :
                (nNameLen <= 4) ? s_Price4 :
                s_Price5;
        }

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
