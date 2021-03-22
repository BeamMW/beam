#pragma once

namespace Faucet
{
    static const ShaderID s_SID = { 0xe3,0x21,0x90,0x44,0x42,0x00,0x0f,0xfd,0x2d,0x2f,0xf0,0x46,0x3f,0x8a,0x77,0x21,0x1b,0x82,0xe4,0x61,0xd3,0x97,0xe1,0xb2,0x20,0x8c,0xc9,0x3b,0xb6,0xa1,0x04,0x65 };

#pragma pack (push, 1)

    struct Params {
        static const uint32_t s_iMethod = 0;

        Height m_BacklogPeriod;
        Amount m_MaxWithdraw;
    };

    struct Deposit {
        static const uint32_t s_iMethod = 2;
        AssetID m_Aid;
        Amount m_Amount;
    };

    struct Key {
        PubKey m_Account;
        AssetID m_Aid;
    };

    struct Withdraw {
        static const uint32_t s_iMethod = 3;
        Key m_Key;
        Amount m_Amount;
    };

    struct AccountData {
        Height m_h0;
        Amount m_Amount;
    };

#pragma pack (pop)

}
