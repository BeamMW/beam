#pragma once

namespace DaoVote
{
    static const ShaderID s_SID = { 0x0c,0xe9,0xd8,0x61,0xbd,0xd6,0x1e,0x75,0x78,0xec,0x7a,0x2f,0xed,0x4e,0x2e,0xef,0x2e,0x98,0xf5,0xf7,0x1f,0xb3,0x13,0x0b,0xd9,0x6e,0xae,0xaa,0xcb,0xa0,0xa4,0x74 };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_State = 0;
        // don't use tag=1 for multiple data entries, it's used by Upgradable2
        static const uint8_t s_Proposal = 2;
        static const uint8_t s_User = 3;
        static const uint8_t s_Dividend = 4;
    };

    struct Cfg
    {
        uint32_t m_hEpochDuration;
        AssetID m_Aid;
        PubKey m_pkAdmin;
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
            Amount m_DividendStake; // not necessarily equal to the total voting stake, can be less.
        } m_Current;

        struct Next {
            uint32_t m_Proposals;
            uint32_t m_iDividendEpoch;
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
            Cfg m_Cfg;
        };

        struct AddProposal
        {
            static const uint32_t s_iMethod = 3;

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
    }
#pragma pack (pop)

} // namespace DaoVote
