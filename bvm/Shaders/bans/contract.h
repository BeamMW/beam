#pragma once

namespace NameService
{
    static const ShaderID s_SID   = { 0x49,0x54,0x75,0x1b,0x58,0x39,0x20,0x83,0xb5,0x41,0x20,0x3f,0xd1,0x72,0x29,0xec,0x20,0x87,0x25,0x9c,0x19,0x50,0x9f,0xb6,0x89,0x3f,0x4c,0x63,0xc5,0xcb,0x94,0xb2 };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_Settings = 0;
        static const uint8_t s_Domain = 1;
    };

    struct Settings
    {
        ContractID m_cidDaoVault;
        ContractID m_cidVault;
    };

    struct Price
    {
        AssetID m_Aid;
        Amount m_Amount;
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
        Price m_Price;

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

        static const Height s_PeriodValidityMax = s_PeriodValidity * 5;
    };


    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0;
            Settings m_Settings;
        };

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

        struct SetPrice
        {
            static const uint32_t s_iMethod = 5;
            Price m_Price;
            uint8_t m_NameLen;
            // followed by name
        };

        struct Buy
        {
            static const uint32_t s_iMethod = 6;
            PubKey m_pkNewOwner;
            uint8_t m_NameLen;
            // followed by name
        };

    } // namespace Method
#pragma pack (pop)

} // namespace NameService
