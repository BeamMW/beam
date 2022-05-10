#pragma once

namespace VaultAnon
{
    static const ShaderID s_SID   = { 0x81,0x44,0xad,0xae,0xd6,0x87,0x4d,0x62,0xca,0x79,0xec,0x4e,0x82,0x71,0x9c,0x33,0x78,0x7a,0x71,0x67,0xfe,0xc9,0xae,0x90,0xd7,0xca,0xe6,0x44,0x8d,0x5c,0x9d,0xd7 };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_Account = 3;
        static const uint8_t s_Deposit = 4;
    };

    struct Account
    {
        struct Key {
            uint8_t m_Tag = Tags::s_Account;
            PubKey m_Pk;
        };

        static const uint8_t s_TitleLenMax = 100;
        // title?
    };

    struct Deposit
    {

        struct Key {
            uint8_t m_Tag = Tags::s_Deposit;
            PubKey m_SpendKey;
        };

        PubKey m_SenderKey;
        AssetID m_Aid;
        Amount m_Amount;
    };

    namespace Method
    {
        struct SetAccount
        {
            static const uint32_t s_iMethod = 2;

            PubKey m_Pk;
            uint8_t m_TitleLen;
        };

        struct Send
        {
            static const uint32_t s_iMethod = 3;

            PubKey m_SpendKey;
            Deposit m_Deposit;
        };

        struct Receive
        {
            static const uint32_t s_iMethod = 4;
            PubKey m_SpendKey;
        };

    } // namespace Method
#pragma pack (pop)

} // namespace VaultAnon
