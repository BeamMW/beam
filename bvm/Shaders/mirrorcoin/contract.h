#pragma once

namespace MirrorCoin
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x0e,0x39,0x97,0xf1,0xcd,0xa4,0xfc,0x13,0xee,0x68,0x05,0xc2,0x8d,0x2b,0xe6,0x8e,0x97,0x0c,0xe0,0xac,0xc4,0x92,0x1b,0x02,0x76,0x02,0xbb,0xb4,0x87,0x96,0xc6,0xc5 };

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
