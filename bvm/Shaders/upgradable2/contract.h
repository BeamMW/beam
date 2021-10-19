#pragma once
namespace Upgradable2
{
    static const ShaderID s_SID = { 0x95,0x05,0x44,0xd5,0x4a,0x88,0xe6,0xdf,0xb6,0x24,0x36,0xa2,0x74,0xaa,0x8e,0x55,0x74,0x5c,0x57,0x95,0x2d,0x09,0xf4,0x62,0xe2,0x2c,0x13,0xf2,0x55,0x90,0x92,0x14 };

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
            uint8_t m_Type;
            Base(uint8_t nType) :m_Type(nType) {}
        };

        struct ExplicitUpgrade
            :public Base
        {
            static const uint8_t s_Type = 1;
            ExplicitUpgrade() :Base(s_Type) {}

            // induce the scheduled upgrade. Without the need to 'hijack' another method.
            // doesn't need to be signed (i.e. any user can invoke)
        };


        struct Signed
            :public Base
        {
            Signed(uint8_t nType) :Base(nType) {}
            uint32_t m_ApproveMask; // bitmask of approvers. Each must sign this call
        };


        struct ScheduleUpgrade
            :public Signed
        {
            static const uint8_t s_Type = 2;
            ScheduleUpgrade() :Signed(s_Type) {}

            Next m_Next;
        };

        struct ReplaceAdmin
            :public Signed
        {
            static const uint8_t s_Type = 3;
            ReplaceAdmin() :Signed(s_Type) {}

            uint32_t m_iAdmin;
            PubKey m_Pk;
        };

        struct SetApprovers
            :public Signed
        {
            static const uint8_t s_Type = 4;
            SetApprovers() :Signed(s_Type) {}

            uint32_t m_NewVal; // must be within [1, s_AdminsMax]
        };
    };

#pragma pack (pop)
}
