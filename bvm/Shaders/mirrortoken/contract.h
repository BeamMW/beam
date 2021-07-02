#pragma once

namespace MirrorToken
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x8d,0x91,0xef,0xb0,0x4c,0x90,0x94,0xc2,0xeb,0x86,0xe1,0x44,0x59,0xd0,0x0b,0xf5,0xa8,0x10,0xab,0x0e,0x75,0x17,0xda,0xa9,0x27,0xa8,0xfa,0x83,0x61,0xe3,0x80,0xe0 };
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

        uint32_t m_PckgId;
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
