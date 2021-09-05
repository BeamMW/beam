#pragma once
namespace Upgradable2
{
    static const ShaderID s_SID = { 0x32,0x4a,0x87,0xd8,0x1a,0x6d,0x27,0xc7,0xd8,0x3a,0x59,0xe5,0xe9,0x29,0x58,0x9c,0x15,0xcf,0x86,0xce,0xe2,0xf9,0x1d,0xa5,0xbb,0xe0,0x6a,0x55,0xb0,0xfd,0xa2,0xd1 };

#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    struct Active {
        ContractID m_Cid;
    };

    struct Next {
        ContractID m_Cid;
        Height m_hTarget;
    };

    struct State
    {
        static const uint16_t s_Key = 0x7700; // whatever, should not be used by the callee impl

        // part of the state (slice) that is accessed on each invocation.
        Active m_Active;
        Next m_Next;
    };

    struct Settings
    {
        static const uint16_t s_Key = 0x7701; // accessed only on admin actions

        Height m_hMinUpgadeDelay;
        uint32_t m_MinApprovers;

        static const uint32_t s_AdminsMax = 32;
        PubKey m_pAdmin[s_AdminsMax];
    };

    struct Create
    {
        static const uint32_t s_iMethod = 0;

        Active m_Active;
        Settings m_Settings;
    };

    struct Control
    {
        static const uint32_t s_iMethod = 2;

        struct Base
        {
            uint32_t m_ApproveMask; // bitmask of approvers. Each must sign this call
            uint8_t m_Type;
        };

        struct ExplicitUpgrade
            :public Base
        {
            static const uint8_t s_Type = 1;
            uint8_t m_Type = s_Type;

            // induce the scheduled upgrade. Without the need to 'hijack' another method.
            // doesn't need to be signed (i.e. any user can invoke)
        };


        struct Signed
            :public Base
        {
            uint32_t m_ApproveMask; // bitmask of approvers. Each must sign this call
        };


        struct ScheduleUpgrade
            :public Signed
        {
            static const uint8_t s_Type = 2;
            uint8_t m_Type = s_Type;

            Next m_Next;
        };

        struct ReplaceAdmin
            :public Signed
        {
            static const uint8_t s_Type = 3;
            uint8_t m_Type = s_Type;

            uint32_t m_iAdmin;
            PubKey m_Pk;
        };

        struct SetApprovers
            :public Signed
        {
            static const uint8_t s_Type = 4;
            uint8_t m_Type = s_Type;

            uint32_t m_NewVal; // must be within [1, s_AdminsMax]
        };
    };

#pragma pack (pop)
}
