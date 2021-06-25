#pragma once
#include "../Eth.h"

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0xcb,0x36,0x4e,0x2a,0x36,0xfd,0x17,0x47,0x80,0x6b,0xc6,0x47,0x2e,0xb2,0x96,0x12,0x69,0x81,0x50,0x5a,0x36,0x71,0xdf,0xfb,0x20,0xcd,0x75,0xe3,0x90,0x69,0xb0,0xf2 };

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
#pragma pack (pop)
}