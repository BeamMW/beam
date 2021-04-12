#pragma once

namespace DemoXdao
{
    static const ShaderID s_SID = { 0x0a,0xc3,0xbd,0x38,0x24,0x9d,0x25,0xb7,0xfd,0xa2,0x55,0x04,0x16,0x8d,0x27,0x20,0x4f,0x5a,0xc2,0x59,0x36,0x91,0x28,0x33,0x0c,0xb1,0x8a,0x4b,0xf8,0xee,0x86,0x4e };

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
