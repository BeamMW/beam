#pragma once

namespace VaultAnon
{
    static const ShaderID s_SID   = { 0x0f,0xfc,0x4b,0xa3,0x32,0x56,0xe3,0xee,0xf0,0x23,0x04,0x7b,0xff,0xf9,0x74,0xd9,0xe9,0xdf,0x28,0x5e,0x3b,0xd9,0x06,0x24,0x0a,0xc4,0x88,0x99,0x52,0x3d,0x79,0xf3 };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_Account = 1;
    };

    struct Account
    {
        struct KeyPrefix {
            uint8_t m_Tag = Tags::s_Account;
        };

        struct KeyBase {
            PubKey m_pkOwner;
            AssetID m_Aid;
            // custom data
        };

        struct Key0
            :public KeyPrefix
            ,public KeyBase
        {
            // custom data
        };

        struct KeyMax :public Key0 {
            uint8_t m_pCustom[KeyTag::s_MaxSize - sizeof(Key0)];
        };

        Amount m_Amount;
    };

    namespace Method
    {
        struct BaseTx
        {
            Amount m_Amount;
            uint32_t m_SizeCustom;
            Account::KeyBase m_Key;
            // custom data
        };

        struct Deposit :public BaseTx {
            static const uint32_t s_iMethod = 2;
        };

        struct Withdraw :public BaseTx {
            static const uint32_t s_iMethod = 3;
        };

    } // namespace Method
#pragma pack (pop)

} // namespace VaultAnon
