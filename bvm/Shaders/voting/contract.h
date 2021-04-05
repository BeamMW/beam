#pragma once

namespace Voting
{
    static const ShaderID s_SID = { 0x58,0xde,0x17,0xbd,0xe5,0x78,0x8c,0x5f,0x98,0xab,0x08,0x87,0x68,0x5b,0x88,0xb2,0xe5,0xa4,0x07,0x0f,0x77,0x6b,0xf8,0x84,0x04,0x1b,0x24,0xf5,0x5b,0xfc,0x07,0x2a };
    
    static const ContractID s_CID = { 0xac,0x79,0x3b,0x33,0xf7,0x05,0x51,0x3b,0x0d,0x3d,0xd7,0x50,0x8b,0x24,0x71,0x60,0xf2,0x39,0x93,0xeb,0x20,0x5d,0x55,0xa0,0xd2,0x28,0x38,0xa6,0xd9,0xf2,0xd9,0x7a };

#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    struct Proposal
    {
        typedef HashValue ID;

        static const uint32_t s_MaxVariants = 128;

        struct Params {
            Height m_hMin;
            Height m_hMax;
            AssetID m_Aid;
        } m_Params;
        // followed by array of amounts for each vote
    };

    struct Proposal_MaxVars
        :public Proposal
    {
        Amount m_pAmount[s_MaxVariants];
    };

    struct OpenProposal
    {
        static const uint32_t s_iMethod = 2;

        Proposal::ID m_ID;
        Proposal::Params m_Params;
        uint32_t m_Variants;
    };

    struct UserKey {
        Proposal::ID m_ID;
        PubKey m_Pk;
    };

    struct UserRequest
        :public UserKey
    {
        Amount m_Amount;
    };

    struct Vote
        :public UserRequest
    {
        static const uint32_t s_iMethod = 3;
        uint32_t m_Variant;
    };

    struct Withdraw
        :public UserRequest
    {
        static const uint32_t s_iMethod = 4;
    };

#pragma pack (pop)

}
