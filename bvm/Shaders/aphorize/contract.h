#pragma once

namespace Aphorize
{
#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    struct Cfg
    {
        PubKey m_Moderator;
        Height m_hPeriod;
        Amount m_PriceSubmit;
    };

    struct Events
    {
        struct RoundEnd {
            static const uint8_t s_Key = 0;
            uint32_t m_iWinner;
        };

        struct Submitted {
            static const uint8_t s_Key = 1;
        };

        struct Ban {
            static const uint8_t s_Key = 2;
            uint32_t m_iVariant;
        };
    };

    struct Account
    {
        typedef PubKey Key;

        Amount m_Avail;
        Amount m_Locked;
        Height m_hRoundStart; // money locked till the end of the round
    };

    struct Variant
    {
        PubKey m_Account;
    };

    struct State
    {
        static const uint8_t s_Key = 0;

        Cfg m_Cfg;
        Height m_hRoundStart;

        static const uint32_t s_MaxVariants = 1000;

        // followed by array of Amount per variant
    };

    struct StatePlus
        :public State
    {
        Amount m_pArr[s_MaxVariants];
    };

    struct Ctor {
        static const uint32_t s_iMethod = 0;
        Cfg m_Cfg;
    };

    struct Submit
    {
        static const uint32_t s_iMethod = 2;

        PubKey m_Pk;

        static const uint32_t s_MaxLen = 200;

        uint32_t m_Len;
        // followed by UTF-8 encoded text
    };

    struct Vote
    {
        static const uint32_t s_iMethod = 3;

        PubKey m_Pk;
        Amount m_Amount;
        uint32_t m_iVariant;
    };

    struct Ban
    {
        static const uint32_t s_iMethod = 4;
        uint32_t m_iVariant;
    };

    struct Withdraw
    {
        static const uint32_t s_iMethod = 5;
        PubKey m_Pk;
        Amount m_Amount;
    };

#pragma pack (pop)

}
