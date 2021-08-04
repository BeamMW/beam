#pragma once
#include "../Eth.h"

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0xb6,0x4e,0x3e,0x3c,0x29,0x4c,0x81,0x4f,0x29,0x89,0xf9,0xf5,0xdb,0x75,0x58,0xd0,0x77,0xdc,0xf0,0x6a,0x82,0x73,0x00,0x66,0x7c,0xd0,0xcb,0x35,0xd0,0x70,0x6d,0xb7 };

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