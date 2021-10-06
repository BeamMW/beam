#pragma once
#include "pool.h"

namespace Liquity
{
    static const ShaderID s_SID = { 0x15,0xac,0x21,0x69,0xa6,0x40,0x1a,0x2a,0x3c,0x27,0xed,0xcf,0xb5,0x49,0x6c,0xae,0xcd,0x3e,0x91,0xe9,0x48,0x7b,0xa6,0xe2,0x87,0xd0,0x1c,0x9d,0xe4,0x35,0xe7,0x66 };

#pragma pack (push, 1)

    struct Tags
    {
        static const uint8_t s_State = 0;
        static const uint8_t s_Epoch = 1;
        static const uint8_t s_Trove = 2;
    };

    typedef ExchangePool::Pair Pair; // s == stable, b == collateral

    struct Balance
    {
        struct Key {
            uint8_t m_Tag = 1;
            PubKey m_Pk;
            AssetID m_Aid;
        };

        typedef Amount ValueType;
    };



    struct Trove
    {
        struct Key
        {
            uint8_t m_Tag = Tags::s_Trove;
            uint32_t m_iTrove;
        };

        PubKey m_pkOwner;
        Pair m_Amounts;
        ExchangePool::User m_Liquidated; // enforced liquidations
    };

    struct Global
    {
        ContractID m_cidOracle;
        AssetID m_Aid;

        struct Troves
        {
            uint32_t m_iLast;
            Pair m_Totals; // effective amounts, after accounting for liquidations

            ExchangePool m_Liquidated;

        } m_Troves;

        ExchangePool m_StabPool;
    };

#pragma pack (pop)

}
