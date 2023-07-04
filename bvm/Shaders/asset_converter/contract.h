#pragma once

namespace AssetConverter
{
    static const ShaderID s_SID = { 0x28,0xae,0x4f,0x84,0x93,0x50,0x99,0xbf,0x8c,0x24,0x93,0x40,0x45,0x90,0x0d,0x80,0x86,0x8f,0x3d,0x4e,0xd5,0x8d,0x7c,0xd9,0x9b,0xe4,0xdf,0xc3,0xb2,0x27,0x2b,0x82 };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_Pool = 1;
    };

    struct AmountBig
    {
        Amount m_Lo;
        Amount m_Hi;

        void operator += (Amount x)
        {
            m_Lo += x;
            if (m_Lo < x)
                m_Hi++; // assume it can't overflow
        }

        void operator -= (Amount x)
        {
            if (m_Lo < x)
            {
                Env::Halt_if(!m_Hi);
                m_Hi--;
            }
            m_Lo -= x;
        }
    };

    struct Pool
    {
        struct ID
        {
            AssetID m_AidFrom;
            AssetID m_AidTo;
        };

        struct Key
        {
            uint8_t m_Tag = Tags::s_Pool;
            ID m_ID;
        };

        AmountBig m_FromLocked; // how much was converted already, it remains locked forever
        AmountBig m_ToRemaining;
    };

    namespace Method
    {
        struct PoolInvoke {
            Pool::ID m_Pid;
        };

        struct PoolCreate :public PoolInvoke
        {
            static const uint32_t s_iMethod = 2;
        };

        struct PoolDeposit :public PoolInvoke
        {
            static const uint32_t s_iMethod = 3;
            Amount m_Amount;
        };

        struct PoolConvert :public PoolInvoke
        {
            static const uint32_t s_iMethod = 4;
            Amount m_Amount;
        };

    }
#pragma pack (pop)

} // namespace AssetConverter
