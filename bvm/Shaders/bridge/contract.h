#pragma once

namespace Bridge
{
    // Hash of the compiled shader bytecode
    static const ShaderID s_SID = { 0xb0,0xed,0x6b,0x13,0x81,0xf8,0x82,0x9b,0x08,0xf1,0x45,0x55,0xc3,0x3e,0x5c,0xc0,0xdf,0x19,0x04,0xb8,0x5b,0xb9,0x14,0xb9,0x13,0x82,0x16,0xa4,0xd6,0xd8,0x51,0x98 };

#pragma pack (push, 1) // the following structures will be stored in the node in binary form
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
        static const uint32_t s_iMethod = 4;
        uint32_t m_Amount;
        uint8_t m_Finalized;
        PubKey m_Pk;
    };

    struct Finalized
    {
        static const uint32_t s_iMethod = 5;
    };
#pragma pack (pop)
}