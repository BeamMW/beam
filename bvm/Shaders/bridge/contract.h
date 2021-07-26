#pragma once
#include "../Eth.h"

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0x72,0x1e,0x91,0x34,0xeb,0xe7,0xfa,0x1b,0xc0,0xdb,0xeb,0xa8,0x3d,0x60,0x5e,0x50,0xd6,0xf4,0x16,0xcd,0x1d,0x6a,0x8a,0x60,0x61,0xf5,0x0b,0x9b,0xa2,0xa4,0x69,0x55 };

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