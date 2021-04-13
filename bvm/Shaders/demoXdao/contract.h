#pragma once

namespace DemoXdao
{
    static const ShaderID s_SID = { 0x43,0x5f,0x96,0x42,0xf9,0xf0,0x1d,0xa6,0x24,0xe1,0x77,0xd4,0x23,0x2b,0xea,0x2c,0x71,0xa2,0xb9,0x58,0x0a,0x97,0x6b,0x17,0x33,0xb6,0x6b,0x71,0xe9,0xea,0x5c,0xd7 };

#pragma pack (push, 1)


    struct State
    {
        AssetID m_Aid;

        static const Amount s_TotalEmission = g_Beam2Groth * 10000;
        static const Amount s_ReleasePerLock = g_Beam2Groth * 10;
    };

    struct LockAndGet
    {
        static const uint32_t s_iMethod = 3; // used to map to class methods to TX kernel. In the class implementation we expect to see Method_2

        PubKey m_Pk;
        Amount m_Amount;
    };

#pragma pack (pop)

}
