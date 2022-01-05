#pragma once

namespace DaoVote
{
#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_State = 0;
        // don't use taag=1 for multiple data entries, it's used by Upgradable2
        static const uint8_t s_Proposal = 2;
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

        // followed by variants
    };

    struct State
    {
        Cfg m_Cfg;
        Height m_hStart;

        uint32_t get_Epoch() const {
            auto dh = Env::get_Height() - m_hStart;
            return static_cast<uint32_t>(dh / m_Cfg.m_hEpochDuration);
        }

        Proposal::ID m_iLastProposal;
        uint32_t m_iCurrentEpoch;
        uint32_t m_CurrentProposals;
        uint32_t m_NextProposals;
    };

    struct Events
    {
        struct Tags
        {
            static const uint8_t s_Proposal = 0;
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
            static const uint32_t s_iMethod = 4; // Ctor
            PubKey m_pkUser;
            Amount m_Amount;
            uint8_t m_Lock; // or unlock
        };

        struct Vote
        {
            static const uint32_t s_iMethod = 5; // Ctor
            PubKey m_pkUser;
            uint32_t m_iEpoch;
            // followed by appropriate vote per proposal
        };

    }
#pragma pack (pop)

} // namespace DaoVote
