#pragma once

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

        // followed by message variable data
    };

    struct KeyType
    {
        static const uint8_t Global = 0;
        static const uint8_t OutMsg = 1;
        static const uint8_t OutCheckpoint = 2;
        static const uint8_t UserInfo = 3;
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
        uint32_t m_iCheckpoint;

        struct Checkpoint {
            Height m_h0;
            uint32_t m_iMsg;
            HashValue m_hv;
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
    };

#pragma pack (pop)

}
