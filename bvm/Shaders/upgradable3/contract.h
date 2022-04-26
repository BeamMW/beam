#pragma once
namespace Upgradable3
{
#pragma pack (push, 1) // the following structures will be stored in the node in binary form

    struct KeyBase
    {
        uint8_t m_Tag = 0x77;
    };

    struct Settings
    {
        struct Key :public KeyBase {
            uint8_t m_Sub = 0x01;
        };

        Height m_hMinUpgradeDelay;
        uint32_t m_MinApprovers;

        static const uint32_t s_AdminsMax = 32;
        PubKey m_pAdmin[s_AdminsMax];

        void TestNumApprovers() const
        {
            uint32_t val = m_MinApprovers - 1; // would overflow if zero
            Env::Halt_if(val >= s_AdminsMax);
        }

        void Save() const;
    };

    struct NextVersion
    {
        struct Key :public KeyBase {
            uint8_t m_Sub = 0x02;
        };

        Height m_hTarget;
        // followed by the new shader bytecode
    };

    void OnUpgraded(uint32_t nPrevVersion); // called when new version is activated
    uint32_t get_CurrentVersion();

    namespace Method
    {
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

            struct OnUpgraded
                :public Base
            {
                static const uint8_t s_Type = 2;
                OnUpgraded() :Base(s_Type) {}

                uint32_t m_PrevVersion;

                // Invoked internally by the prev implementation
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
                static const uint8_t s_Type = 3;
                ScheduleUpgrade() :Signed(s_Type) {}

                uint32_t m_SizeShader;
                NextVersion m_Next;
                // followed by the shader bytecode
            };

            struct ReplaceAdmin
                :public Signed
            {
                static const uint8_t s_Type = 4;
                ReplaceAdmin() :Signed(s_Type) {}

                uint32_t m_iAdmin;
                PubKey m_Pk;
            };

            struct SetApprovers
                :public Signed
            {
                static const uint8_t s_Type = 5;
                SetApprovers() :Signed(s_Type) {}

                uint32_t m_NewVal; // must be within [1, s_AdminsMax]
            };
        };

    } // namespace Method

#pragma pack (pop)
}
