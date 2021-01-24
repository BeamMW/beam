#pragma once
#include "../BeamDifficulty.h"

namespace Pipe
{
#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    static const ShaderID s_SID = { 0x3b,0x91,0xdc,0xd4,0xb6,0x92,0xaa,0x0e,0x05,0x17,0xc7,0x70,0xae,0xe8,0x10,0x89,0x9a,0xd2,0x25,0x3e,0xba,0x7d,0x79,0xfe,0xe0,0xfb,0x9a,0xad,0xc7,0x84,0xcc,0x7c };

    struct Cfg
    {
        struct Out {
            uint32_t m_CheckpointMaxMsgs;
            uint32_t m_CheckpointMaxDH;
        } m_Out;

        struct In {
            HashValue m_RulesRemote;
            Amount m_ComissionPerMsg;
            Amount m_StakeForRemote;
            Height m_hDisputePeriod;
            Height m_hContenderWaitPeriod;
            uint8_t m_FakePoW; // for tests
        } m_In;
    };

    struct Create
    {
        static const uint32_t s_iMethod = 0;
        Cfg m_Cfg;
    };

    struct SetRemote
    {
        static const uint32_t s_iMethod = 2;
        ContractID m_cid;
    };

    struct PushLocal0
    {
        static const uint32_t s_iMethod = 3;

        ContractID m_Receiver;
        uint32_t m_MsgSize;
        // followed by the message
    };

    struct PushRemote0
    {
        static const uint32_t s_iMethod = 4;

        PubKey m_User;

        struct Flags {
            static const uint8_t Msgs = 1; // has being-transferred message hashes
            static const uint8_t Hdr0 = 2; // has root header and proof of the checkpoint
            static const uint8_t HdrsUp = 4; // has new headers above hdr0
            static const uint8_t HdrsDown = 8; // has new headers below hdr0
            static const uint8_t Reset = 0x10; // drop all the current headers. Needed if attacker deliberately added fake header on top of the honest chain
        };

        uint8_t m_Flags;

        // followed by message variable data
    };

    struct FinalyzeRemote
    {
        static const uint32_t s_iMethod = 5;
        uint8_t m_DepositStake; // instead of immediate withdraw. Can be used by other user to enforce checkpoint finalization
    };

    struct VerifyRemote0
    {
        static const uint32_t s_iMethod = 6;

        uint32_t m_iCheckpoint;
        uint32_t m_iMsg;
        ContractID m_Sender;
        uint32_t m_MsgSize;
        Height m_Height;
        uint8_t m_Public; // original receiver was set to zero
        uint8_t m_Wipe; // wipe the message after verification. Allowed only for private messages (i.e. sent specifically to the caller contract)
        // followed by the message
    };

    struct Withdraw
    {
        static const uint32_t s_iMethod = 7;

        PubKey m_User;
        Amount m_Amount;
    };

    struct KeyType
    {
        static const uint8_t OutMsg = 1;
        static const uint8_t OutCheckpoint = 2;
        static const uint8_t UserInfo = 3;
        static const uint8_t VariantHdr = 5;
        static const uint8_t InpCheckpoint = 6;
        static const uint8_t Variant = 7;
        static const uint8_t StateIn = 8;
        static const uint8_t StateOut = 9;
    };


    struct MsgHdr
    {
        struct Key
        {
            uint8_t m_Type = KeyType::OutMsg;
            // big-endian, for simpler enumeration by app shader
            uint32_t m_iCheckpoint_BE;
            uint32_t m_iMsg_BE;
        };

        ContractID m_Sender;
        ContractID m_Receiver; // zero if no sender, would be visible for everyone
        Height m_Height;

        void get_Hash(HashValue& res, uint32_t nMsg) const
        {
            HashProcessor hp;
            hp.m_p = Env::HashCreateSha256();
            hp << "b.msg";
            hp.Write(this, nMsg);
            hp >> res;
        }
    };

    struct InpCheckpointHdr
    {
        struct Key
        {
            uint8_t m_Type = KeyType::InpCheckpoint;
            uint32_t m_iCheckpoint;
        };

        PubKey m_User;
        // followed by hashes
    };


    struct OutCheckpoint
    {
        struct Key
        {
            uint8_t m_Type = KeyType::OutCheckpoint;
            uint32_t m_iCheckpoint_BE;
        };

        typedef HashValue ValueType;
    };

    struct StateOut
    {
        struct Key
        {
            uint8_t m_Type = KeyType::StateOut;
        };

        Cfg::Out m_Cfg;

        struct Checkpoint {
            uint32_t m_iIdx;
            uint32_t m_iMsg;
            Height m_h0;
        } m_Checkpoint;

        bool IsCheckpointClosed(Height h) const
        {
            return (m_Checkpoint.m_iMsg == m_Cfg.m_CheckpointMaxMsgs) || (h - m_Checkpoint.m_h0 >= m_Cfg.m_CheckpointMaxDH);
        }
    };

    struct StateIn
    {
        struct Key
        {
            uint8_t m_Type = KeyType::StateIn;
        };

        Cfg::In m_Cfg;
        ContractID m_cidRemote;

        struct Dispute
        {
            uint32_t m_iIdx;
            Height m_Height;
            Amount m_Stake;
            HashValue m_hvBestVariant;
            uint32_t m_Variants;
        } m_Dispute;
    };

    struct Variant
    {
        struct Key
        {
            uint8_t m_Type = KeyType::Variant;
            HashValue m_hvVariant;
        };

        Height m_hLastLoose;
        uint32_t m_iDispute;

        struct Ending {
            Height m_Height;
            BeamDifficulty::Raw m_Work;
            HashValue m_hvPrev;
        };

        Ending m_Begin;
        Ending m_End;

        InpCheckpointHdr m_Cp;
        // followed by hashes
    };

    struct UserInfo
    {
        struct Key
        {
            uint8_t m_Type = KeyType::UserInfo;
            PubKey m_Pk;
        };

        Amount m_Balance;
    };

    struct VariantHdr
    {
        struct Key
        {
            uint8_t m_Type = KeyType::VariantHdr;
            HashValue m_hvVariant;
            Height m_Height;
        };

        HashValue m_hvHeader;
    };

#pragma pack (pop)

}
