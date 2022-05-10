#pragma once

namespace VaultAnon
{
    static const ShaderID s_SID   = { 0x20,0x62,0x26,0xea,0x8c,0x6c,0x01,0x2b,0xae,0xbf,0x49,0xdb,0xb2,0x69,0x6e,0x41,0x05,0x10,0x05,0x4c,0x21,0x26,0xe1,0x46,0xe4,0xad,0xc7,0xca,0xf6,0xde,0x70,0x13 };

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
