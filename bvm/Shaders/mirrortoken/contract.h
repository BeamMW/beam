#pragma once

namespace MirrorToken
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0xd7,0x70,0x58,0xd2,0xc4,0x13,0x39,0x5a,0x13,0x2a,0x54,0x0c,0x0b,0x02,0xf9,0xae,0x6d,0x25,0xf7,0x68,0x29,0xf9,0x30,0x26,0xe0,0xb0,0xa7,0x43,0xe6,0x38,0xf5,0x9c };
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

    struct Params
    {
        ContractID m_BridgeID;
        RemoteID m_Remote;
        AssetID m_Aid;
    };

#pragma pack (pop)

}
