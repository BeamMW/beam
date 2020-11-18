#pragma once

namespace Faucet
{
    static const ShaderID s_SID = { 0x75,0xa0,0x05,0x3f,0x5f,0x2a,0xcf,0x2c,0x37,0x0d,0x98,0x2a,0xa4,0x27,0x22,0x3a,0x8b,0xfc,0x50,0xdf,0xde,0xf6,0xae,0xb6,0x9c,0xe4,0x6f,0x99,0xd4,0x5b,0x9b,0xc2 };

#pragma pack (push, 1)

    struct Params {
        static const uint32_t s_iMethod = 0;

        Height m_BacklogPeriod;
        Amount m_MaxWithdraw;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_BacklogPeriod);
            ConvertOrd<bToShader>(m_MaxWithdraw);
        }
    };

    struct Deposit {
        static const uint32_t s_iMethod = 2;
        AssetID m_Aid;
        Amount m_Amount;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Aid);
            ConvertOrd<bToShader>(m_Amount);
        }
    };

    struct Key {
        PubKey m_Account;
        AssetID m_Aid;
    };

    struct Withdraw {
        static const uint32_t s_iMethod = 3;
        Key m_Key;
        Amount m_Amount;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Key.m_Aid);
            ConvertOrd<bToShader>(m_Amount);
        }
    };

    struct AccountData {
        Height m_h0;
        Amount m_Amount;
    };

#pragma pack (pop)

}
