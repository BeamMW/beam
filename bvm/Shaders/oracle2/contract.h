#pragma once
#include "../Math.h"

namespace Oracle2
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x93,0x48,0x3a,0x76,0x06,0xc9,0x61,0xf9,0xd4,0xac,0xff,0xa5,0xf6,0xe3,0x97,0x67,0xdc,0x1d,0x4f,0x92,0x24,0xe5,0xb1,0x16,0xa4,0x10,0x65,0x30,0xa2,0x5d,0x77,0x02 };

    typedef MultiPrecision::Float ValueType;

    struct Tags
    {
        static const uint8_t s_Median = 0;
        // don't use taag=1 for multiple data entries, it's used by Upgradable2
        static const uint8_t s_StateFull = 2;
    };

    struct Median {
        ValueType m_Res;
    };

    struct StateFull
    {
        static const uint32_t s_ProvsMax = 32; // practically much less

        struct Entry
        {
            PubKey m_Pk;
            uint32_t m_iPos;
            uint32_t m_iProv;
            ValueType m_Val;
        };

        Entry m_pE[s_ProvsMax];
    };


    namespace Method
    {
        template <uint32_t nProvs>
        struct Create
        {
            static const uint32_t s_iMethod = 0; // Ctor

            uint32_t m_Providers;
            ValueType m_InitialValue;
            PubKey m_pPk[nProvs]; // variable size
        };

        struct Get
        {
            static const uint32_t s_iMethod = 3;
            ValueType m_Value; // retval
        };

        struct Set
        {
            static const uint32_t s_iMethod = 4;

            uint32_t m_iProvider;
            ValueType m_Value;
        };

    }
#pragma pack (pop)

}
