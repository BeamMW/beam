////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"

namespace Amm {

using MultiPrecision::Float;


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

    Float get_Vol() const
    {
        return Float(m_Totals.m_Tok1) * Float(m_Totals.m_Tok2);
    }

    static Amount ToAmount(const Float& f)
    {
        static_assert(sizeof(Amount) == sizeof(f.m_Num));
        Env::Halt_if(f.m_Order > 0);
        return f.Get();

    }

    static Amount ShrinkTok(const Float& kShrink, Amount& val)
    {
        Amount val0 = val;
        val = Float(val0) * kShrink;

        assert(val <= val0); // no need for runtime check
        return val0 - val;
    }
};

BEAM_EXPORT void Method_2(const Method::CreatePool& r)
{
    MyPool p;
    _POD_(p).SetZero();

    // TODO: generate unique metadata, contract can't create different assets with the same metadata
    static const char szMeta[] = "STD:SCH_VER=1;N=Amm Token;SN=Amm;UN=AMM;NTHUN=GROTHX";
    p.m_aidCtl = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
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
    Amount ctl0 = p.m_Totals.m_Ctl;
    if (ctl0)
    {
        Float vol0 = p.get_Vol();

        Strict::Add(p.m_Totals.m_Tok1, r.m_Amounts.m_Tok1);
        Strict::Add(p.m_Totals.m_Tok2, r.m_Amounts.m_Tok2);

        Float kGrow = p.get_Vol() / vol0;

        p.m_Totals.m_Ctl = MyPool::ToAmount(Float(ctl0) * kGrow);

        Env::Halt_if(p.m_Totals.m_Ctl <= ctl0);
        dCtl = p.m_Totals.m_Ctl - ctl0;
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

    Amount ctl0 = p.m_Totals.m_Ctl;
    Strict::Sub(p.m_Totals.m_Ctl, r.m_Ctl);

    Amounts dVals;

    if (p.m_Totals.m_Ctl)
    {
        Float kShrink = Float(p.m_Totals.m_Ctl) / Float(ctl0);

        dVals.m_Tok1 = MyPool::ShrinkTok(kShrink, p.m_Totals.m_Tok1);
        dVals.m_Tok2 = MyPool::ShrinkTok(kShrink, p.m_Totals.m_Tok2);
    }
    else
    {
        // last provider
        dVals = p.m_Totals;
        _POD_(p.m_Totals).SetZero();
    }

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

    Float vol = p.get_Vol();

    Env::Halt_if(p.m_Totals.m_Tok1 <= r.m_Buy);
    p.m_Totals.m_Tok1 -= r.m_Buy;

    Amount valPay = MyPool::ToAmount(vol / Float(p.m_Totals.m_Tok1));
    Strict::Sub(valPay, p.m_Totals.m_Tok2);

    // add comission 0.3%
    Amount fee = valPay / 1000 * 3;
    Strict::Add(valPay, fee);
    Strict::Add(p.m_Totals.m_Tok2, valPay);

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
