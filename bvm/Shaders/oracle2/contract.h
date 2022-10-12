#pragma once
#include "../Math.h"
#include "../upgradable3/contract.h"

namespace Oracle2
{
#pragma pack (push, 1)

    static const ShaderID s_pSID[] = {
        { 0x97,0x6e,0x3e,0xff,0x9d,0x3c,0xa3,0xd3,0x3a,0xfe,0x07,0x39,0x38,0xec,0xec,0x00,0x38,0xe9,0xc8,0xcb,0x5c,0xfe,0x9e,0x96,0xa5,0x86,0x51,0xf6,0xed,0x1b,0x7d,0x50 },
    };

    typedef MultiPrecision::Float ValueType;

    struct Tags
    {
        static const uint8_t s_Median = 0;
        // don't use taag=1 for multiple data entries, it's used by Upgradable2
        static const uint8_t s_StateFull = 2;
    };

    struct Settings
    {
        uint32_t m_MinProviders;
        Height m_hValidity;
    };

    struct Median
    {
        ValueType m_Res;
        Height m_hEnd;
    };

    struct State0
    {
        Settings m_Settings;

        static const uint32_t s_ProvsMax = 32; // practically much less

        struct Entry
        {
            PubKey m_Pk;
            ValueType m_Val;
            Height m_hUpdated;
        };
    };

    struct StateMax
        :public State0
    {
        Entry m_pE[s_ProvsMax];
    };


    namespace Method
    {
        struct Create
        {
            static const uint32_t s_iMethod = 0; // Ctor
            Upgradable3::Settings m_Upgradable;
            Settings m_Settings;
        };

        struct Get
        {
            static const uint32_t s_iMethod = 3;
            ValueType m_Value; // retval
            uint8_t m_IsValid;
        };

        struct FeedData
        {
            static const uint32_t s_iMethod = 4;

            uint32_t m_iProvider;
            ValueType m_Value;
        };

        struct Signed {
            uint32_t m_ApproveMask;
        };

        struct SetSettings
            :public Signed
        {
            static const uint32_t s_iMethod = 5;
            Settings m_Settings;
        };

        struct ProviderAdd
            :public Signed
        {
            static const uint32_t s_iMethod = 6;
            PubKey m_pk;
        };

        struct ProviderDel
            :public Signed
        {
            static const uint32_t s_iMethod = 7;
            uint32_t m_iProvider;
        };

    }
#pragma pack (pop)

}
