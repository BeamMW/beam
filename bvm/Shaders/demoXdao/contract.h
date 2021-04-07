#pragma once

namespace DemoXdao
{
    static const ShaderID s_SID = { 0x8c,0x56,0xd6,0xbc,0x2d,0x47,0xe2,0xd0,0xdc,0x22,0x74,0x07,0xb5,0x1f,0x7a,0x7a,0x14,0x1e,0x93,0x06,0x7f,0xe8,0xec,0x2a,0xbd,0x59,0x0a,0xee,0x65,0xd1,0x34,0x59 };

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
