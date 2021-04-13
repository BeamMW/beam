#pragma once

namespace Upgradable
{
    static const ShaderID s_SID = { 0x32,0x4a,0x87,0xd8,0x1a,0x6d,0x27,0xc7,0xd8,0x3a,0x59,0xe5,0xe9,0x29,0x58,0x9c,0x15,0xcf,0x86,0xce,0xe2,0xf9,0x1d,0xa5,0xbb,0xe0,0x6a,0x55,0xb0,0xfd,0xa2,0xd1 };

#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    struct Current {
        ContractID m_Cid;
        Height m_hMinUpgadeDelay;
        PubKey m_Pk;
    };

    struct Next {
        ContractID m_cidNext;
        Height m_hNextActivate;
    };

    struct State
        :public Current
        ,public Next
    {
        static const uint8_t s_Key = 0x77; // whatever, should not be used by the callee impl
    };

    struct Create
        :public Current
    {
        static const uint32_t s_iMethod = 0;
    };

    struct ScheduleUpgrade
        :public Next
    {
        static const uint32_t s_iMethod = 2;
    };

#pragma pack (pop)

}
