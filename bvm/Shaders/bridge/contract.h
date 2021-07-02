#pragma once
#include "../Eth.h"

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0x2f,0xf7,0x22,0xc0,0x53,0x30,0xf4,0x5e,0xaf,0x8d,0xb1,0x4f,0xcf,0x9c,0x4e,0x52,0xf7,0xf2,0xd9,0xa2,0x85,0x54,0xfa,0x79,0x0a,0x89,0x04,0xf7,0x19,0xb5,0x71,0x5e };

    static const uint8_t kLocalPckgCounterKey = 5;

#pragma pack (push, 1) // the following structures will be stored in the node in binary form
    struct Unlock
    {
        static const uint32_t s_iMethod = 2;
    };

    struct Lock
    {
        static const uint32_t s_iMethod = 3;
        uint32_t m_Amount;
    };

    struct InMsg
    {
        uint32_t m_Amount;
        uint8_t m_Finalized;
        PubKey m_Pk;
    };

    struct ImportMessage
    {
        static const uint32_t s_iMethod = 4;
        InMsg m_Msg;
        Eth::Header m_Header;
        uint32_t m_DatasetCount;
        uint32_t m_ProofSize;
        uint32_t m_ReceiptProofSize;
        uint32_t m_TrieKeySize;
    };

    struct Finalized
    {
        static const uint32_t s_iMethod = 5;
    };

    struct KeyType
    {
        static const uint8_t LocalMsg = 2;
        static const uint8_t RemoteMsg = 3;
    };

    struct MsgKeyBase
    {
        uint8_t m_Type;
        // big-endian, for simpler enumeration by app shader
        uint32_t m_PckgId_BE;
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
        static const uint32_t s_iMethod = 6;

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
        static const uint32_t s_iMethod = 7;

        uint32_t m_PckgId;
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
        static const uint32_t s_iMethod = 8;

        uint32_t m_PckgId;
        uint32_t m_MsgId;
        uint32_t m_MsgSize;
        // out
        Eth::Address m_ContractSender;
        // followed by the message bufer
    };
#pragma pack (pop)
}