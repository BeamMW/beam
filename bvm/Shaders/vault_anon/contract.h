#pragma once

namespace VaultAnon
{
    static const ShaderID s_SID   = { 0x9f,0xa1,0x58,0x18,0x2f,0xfd,0x78,0xe5,0x3d,0x7b,0x4f,0xe2,0x3c,0x12,0x42,0x36,0xb4,0xeb,0x0e,0x47,0x34,0x57,0x1a,0x2f,0xb5,0x97,0x5e,0x0c,0x2b,0x51,0x80,0x6d };

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

        static const uint32_t s_CustomMaxSize = KeyTag::s_MaxSize - sizeof(Key0);

        struct KeyMax :public Key0 {
            uint8_t m_pCustom[s_CustomMaxSize];
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
