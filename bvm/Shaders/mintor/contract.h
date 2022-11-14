#pragma once

namespace Mintor
{
    static const ShaderID s_SID = { 0x0b,0xd2,0x69,0xde,0xc1,0xa5,0xcf,0xc6,0x4c,0x3c,0x24,0x93,0xe7,0x27,0xce,0x80,0x46,0xf4,0x02,0x57,0x59,0xf0,0xbe,0x30,0xf8,0xc8,0x9b,0x60,0xac,0xd8,0xd5,0x2e };

#pragma pack (push, 1)

    struct AmountBig
    {
        Amount m_Lo;
        Amount m_Hi;

        void operator += (Amount val)
        {
            m_Lo += val;
            if (m_Lo < val)
                Env::Halt_if(!++m_Hi);
        }

        void operator -= (Amount val)
        {
            if (m_Lo < val)
                Env::Halt_if(!m_Hi--);
            m_Lo -= val;
        }
    };

    struct Tags
    {
        static const uint8_t s_Settings = 0;
        static const uint8_t s_Token = 1;
    };

    struct PubKeyFlag
    {
        static const uint8_t s_Cid = 2; // pk.X == cid
    };

    struct Settings
    {
        ContractID m_cidDaoVault;
        Amount m_IssueFee;
    };

    struct Token
    {
        struct Key {
            uint8_t m_Tag = Tags::s_Token;
            AssetID m_Aid;
        };

        AmountBig m_Minted;
        AmountBig m_Limit;
        PubKey m_pkOwner;
    };

    namespace Method
    {
        struct Base {
            AssetID m_Aid;
        };

        struct Init
        {
            static const uint32_t s_iMethod = 0;

            Settings m_Settings;
        };

        struct View :public Base
        {
            static const uint32_t s_iMethod = 2;

            Token m_Result; // seto to 0 if no such a token
        };

        struct CreateToken :public Base
        {
            static const uint32_t s_iMethod = 3;

            AmountBig m_Limit;
            PubKey m_pkOwner;
            uint32_t m_MetadataSize;
            // followed by metadata
        };

        struct Withdraw :public Base
        {
            static const uint32_t s_iMethod = 4;

            Amount m_Value;
        };
    }
#pragma pack (pop)

} // namespace Mintor
