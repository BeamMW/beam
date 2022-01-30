////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"

namespace Amm {


BEAM_EXPORT void Ctor(const void*)
{
}

BEAM_EXPORT void Dtor(void*)
{
}


struct MyPool :public Pool
{
    struct MyKey :public Key
    {
        MyKey(const Pool::ID& pid)
        {
            Env::Halt_if(pid.m_Aid1 >= pid.m_Aid2);
            m_ID = pid;
        }
    };


    MyPool() {}

    MyPool(const Key& key)
    {
        Env::Halt_if(!Env::LoadVar_T(key, *this));
    }

    bool Save(const Key& key)
    {
        return Env::SaveVar_T(key, *this);
    }
};

BEAM_EXPORT void Method_2(const Method::CreatePool& r)
{
    MyPool p;
    _POD_(p).SetZero();

    // generate unique metadata
    static const char s_szMeta[] = "STD:SCH_VER=1;N=Amm Token;SN=Amm;UN=AMM;NTHUN=GROTHX";

#pragma pack (push, 1)
    struct Meta {
        char m_szMeta[sizeof(s_szMeta)]; // including 0-terminator
        Pool::ID m_Pid;
    } md;
#pragma pack (pop)

    Env::Memcpy(md.m_szMeta, s_szMeta, sizeof(s_szMeta));
    md.m_Pid = r.m_PoolID;

    p.m_aidCtl = Env::AssetCreate(&md, sizeof(md));
    Env::Halt_if(!p.m_aidCtl);

    MyPool::MyKey key(r.m_PoolID);
    Env::Halt_if(p.Save(key)); // would fail if duplicated
}

BEAM_EXPORT void Method_3(const Method::DeletePool& r)
{
    MyPool::MyKey key(r.m_PoolID);
    MyPool p(key);

    Env::Halt_if(!Env::AssetDestroy(p.m_aidCtl)); // would fail unless fully burned

    Env::DelVar_T(key);
}

BEAM_EXPORT void Method_4(const Method::AddLiquidity& r)
{
    MyPool::MyKey key(r.m_PoolID);
    MyPool p(key);

    Amount dCtl;
    if (p.m_Totals.m_Ctl)
    {
        Env::Halt_if(p.m_Totals.TestAdd(r.m_Amounts));

        dCtl = p.m_Totals.m_Ctl;
        p.m_Totals.Add(r.m_Amounts);

        Env::Halt_if(p.m_Totals.m_Ctl <= dCtl);
        dCtl = p.m_Totals.m_Ctl - dCtl;
    }
    else
    {
        // 1st provider
        dCtl = std::min(r.m_Amounts.m_Tok1, r.m_Amounts.m_Tok2);
        Env::Halt_if(!dCtl);

        Cast::Down<Amounts>(p.m_Totals) = r.m_Amounts;
        p.m_Totals.m_Ctl = dCtl;
    }

    p.Save(key);

    Env::FundsLock(key.m_ID.m_Aid1, r.m_Amounts.m_Tok1);
    Env::FundsLock(key.m_ID.m_Aid2, r.m_Amounts.m_Tok2);

    Env::Halt_if(!Env::AssetEmit(p.m_aidCtl, dCtl, 1));
    Env::FundsUnlock(p.m_aidCtl, dCtl);
}

BEAM_EXPORT void Method_5(const Method::Withdraw& r)
{
    MyPool::MyKey key(r.m_PoolID);
    MyPool p(key);

    auto dVals = p.m_Totals.Remove(r.m_Ctl);
    p.Save(key);

    Env::FundsUnlock(key.m_ID.m_Aid1, dVals.m_Tok1);
    Env::FundsUnlock(key.m_ID.m_Aid2, dVals.m_Tok2);

    Env::FundsLock(p.m_aidCtl, r.m_Ctl);
    Env::Halt_if(!Env::AssetEmit(p.m_aidCtl, r.m_Ctl, 0));
}

BEAM_EXPORT void Method_6(const Method::Trade& r)
{
    MyPool::MyKey key(r.m_PoolID);
    MyPool p(key);

    if (r.m_iBuy)
    {
        std::swap(p.m_Totals.m_Tok1, p.m_Totals.m_Tok2);
        std::swap(key.m_ID.m_Aid1, key.m_ID.m_Aid2);
    }

    Amount valPay = p.m_Totals.Trade(r.m_Buy);

    Env::FundsUnlock(key.m_ID.m_Aid1, r.m_Buy);
    Env::FundsLock(key.m_ID.m_Aid2, valPay);

    if (r.m_iBuy)
    {
        std::swap(p.m_Totals.m_Tok1, p.m_Totals.m_Tok2);
        std::swap(key.m_ID.m_Aid1, key.m_ID.m_Aid2);
    }

    p.Save(key);
}

} // namespace Amm
