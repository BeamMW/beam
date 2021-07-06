#pragma once

namespace MirrorToken
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x26,0xab,0x17,0x15,0x9f,0xb1,0xea,0xb3,0xb4,0xdc,0xea,0x98,0x6a,0x51,0x7a,0xfd,0xb1,0xb8,0x67,0x72,0x2e,0x12,0x55,0x66,0xa9,0x4f,0xf7,0x71,0xd9,0xa7,0x97,0x5d };
    static const uint8_t kParamsKey = 0;

    using RemoteID = Eth::Address;

    struct Create
    {
        static const uint32_t s_iMethod = 0;

        ContractID m_BridgeID;
        uint32_t m_MetadataSize;
        // followed by metadata
    };

    struct SetRemote {
        static const uint32_t s_iMethod = 2;
        RemoteID m_Remote;
    };

    struct OutMessage {
        Eth::Address m_User;
        Amount m_Amount;
    };

    struct Send :public OutMessage {
        static const uint32_t s_iMethod = 3;
    };

    struct InMessage {
        PubKey m_User;
        Amount m_Amount;
    };

    struct Receive
    {
        static const uint32_t s_iMethod = 4;

        uint32_t m_MsgId;
    };

    struct Mint
    {
        static const uint32_t s_iMethod = 5;

        Amount m_Amount;
    };

    struct Params
    {
        ContractID m_BridgeID;
        RemoteID m_Remote;
        AssetID m_Aid;
    };

#pragma pack (pop)

}
