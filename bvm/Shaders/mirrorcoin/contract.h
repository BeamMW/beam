#pragma once

namespace MirrorCoin
{
#pragma pack (push, 1)

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
