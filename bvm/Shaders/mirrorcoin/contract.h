#pragma once

namespace MirrorCoin
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x4c,0x7c,0x68,0x84,0x7c,0xfb,0xc4,0x99,0xcd,0x46,0xab,0xeb,0xf6,0x3a,0x51,0x38,0xb4,0x51,0xd0,0x9e,0xcd,0xf5,0xcf,0x91,0x8e,0xda,0xaf,0xd9,0x08,0x88,0x4e,0xad };

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
