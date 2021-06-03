#pragma once
//#include "../Eth.h"

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0x9c,0x40,0x75,0xe2,0xc8,0x08,0xf7,0x26,0x4d,0x33,0xdf,0x2d,0xbf,0x64,0xb4,0xa6,0x39,0x80,0x6c,0x54,0x2b,0x88,0x35,0x8c,0x52,0x23,0x23,0x27,0xd7,0x8f,0x89,0xfc };

#pragma pack (push, 1) // the following structures will be stored in the node in binary form
    struct Header
    {
        Opaque<32> m_ParentHash;
        Opaque<32> m_UncleHash;
        Opaque<20> m_Coinbase;
        Opaque<32> m_Root;
        Opaque<32> m_TxHash;
        Opaque<32> m_ReceiptHash;
        Opaque<256> m_Bloom;
        Opaque<32> m_Extra;
        uint32_t m_nExtra; // can be less than maximum size

        uint64_t m_Difficulty;
        uint64_t m_Number; // height
        uint64_t m_GasLimit;
        uint64_t m_GasUsed;
        uint64_t m_Time;
        uint64_t m_Nonce;
    };

    struct Unlock
    {
        static const uint32_t s_iMethod = 2;
        /*uint32_t m_Amount;
        PubKey m_Pk;*/
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
        Header m_Header;
    };

    struct Finalized
    {
        static const uint32_t s_iMethod = 5;
    };
#pragma pack (pop)
}