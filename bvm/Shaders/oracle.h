#pragma once

namespace Oracle
{
#pragma pack (push, 1)

    typedef uint64_t ValueType;

    struct Create
    {
        static const uint32_t s_iMethod = 0; // Ctor

        uint32_t m_Providers;
        ValueType m_InitialValue;
        PubKey m_pPk[0]; // variable size
    };

    struct Set
    {
        static const uint32_t s_iMethod = 2;

        uint32_t m_iProvider;
        ValueType m_Value;
    };

    struct Get
    {
        static const uint32_t s_iMethod = 3;
        ValueType m_Value; // retval
    };

#pragma pack (pop)

}
