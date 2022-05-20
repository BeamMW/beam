#pragma once

namespace VaultAnon
{
    static const ShaderID s_SID   = { 0x15,0x52,0x75,0x63,0x50,0x86,0x27,0x38,0x40,0x35,0x04,0x23,0x55,0x49,0x16,0x17,0x33,0x23,0x40,0x94,0x59,0x17,0x06,0x36,0x10,0x96,0x36,0x53,0x82,0x61,0x21,0x43 };

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
