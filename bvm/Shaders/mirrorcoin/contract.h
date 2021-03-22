#pragma once

namespace MirrorCoin
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x29,0x83,0xf2,0x0e,0x91,0xe4,0xea,0x4d,0x61,0x18,0xad,0xca,0x82,0xf3,0x04,0x33,0x07,0x35,0xee,0x82,0xff,0xd7,0x86,0xcc,0xd5,0xf3,0x50,0xf4,0x0c,0xab,0xa9,0x44 };

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
