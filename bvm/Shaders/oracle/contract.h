#pragma once

namespace Oracle
{
#pragma pack (push, 1)

    typedef uint64_t ValueType;

    template <uint32_t nProvs>
    struct Create
    {
        static const uint32_t s_iMethod = 0; // Ctor

        uint32_t m_Providers;
        ValueType m_InitialValue;
        PubKey m_pPk[nProvs]; // variable size

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Providers);
            ConvertOrd<bToShader>(m_InitialValue);
        }
    };

    struct Set
    {
        static const uint32_t s_iMethod = 2;

        uint32_t m_iProvider;
        ValueType m_Value;

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_iProvider);
            ConvertOrd<bToShader>(m_Value);
        }
    };

    struct Get
    {
        static const uint32_t s_iMethod = 3;
        ValueType m_Value; // retval

        template <bool bToShader>
        void Convert()
        {
            ConvertOrd<bToShader>(m_Value);
        }
    };

#pragma pack (pop)

}
