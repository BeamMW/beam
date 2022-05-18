#pragma once

namespace Fuddle
{
    static const ShaderID s_SID = { 0x14,0x7b,0xf6,0x72,0xc2,0x26,0x21,0x39,0x9d,0xd4,0x9f,0x97,0x63,0x01,0xf5,0x63,0xaf,0x95,0x3a,0xca,0x49,0x25,0x1f,0x19,0xa8,0x64,0x4e,0x9d,0x44,0x2f,0x78,0x3f };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_Global = 2;
        static const uint8_t s_Payout = 3;
        static const uint8_t s_Letter = 4;
        static const uint8_t s_Goal = 5;
    };

    struct AmountWithAsset {
        Amount m_Amount;
        AssetID m_Aid;
    };

    struct Payout
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_Payout;

            PubKey m_pkUser;
            AssetID m_Aid;
        };

        Amount m_Amount;
    };

    struct Letter
    {
        typedef uint32_t Char;

        struct Key
        {
            uint8_t m_Tag = Tags::s_Letter;

            struct Raw {
                PubKey m_pkUser;
                Char m_Char;
            } m_Raw;
        };

        uint32_t m_Count;
        AmountWithAsset m_Price;
    };

    struct Goal
    {
        typedef uint32_t ID;

        struct Key
        {
            uint8_t m_Tag = Tags::s_Goal;
            ID m_ID;
        };

        static const uint32_t s_MaxLen = 256;

        AmountWithAsset m_Prize;
        // followed by Chars
    };

    struct GoalMax :public Goal {
        Letter::Char m_pCh[s_MaxLen];
    };

    struct Config
    {
        PubKey m_pkAdmin;
    };

    struct State
    {
        static const uint8_t s_Key = Tags::s_Global;

        Config m_Config;
        Goal::ID m_Goals;
    };

    namespace Method
    {
        struct Init
        {
            static const uint32_t s_iMethod = 0;

            Config m_Config;
        };


        struct Withdraw
        {
            static const uint32_t s_iMethod = 3;

            PubKey m_pkUser;
            AmountWithAsset m_Val;
        };

        struct SetPrice
        {
            static const uint32_t s_iMethod = 4;

            Letter::Key::Raw m_Key;
            AmountWithAsset m_Price;
        };

        struct Buy
        {
            static const uint32_t s_iMethod = 5;

            Letter::Key::Raw m_Key;
            PubKey m_pkNewOwner;
        };

        struct Mint
        {
            static const uint32_t s_iMethod = 6;

            Letter::Key::Raw m_Key;
            uint32_t m_Count;
        };

        struct SetGoal
        {
            static const uint32_t s_iMethod = 7;

            AmountWithAsset m_Prize;
            uint32_t m_Len;
            // followed by array of Chars
        };

        struct SolveGoal
        {
            static const uint32_t s_iMethod = 8;

            PubKey m_pkUser;
            Goal::ID m_iGoal;
        };

    } // namespace Method

#pragma pack (pop)

}
