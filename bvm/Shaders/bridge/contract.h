#pragma once
#include "../Eth.h"

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0xe4,0x0c,0x3a,0x1d,0x53,0xf3,0x34,0x15,0x1a,0xd4,0x6a,0xea,0x6c,0xc4,0x5e,0x8c,0x47,0x7c,0xb2,0x2c,0xf1,0xa5,0xa3,0x17,0x66,0x10,0xb1,0x88,0x82,0x31,0x4a,0x73 };

    static const uint8_t kLocalMsgCounterKey = 5;

#pragma pack (push, 1) // the following structures will be stored in the node in binary form
    struct KeyType
    {
        static const uint8_t LocalMsg = 2;
        static const uint8_t RemoteMsg = 3;
    };

    struct MsgKeyBase
    {
        uint8_t m_Type;
        // big-endian, for simpler enumeration by app shader
        uint32_t m_MsgId_BE;
    };

    struct LocalMsgHdr
    {
        struct Key : public MsgKeyBase
        {
            Key() { m_Type = KeyType::LocalMsg; }
        };

        ContractID m_ContractSender;
        Eth::Address m_ContractReceiver;
    };

    struct PushLocal
    {
        static const uint32_t s_iMethod = 2;

        Eth::Address m_ContractReceiver;
        uint32_t m_MsgSize;
        // followed by the message
    };

    struct RemoteMsgHdr
    {
        struct Key : public MsgKeyBase
        {
            Key() { m_Type = KeyType::RemoteMsg; }
        };

        ContractID m_ContractReceiver;
        Eth::Address m_ContractSender;
    };

    struct PushRemote
    {
        static const uint32_t s_iMethod = 3;

        uint32_t m_MsgId;
        RemoteMsgHdr m_MsgHdr;
        Eth::Header m_Header;
        uint32_t m_DatasetCount;
        uint32_t m_ProofSize;
        uint32_t m_ReceiptProofSize;
        uint32_t m_TrieKeySize;
        uint32_t m_MsgSize;
        // followed by message variable data
    };

    struct ReadRemote
    {
        static const uint32_t s_iMethod = 4;

        uint32_t m_MsgId;
        uint32_t m_MsgSize;
        // out
        Eth::Address m_ContractSender;
        // followed by the message bufer
    };
#pragma pack (pop)
}