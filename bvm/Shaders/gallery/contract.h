#pragma once

namespace Gallery
{
    static const ShaderID s_pSID[] = {
        { 0x14,0x7b,0xf6,0x72,0xc2,0x26,0x21,0x39,0x9d,0xd4,0x9f,0x97,0x63,0x01,0xf5,0x63,0xaf,0x95,0x3a,0xca,0x49,0x25,0x1f,0x19,0xa8,0x64,0x4e,0x9d,0x44,0x2f,0x78,0x3f },
        { 0x7c,0x43,0x7d,0xe3,0x3f,0x77,0xb3,0x6f,0x87,0x7e,0xa7,0x5c,0x05,0xf5,0xbe,0x6d,0x92,0xde,0xec,0x96,0xc5,0x0d,0x49,0xb3,0xcd,0xc7,0x05,0xa6,0x8f,0x8a,0xf8,0xca },
    };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_Artist = 1;
        static const uint8_t s_Payout = 2;
        static const uint8_t s_Masterpiece = 4;
        static const uint8_t s_Impression = 5;
    };

    struct Artist
    {
        struct Key {
            uint8_t m_Tag = Tags::s_Artist;
            PubKey m_pkUser;
        };

        Height m_hRegistered;

        // followed by label
        static const uint32_t s_LabelMaxLen = 100;
    };

    struct AmountWithAsset {
        Amount m_Amount;
        AssetID m_Aid;
    };

    struct Masterpiece
    {
        typedef uint32_t ID; // in big-endian format, for more convenient enumeration sorted by ID

        struct Key {
            uint8_t m_Tag = Tags::s_Masterpiece;
            ID m_ID;
        };

        PubKey m_pkOwner;
        AssetID m_Aid; // set when it's taken out of gallery

        AmountWithAsset m_Price;
    };

    struct Impression
    {
        struct ID
        {
            Masterpiece::ID m_MasterpieceID;
            PubKey m_pkUser;
        };

        struct Key
        {
            uint8_t m_Tag = Tags::s_Impression;
            ID m_ID;
        };

        uint32_t m_Value; // 0 = none, 1 = like, etc.
    };

    struct Payout
    {
        struct Key {
            uint8_t m_Tag = Tags::s_Payout;
            PubKey m_pkUser;
            AssetID m_Aid;
            Masterpiece::ID m_ID;
        };

        Amount m_Amount;
    };

    struct Config
    {
        PubKey m_pkAdmin;
        AmountWithAsset m_VoteReward;
    };

    struct State
    {
        static const uint8_t s_Key = 0;

        Config m_Config;
        Masterpiece::ID m_Exhibits;
        Amount m_VoteBalance;
    };

    struct Events
    {
        struct Add {
            struct Key {
                uint8_t m_Prefix = 0;
                Masterpiece::ID m_ID;
                PubKey m_pkArtist;
            };
            // data is the exhibit itself
        };

        struct Sell {
            struct Key {
                uint8_t m_Prefix = 1;
                Masterpiece::ID m_ID;
            };

            AmountWithAsset m_Price;
            uint8_t m_HasAid;
        };
    };

    namespace Method
    {
        struct Init
        {
            static const uint32_t s_iMethod = 0;
            Config m_Config;
        };

        struct ManageArtist
        {
            static const uint32_t s_iMethod = 10;

            PubKey m_pkArtist;
            uint32_t m_LabelLen; // set Artist::s_LabelMaxLen to remove
            // followed by label
        };

        struct AddExhibit
        {
            static const uint32_t s_iMethod = 3;

            PubKey m_pkArtist;
            uint32_t m_Size;
            // followed by the data
        };

        struct SetPrice
        {
            static const uint32_t s_iMethod = 4;

            Masterpiece::ID m_ID;
            AmountWithAsset m_Price;
        };

        struct Buy
        {
            static const uint32_t s_iMethod = 5;

            Masterpiece::ID m_ID;
            PubKey m_pkUser;
            uint8_t m_HasAid;
            Amount m_PayMax;
        };

        struct Withdraw
        {
            static const uint32_t s_iMethod = 6;

            Payout::Key m_Key;
            Amount m_Value;
        };

        struct CheckPrepare
        {
            static const uint32_t s_iMethod = 7;

            Masterpiece::ID m_ID;
        };

        struct CheckOut
        {
            static const uint32_t s_iMethod = 8;

            Masterpiece::ID m_ID;
        };

        struct CheckIn
        {
            static const uint32_t s_iMethod = 9;

            Masterpiece::ID m_ID;
            PubKey m_pkUser;
        };

        struct Vote
        {
            static const uint32_t s_iMethod = 11;
            Impression::ID m_ID;
            Impression m_Impression;
        };

        struct AddVoteRewards
        {
            static const uint32_t s_iMethod = 12;
            Amount m_Amount;
        };

        struct AdminDelete
        {
            static const uint32_t s_iMethod = 13;
            Masterpiece::ID m_ID;
        };

        struct Transfer
        {
            static const uint32_t s_iMethod = 14;
            Masterpiece::ID m_ID;
            PubKey m_pkNewOwner;
        };

    } // namespace Method

#pragma pack (pop)

}
