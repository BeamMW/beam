#pragma once

namespace Faucet2
{
    static const ShaderID s_SID = { 0xe0,0x84,0xa2,0x7d,0x3b,0xd6,0x75,0xf3,0xe2,0xca,0x05,0x6d,0x1b,0x5b,0x67,0xd6,0x6e,0xbd,0x05,0x45,0x0d,0xb8,0x04,0xd1,0x6d,0xef,0x6f,0xb1,0xe9,0x09,0xbc,0xc8 };

#pragma pack (push, 1)

    struct Epoch
    {
        Height m_Height;
        Amount m_Amount;
    };

    struct Params
    {
        Epoch m_Limit;
        PubKey m_pkAdmin;
    };

    struct State
    {
        static const uint8_t s_Key = 0;

        Epoch m_Epoch;
        Params m_Params;
        uint8_t m_Enabled;

        bool UpdateEpoch(Height h)
        {
            if (h - m_Epoch.m_Height < m_Params.m_Limit.m_Height)
                return false;

            m_Epoch.m_Height = h;
            m_Epoch.m_Amount = m_Params.m_Limit.m_Amount;
            return true;
        }
    };

    struct AmountWithAsset {
        Amount m_Amount;
        AssetID m_Aid;
    };


    namespace Method
    {
        struct Create {
            static const uint32_t s_iMethod = 0;
            Params m_Params;
        };

        struct Deposit {
            static const uint32_t s_iMethod = 2;
            AmountWithAsset m_Value;
        };

        struct Withdraw {
            static const uint32_t s_iMethod = 3;
            AmountWithAsset m_Value;
        };

        struct AdminCtl {
            static const uint32_t s_iMethod = 4;
            uint8_t m_Enable;
        };

        struct AdminWithdraw {
            static const uint32_t s_iMethod = 5;
            AmountWithAsset m_Value;
        };
    }

#pragma pack (pop)

}
