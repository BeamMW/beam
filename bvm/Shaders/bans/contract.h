#pragma once
#include "../upgradable3/contract.h"
#include "../oracle2/contract.h"

namespace NameService
{
    static const ShaderID s_pSID[] = {
        { 0x96,0x87,0x6e,0x38,0x58,0x53,0x96,0xe8,0xba,0x72,0x5c,0x42,0x2f,0x29,0x13,0x0b,0xfa,0x0b,0x83,0x4c,0x64,0xa8,0x39,0xac,0x2c,0xa1,0xc0,0xa2,0x6d,0xdb,0x4b,0xae },
        { 0x3d,0x4e,0x2d,0x69,0xcb,0x67,0x74,0x47,0xcb,0x9b,0xaa,0xe3,0xa4,0x6e,0x77,0x7d,0xbc,0xe8,0x57,0xdd,0x7c,0x81,0xcf,0x95,0xce,0x55,0x43,0x66,0xfb,0xcc,0xad,0x82 },
    };

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
        ContractID m_cidOracle;
        Height m_h0;
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

        static const Amount s_PriceTok3 = g_Beam2Groth * 320;
        static const Amount s_PriceTok4 = g_Beam2Groth * 120;
        static const Amount s_PriceTok5 = g_Beam2Groth * 10;

        static Amount get_PriceTok(uint32_t nNameLen)
        {
            return
                (nNameLen <= 3) ? s_PriceTok3 :
                (nNameLen <= 4) ? s_PriceTok4 :
                s_PriceTok5;
        }

        static Amount get_PriceBeams(Amount priceTok, const Oracle2::ValueType& rate)
        {
            return MultiPrecision::Float(priceTok) / rate;
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
			Upgradable3::Settings m_Upgradable;
			Settings m_Settings;
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

        struct Register
        {
            static const uint32_t s_iMethod = 7;

            PubKey m_pkOwner;
            uint8_t m_Periods;
            uint8_t m_NameLen;
            // followed by name
        };

    } // namespace Method
#pragma pack (pop)

} // namespace NameService
