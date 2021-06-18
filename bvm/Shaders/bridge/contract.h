#pragma once
#include "../Eth.h"

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0xf5,0xc9,0x00,0xa3,0x97,0x5d,0x64,0x47,0x25,0xd9,0x7e,0x7a,0xe5,0x34,0xae,0xb4,0x09,0x30,0x94,0x24,0xbd,0x33,0xd8,0x03,0x7b,0xdb,0x82,0xdd,0x6f,0x7b,0xce,0xbf };

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
    };

    struct Finalized
    {
        static const uint32_t s_iMethod = 5;
    };
#pragma pack (pop)
}