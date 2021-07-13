#pragma once
#include "../Eth.h"

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0x8f,0x29,0xae,0x43,0x68,0xb6,0x5e,0x17,0xab,0xe9,0xb5,0xcb,0x5d,0x60,0x48,0x77,0x15,0xd7,0x85,0x55,0x19,0xcf,0x5f,0x5c,0x25,0x20,0xcf,0xcc,0x5f,0xa5,0x82,0xac };

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