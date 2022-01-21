#pragma once
#include "../Math.h"

namespace Amm
{
#pragma pack (push, 1)

    struct Tags
    {
        // don't use tag=1 for multiple data entries, it's used by Upgradable2
        static const uint8_t s_Pool = 2;
        //static const uint8_t s_User = 3;
    };

    struct Amounts
    {
        Amount m_Tok1;
        Amount m_Tok2;
    };

    struct Totals :public Amounts
    {
        Amount m_Ctl;
    };

    struct Pool
    {
        struct ID
        {
            AssetID m_Aid1;
            AssetID m_Aid2;
            // must be well-ordered
        };

        struct Key
        {
            uint8_t m_Tag = Tags::s_Pool;
            ID m_ID;
        };

        Totals m_Totals;
        AssetID m_aidCtl;
    };

    //struct User
    //{
    //    struct Key {
    //        uint8_t m_Tag = Tags::s_User;
    //        Pool::ID m_PoolID;
    //        PubKey m_pk;
    //    };
    //
    //    Totals m_Totals;
    //};

    namespace Method
    {
        struct PoolInvoke
        {
            Pool::ID m_PoolID;
        };

        struct CreatePool :public PoolInvoke
        {
            static const uint32_t s_iMethod = 2;
        };

        struct DeletePool :public PoolInvoke
        {
            static const uint32_t s_iMethod = 3;
        };

        struct AddLiquidity :public PoolInvoke
        {
            static const uint32_t s_iMethod = 4;
            Amounts m_Amounts;
        };

        struct Withdraw :public PoolInvoke
        {
            static const uint32_t s_iMethod = 5;
            Amount m_Ctl;
        };

        struct Trade :public PoolInvoke
        {
            static const uint32_t s_iMethod = 6;
            Amount m_Buy;
            uint8_t m_iBuy;
        };

    }
#pragma pack (pop)

} // namespace Amm
