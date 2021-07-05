#pragma once

namespace MirrorToken
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x8a,0x1f,0x29,0x1e,0x2d,0xaf,0x59,0x66,0x26,0xcc,0xf7,0x7e,0x66,0x47,0x4b,0x57,0xd0,0x25,0x65,0x00,0xf6,0x2e,0x3c,0xff,0x6e,0x40,0x64,0xfc,0x60,0x4d,0x6b,0x54 };
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
