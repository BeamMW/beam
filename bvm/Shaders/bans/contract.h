#pragma once
#include "../upgradable3/contract.h"

namespace NameService
{
    static const ShaderID s_pSID[] = {
        { 0x09,0x68,0xb9,0x20,0x08,0xc5,0x32,0x90,0xba,0xfd,0x67,0x11,0xd4,0x91,0x50,0xe2,0xef,0xc2,0xc4,0x57,0xe5,0xf5,0xb9,0x27,0x7e,0xab,0x5a,0x31,0x9e,0x1a,0x95,0x18 },
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
