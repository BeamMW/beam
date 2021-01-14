#pragma once
#include "../BeamDifficulty.h"

namespace Pipe
{
#pragma pack (push, 1) // the following structures will be stored in the node in binary form

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
        };

        uint8_t m_Flags;

        // followed by message variable data
    };

    struct FinalyzeRemote
    {
        static const uint32_t s_iMethod = 5;
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
        // followed by the message
    };

    struct KeyType
    {
        static const uint8_t Global = 0;
        static const uint8_t OutMsg = 1;
        static const uint8_t OutCheckpoint = 2;
        static const uint8_t UserInfo = 3;
        static const uint8_t UserMsgs = 4;
        static const uint8_t UserHdr = 5;
        static const uint8_t InpCheckpoint = 6;
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
            uint8_t m_Type = KeyType::Global;
            uint8_t m_SubType = 0;
        };

        Cfg::Out m_Cfg;

        struct Checkpoint {
            uint32_t m_iIdx;
            uint32_t m_iMsg;
            Height m_h0;
        } m_Checkpoint;
    };

    struct StateIn
    {
        struct Key
        {
            uint8_t m_Type = KeyType::Global;
            uint8_t m_SubType = 1;
        };

        Cfg::In m_Cfg;
        ContractID m_cidRemote;

        struct Dispute
        {
            uint32_t m_iIdx;
            Height m_Height;
            Amount m_Stake;
            PubKey m_Winner;
        } m_Dispute;
    };

    struct UserInfo
    {
        struct Key
        {
            uint8_t m_Type = KeyType::UserInfo;
            PubKey m_Pk;
        };

        Amount m_Balance;

        struct Dispute
        {
            uint32_t m_iIdx;
            Height m_hMin;
            Height m_hMax;
            BeamDifficulty::Raw m_Work;
        } m_Dispute;

    };

    struct UserHdr
    {
        struct Key
        {
            uint8_t m_Type = KeyType::UserHdr;
            PubKey m_Pk;
            Height m_Height;
        };

        HashValue m_hv;
        BeamDifficulty::Raw m_ChainWork;

    };

#pragma pack (pop)

}
