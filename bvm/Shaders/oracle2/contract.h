#pragma once
#include "../Math.h"

namespace Oracle2
{
#pragma pack (push, 1)

    static const ShaderID s_SID = { 0x13,0x6f,0xe9,0x6e,0xcf,0x64,0x43,0x62,0x3d,0x21,0x6e,0x79,0x2c,0x46,0x32,0x14,0x2c,0x32,0x9b,0x0f,0xa2,0x4c,0xca,0xc0,0x51,0x7b,0x9a,0xdb,0xdd,0xf5,0x2f,0xdd };

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

            // 2 arrays merged:
            //  iPos -> m_iProv, m_Val
            //  iProv -> m_Pk, m_iPos
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
