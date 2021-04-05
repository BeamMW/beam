#pragma once

namespace Upgradable
{
    static const ShaderID s_SID = { 0x58,0xde,0x17,0xbd,0xe5,0x78,0x8c,0x5f,0x98,0xab,0x08,0x87,0x68,0x5b,0x88,0xb2,0xe5,0xa4,0x07,0x0f,0x77,0x6b,0xf8,0x84,0x04,0x1b,0x24,0xf5,0x5b,0xfc,0x07,0x2a };

#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    struct Current {
        ContractID m_Cid;
        Height m_hMinUpgadeDelay;
    };

    struct Next {
        ContractID m_cidNext;
        Height m_hNextActivate;
    };

    struct State
        :public Current
        ,public Next
    {
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
