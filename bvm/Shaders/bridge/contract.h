#pragma once
#include "../Eth.h"

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0x3d,0x9a,0x2a,0x39,0xc8,0xec,0x41,0x18,0x8d,0xd2,0xeb,0x83,0xa7,0xdc,0x9e,0x96,0x9e,0xc5,0x1d,0xb2,0x5e,0xb0,0xd5,0xb1,0x85,0x04,0x15,0xea,0xb8,0xe8,0xc1,0x22 };

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