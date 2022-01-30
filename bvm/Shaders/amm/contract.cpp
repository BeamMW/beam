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
    MyPool() {}

    MyPool(const Key& key) { Env::Halt_if(!Env::LoadVar_T(key, *this)); }

    void Save(const Key& key) { Env::SaveVar_T(key, *this); }
};

BEAM_EXPORT void Method_2(const Method::AddLiquidity& r)
{
    Pool::Key key;
    key.m_ID = r.m_Uid.m_Pid;

    MyPool p;
    if (!Env::LoadVar_T(key, p))
    {
        Env::Halt_if(key.m_ID.m_Aid1 >= key.m_ID.m_Aid2); // potentially creating a new pool, key must be well-ordered
        _POD_(p).SetZero();
    }

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

    User::Key uk;
    _POD_(uk.m_ID) = r.m_Uid;

    User u;
    if (Env::LoadVar_T(uk, u))
        Strict::Add(u.m_Ctl, dCtl);
    else
        u.m_Ctl = dCtl;

    Env::SaveVar_T(uk, u);
}

BEAM_EXPORT void Method_3(const Method::Withdraw& r)
{
    Pool::Key key;
    key.m_ID = r.m_Uid.m_Pid;
    MyPool p(key);

    auto dVals = p.m_Totals.Remove(r.m_Ctl);
    p.Save(key);

    Env::FundsUnlock(key.m_ID.m_Aid1, dVals.m_Tok1);
    Env::FundsUnlock(key.m_ID.m_Aid2, dVals.m_Tok2);

    User::Key uk;
    _POD_(uk.m_ID) = r.m_Uid;

    User u;
    Env::Halt_if(!Env::LoadVar_T(uk, u));
    Strict::Sub(u.m_Ctl, r.m_Ctl);

    if (u.m_Ctl)
        Env::SaveVar_T(uk, u);
    else
        Env::DelVar_T(uk);
}

BEAM_EXPORT void Method_4(const Method::Trade& r)
{
    Pool::Key key;
    bool bReverse = (r.m_Pid.m_Aid1 > r.m_Pid.m_Aid2);
    if (bReverse)
    {
        key.m_ID.m_Aid1 = r.m_Pid.m_Aid2;
        key.m_ID.m_Aid2 = r.m_Pid.m_Aid1;
    }
    else
        key.m_ID = r.m_Pid;
    
    MyPool p(key);

    if (bReverse)
        std::swap(p.m_Totals.m_Tok1, p.m_Totals.m_Tok2);

    Amount valPay = p.m_Totals.Trade(r.m_Buy1);

    if (bReverse)
        std::swap(p.m_Totals.m_Tok1, p.m_Totals.m_Tok2);

    Env::FundsUnlock(r.m_Pid.m_Aid1, r.m_Buy1);
    Env::FundsLock(r.m_Pid.m_Aid2, valPay);

    p.Save(key);
}

} // namespace Amm
