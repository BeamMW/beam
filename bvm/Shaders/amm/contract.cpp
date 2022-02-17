////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"

namespace Amm {


BEAM_EXPORT void Ctor(const void*)
{
    Env::Halt_if(!Env::RefAdd(Mintor::s_CID));
}

BEAM_EXPORT void Dtor(void*)
{
    Env::Halt_if(!Env::RefRelease(Mintor::s_CID));
}


struct MyPool :public Pool
{
    MyPool() {}

    MyPool(const Key& key) { Env::Halt_if(!Env::LoadVar_T(key, *this)); }

    void Save(const Key& key) { Env::SaveVar_T(key, *this); }
};

void FundsMove(AssetID aid, const PubKey& pk, Amount val, bool bLock)
{
    if (Pool::ID::s_Token & aid)
    {
        Mintor::Method::Transfer arg;
        arg.m_Tid = aid & ~Pool::ID::s_Token;
        arg.m_Value = val;

        auto& pkUser = bLock ? arg.m_pkUser : arg.m_pkDst;
        auto& pkMe = bLock ? arg.m_pkDst : arg.m_pkUser;

        _POD_(pkUser) = pk;
        _POD_(pkMe.m_X).SetZero();
        pkMe.m_Y = Mintor::PubKeyFlag::s_Cid;

        Env::CallFar_T(Mintor::s_CID, arg);
    }
    else
    {
        if (bLock)
            Env::FundsLock(aid, val);
        else
            Env::FundsUnlock(aid, val);
    }
}

void PoolCtlMove(const Pool& p, const PubKey& pk, Amount val, uint8_t bMint)
{
    Mintor::Method::Mint arg;
    arg.m_Tid = p.m_tidCtl;
    arg.m_Mint = bMint;
    arg.m_Value = val;
    _POD_(arg.m_pkUser) = pk;
    Env::CallFar_T(Mintor::s_CID, arg);
}

BEAM_EXPORT void Method_2(const Method::AddLiquidity& r)
{
    Pool::Key key;
    key.m_ID = r.m_Pid;

    MyPool p;
    if (!Env::LoadVar_T(key, p))
    {
        Env::Halt_if(key.m_ID.m_Aid1 >= key.m_ID.m_Aid2); // potentially creating a new pool, key must be well-ordered
        _POD_(p).SetZero();

        Mintor::Method::Create arg;
        arg.m_pkUser.m_Y = Mintor::PubKeyFlag::s_Cid;
        Env::get_CallerCid(0, arg.m_pkUser.m_X);
        _POD_(arg.m_Limit).SetObject(0xff);

        Env::CallFar_T(Mintor::s_CID, arg);

        p.m_tidCtl = arg.m_Tid;
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

    FundsMove(key.m_ID.m_Aid1, r.m_pk, r.m_Amounts.m_Tok1, true);
    FundsMove(key.m_ID.m_Aid2, r.m_pk, r.m_Amounts.m_Tok2, true);

    PoolCtlMove(p, r.m_pk, dCtl, 1);
}

BEAM_EXPORT void Method_3(const Method::Withdraw& r)
{
    Pool::Key key;
    key.m_ID = r.m_Pid;
    MyPool p(key);

    auto dVals = p.m_Totals.Remove(r.m_Ctl);

    if (p.m_Totals.m_Ctl)
        p.Save(key);
    else
        Env::DelVar_T(key);

    FundsMove(key.m_ID.m_Aid1, r.m_pk, dVals.m_Tok1, false);
    FundsMove(key.m_ID.m_Aid2, r.m_pk, dVals.m_Tok2, false);

    PoolCtlMove(p, r.m_pk, r.m_Ctl, 0);
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
        p.m_Totals.Swap();

    Amount valPay = p.m_Totals.Trade(r.m_Buy1);

    if (bReverse)
        p.m_Totals.Swap();

    FundsMove(r.m_Pid.m_Aid1, r.m_pk, r.m_Buy1, false);
    FundsMove(r.m_Pid.m_Aid2, r.m_pk, valPay, true);

    p.Save(key);
}

} // namespace Amm
