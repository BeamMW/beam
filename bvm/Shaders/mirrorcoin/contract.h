#pragma once

namespace MirrorCoin
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x26,0xf7,0xcc,0xd7,0xd4,0x3c,0xe7,0x70,0x17,0xbe,0xfc,0x6e,0xb8,0xd8,0x6b,0x4b,0xc3,0x4f,0xf8,0x6f,0xb1,0xad,0x40,0x8c,0xab,0x99,0x1c,0xd4,0x0c,0xa1,0x8a,0x24 };

    struct Create0
    {
        static const uint32_t s_iMethod = 0;

        ContractID m_PipeID;
        AssetID m_Aid; // must be 0 for mirror part
        uint32_t m_MetadataSize; // set to 0 for host part, non-zero for mirror part
        // followed by metadata
    };

    struct SetRemote {
        static const uint32_t s_iMethod = 2;
        ContractID m_Cid;
    };

    struct Message {
        PubKey m_User;
        Amount m_Amount;
    };

    struct Send :public Message {
        static const uint32_t s_iMethod = 3;
    };

    struct Receive
    {
        static const uint32_t s_iMethod = 4;

        uint32_t m_iCheckpoint;
        uint32_t m_iMsg;
    };

    struct Global
    {
        ContractID m_PipeID;
        ContractID m_Remote;
        AssetID m_Aid;
        uint8_t m_IsMirror;
    };

#pragma pack (pop)

}
