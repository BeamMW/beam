#pragma once

namespace L2Tst1_L2
{
    static const ShaderID s_pSID[] = {
        { 0xc5,0x57,0xdb,0xf6,0xba,0x78,0xfb,0x9f,0x7d,0xbe,0xb4,0x32,0x74,0x9b,0x73,0x55,0x7c,0x12,0x8b,0x96,0x26,0x88,0xbe,0x13,0x33,0x9d,0x01,0x4e,0x3b,0xc3,0x35,0x5c },
    };

#pragma pack (push, 1)

    namespace Method
    {
        struct BridgeOp
        {
            AssetID m_Aid;
            Amount m_Amount;
            uint64_t m_Cookie;
            PubKey m_pk;
        };

        struct BridgeEmit
            :public BridgeOp
        {
            static const uint32_t s_iMethod = 2;
        };

        struct BridgeBurn
            :public BridgeOp
        {
            static const uint32_t s_iMethod = 3;
        };
    }

#pragma pack (pop)

} // namespace L2Tst1_L2
