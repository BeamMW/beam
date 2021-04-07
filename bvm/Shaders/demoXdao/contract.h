#pragma once

namespace DemoXdao
{
    static const ShaderID s_SID = { 0xdf,0x66,0x42,0x45,0xc0,0x25,0x7b,0x64,0x31,0x6d,0xf1,0x34,0x95,0xf0,0x31,0x83,0xdc,0xe9,0xfb,0xa2,0x05,0x34,0xa3,0x77,0x74,0x37,0x1a,0x2e,0xc2,0xf6,0xdb,0xb2 };

#pragma pack (push, 1)


    struct State
    {
        AssetID m_Aid;

        static const Amount s_TotalEmission = g_Beam2Groth * 10000;
        static const Amount s_ReleasePerLock = g_Beam2Groth * 10;
    };

    struct LockAndGet
    {
        static const uint32_t s_iMethod = 2; // used to map to class methods to TX kernel. In the class implementation we expect to see Method_2

        PubKey m_Pk;
        Amount m_Amount;
    };

#pragma pack (pop)

}
