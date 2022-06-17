#pragma once
#include "../Math.h"
#include "../upgradable3/contract.h"

namespace Oracle2
{
#pragma pack (push, 1)

    static const ShaderID s_pSID[] = {
        { 0x85,0x11,0xe8,0xb9,0x6c,0x15,0x68,0x9c,0x3a,0x57,0x02,0xab,0xf1,0x35,0xef,0x23,0x99,0x55,0x4f,0x53,0x48,0xb7,0x9d,0xf3,0x57,0xb2,0x35,0x1d,0xb5,0x22,0xdd,0x2a },
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
