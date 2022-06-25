#pragma once

namespace NameService
{
    static const ShaderID s_SID   = { 0x54,0xe7,0x7d,0x85,0xc9,0x87,0x23,0x47,0xe0,0xc1,0xa9,0xa9,0x15,0xad,0x09,0xcf,0xe3,0xb2,0x55,0x4d,0x68,0x39,0x9d,0xec,0xea,0x3e,0xd6,0x48,0xc2,0xff,0x3f,0x0c };

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
        static const Height s_PeriodValidityMax = s_PeriodValidity * 50;
        static const Height s_PeriodHold = 1440 * 90;

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
            uint8_t m_Periods;
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
            uint8_t m_Periods;
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
