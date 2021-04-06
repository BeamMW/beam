#pragma once

namespace Voting
{
    static const ShaderID s_SID = { 0x15,0xac,0x21,0x69,0xa6,0x40,0x1a,0x2a,0x3c,0x27,0xed,0xcf,0xb5,0x49,0x6c,0xae,0xcd,0x3e,0x91,0xe9,0x48,0x7b,0xa6,0xe2,0x87,0xd0,0x1c,0x9d,0xe4,0x35,0xe7,0x66 };

    static const ContractID s_CID = { 0xf5,0x1d,0x60,0xd2,0x09,0xca,0xe2,0x3a,0xb4,0x39,0x8a,0x67,0x88,0xd1,0x52,0x6b,0x34,0x7b,0x32,0x9c,0xb1,0x86,0x30,0xc0,0x99,0x16,0x2a,0x68,0x56,0x06,0xef,0x91 };

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

    struct Read
    {
        static const uint32_t s_iMethod = 5;

        Proposal::ID m_ID; // in
        uint32_t m_Variants; // in/out. On input specifies the buf size, on output contains the actual number of variants. 0 if failed to read
        Proposal m_Res; // should be big enough to fit the data.
    };

#pragma pack (pop)

}
