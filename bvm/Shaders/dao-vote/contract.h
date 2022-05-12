#pragma once
#include "../upgradable3/contract.h"

namespace DaoVote
{
    static const ShaderID s_SID_0 = { 0x66,0x31,0x58,0xda,0x3f,0xae,0xe9,0xbc,0x7b,0x1c,0x1e,0xa9,0x45,0x96,0x3c,0x51,0x3e,0x89,0xbe,0xae,0xb7,0x10,0xe2,0x39,0x65,0x7a,0x5c,0x78,0xa4,0xc0,0xa4,0x4d };
    static const ShaderID s_SID_1 = { 0x81,0xbe,0x59,0xc8,0x46,0x3f,0x4b,0xb9,0x52,0x22,0xe0,0xb0,0x7e,0x71,0x6d,0xc6,0x25,0xb4,0xac,0x72,0xbd,0x3e,0x1b,0x1b,0xf4,0xba,0x56,0x9e,0x49,0x40,0x1a,0x79 };

    static const ShaderID& s_SID = s_SID_1; // current version

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_State = 0;
        static const uint8_t s_Proposal = 2;
        static const uint8_t s_User = 3;
        static const uint8_t s_Dividend = 4;
        static const uint8_t s_Moderator = 5;
    };

    struct Cfg
    {
        uint32_t m_hEpochDuration;
        AssetID m_Aid;
        PubKey m_pkAdmin;
    };

    struct Moderator
    {
        struct Key {
            uint8_t m_Tag = Tags::s_Moderator;
            PubKey m_pk;
        };

        Height m_Height;
    };

    struct Proposal
    {
        typedef uint32_t ID;

        struct Key {
            uint8_t m_Tag = Tags::s_Proposal;
            ID m_ID;
        };

        static const uint32_t s_VariantsMax = 64;
        static const uint32_t s_ProposalsPerEpochMax = 50;

        // followed by variants
    };

    struct EpochStats
    {
        Amount m_StakeActive;
        Amount m_StakeVoted;
    };

    struct ProposalMax
        :public Proposal
    {
        Amount m_pVariant[s_VariantsMax];
    };

    struct AssetAmount {
        AssetID m_Aid;
        Amount m_Amount;
    };

    struct Dividend0
    {
        struct Key {
            uint8_t m_Tag = Tags::s_Dividend;
            uint32_t m_iEpoch;
        };

        Amount m_Stake;

        // followed by array of AssetAmount
        static const uint32_t s_AssetsMax = 64;
    };

    struct DividendMax
        :public Dividend0
    {
        AssetAmount m_pArr[s_AssetsMax];
    };

    struct State
    {
        Cfg m_Cfg;
        Height m_hStart;

        uint32_t get_Epoch() const {
            auto dh = Env::get_Height() - m_hStart;
            return 1u + static_cast<uint32_t>(dh / m_Cfg.m_hEpochDuration);
        }

        Proposal::ID m_iLastProposal;

        Proposal::ID get_Proposal0() const {
            return m_iLastProposal - m_Next.m_Proposals - m_Current.m_Proposals;
        }

        struct Current {
            uint32_t m_iEpoch;
            uint32_t m_Proposals;
            uint32_t m_iDividendEpoch; // set to 0 if no reward
            EpochStats m_Stats;
        } m_Current;

        struct Next {
            uint32_t m_Proposals;
            uint32_t m_iDividendEpoch;
            Amount m_StakePassive;
        } m_Next;
    };

    struct User
    {
        struct Key {
            uint8_t m_Tag = Tags::s_User;
            PubKey m_pk;
        };

        uint32_t m_iEpoch;
        Proposal::ID m_iProposal0;
        uint32_t m_iDividendEpoch;
        Amount m_Stake;
        Amount m_StakeNext;

        static const uint8_t s_NoVote = 0xff;
        static_assert(s_NoVote >= Proposal::s_VariantsMax);

        // followed by the votes for the epoch's proposal
    };

    struct VotesMax {
        uint8_t m_pVotes[Proposal::s_ProposalsPerEpochMax];
    };

    struct UserMax
        :public User
        ,public VotesMax
    {
    };

    struct Events
    {
        struct Tags
        {
            static const uint8_t s_Proposal = 0;
            static const uint8_t s_UserVote = 1;
        };

        struct Proposal
        {
            struct Key {
                uint8_t m_Tag = Tags::s_Proposal;
                DaoVote::Proposal::ID m_ID_be; // in big endian form, for more convenient enumeration
            };

            uint32_t m_Variants;
            // followed by arbitrary text
        };

        struct UserVote
        {
            struct Key {
                uint8_t m_Tag = Tags::s_UserVote;
                PubKey m_pk;
                DaoVote::Proposal::ID m_ID_0_be;
            };

            Amount m_Stake;
            // followed by votes
        };

        struct UserVoteMax
            :public UserVote
            ,public VotesMax
        {
        };
    };

    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0; // Ctor
            Upgradable3::Settings m_Upgradable;
            Cfg m_Cfg;
        };

        struct AddProposal
        {
            static const uint32_t s_iMethod = 3;

            PubKey m_pkModerator;
            uint32_t m_TxtLen;
            Events::Proposal m_Data;
            // followed by text
        };

        struct MoveFunds
        {
            static const uint32_t s_iMethod = 4;
            PubKey m_pkUser;
            Amount m_Amount;
            uint8_t m_Lock; // or unlock
        };

        struct Vote
        {
            static const uint32_t s_iMethod = 5;
            PubKey m_pkUser;
            uint32_t m_iEpoch;
            // followed by appropriate vote per proposal
        };

        struct AddDividend
        {
            static const uint32_t s_iMethod = 6;
            AssetAmount m_Val;
        };

        struct GetResults
        {
            static const uint32_t s_iMethod = 7;
            Proposal::ID m_ID;
            uint32_t m_Variants; // in/out
            uint8_t m_Finished;
            // followed by variants
        };

        struct SetModerator
        {
            static const uint32_t s_iMethod = 8;
            PubKey m_pk;
            uint8_t m_Enable;
            // followed by variants
        };
    }
#pragma pack (pop)

} // namespace DaoVote
