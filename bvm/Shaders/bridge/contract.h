#pragma once
#include "../Eth.h"

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0xc8,0x8a,0x02,0x1f,0xc8,0xb9,0xb2,0x21,0xa5,0x1e,0x63,0x0c,0x27,0x9d,0x5f,0x2a,0x58,0x6b,0x2a,0x1d,0x36,0x2a,0x56,0x5a,0x55,0x51,0x15,0x4e,0x01,0xef,0x89,0xca };

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
    };

    struct Finalized
    {
        static const uint32_t s_iMethod = 5;
    };
#pragma pack (pop)
}