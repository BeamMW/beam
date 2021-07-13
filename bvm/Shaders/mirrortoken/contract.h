#pragma once

namespace MirrorToken
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x37,0xec,0x8c,0xab,0x27,0x3b,0x6e,0x3e,0x8d,0x9d,0xce,0xe2,0xaa,0xb6,0x76,0xde,0xcb,0x05,0xd2,0x8c,0x28,0x4b,0xe9,0x66,0xdd,0xfa,0x74,0x99,0xc3,0x55,0x33,0x61 };
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
