////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../upgradable3/contract_impl.h"

namespace Amm {


BEAM_EXPORT void Ctor(const Method::Create& r)
{
    r.m_Upgradable.TestNumApprovers();
    r.m_Upgradable.Save();
}

BEAM_EXPORT void Dtor(void*)
{
    Upgradable3::Settings::Key key;
    Env::DelVar_T(key);
}


struct MyPool :public Pool
{
    MyPool() {}

    MyPool(const Key& key) { Env::Halt_if(!Env::LoadVar_T(key, *this)); }

    bool Save(const Key& key) { return Env::SaveVar_T(key, *this); }
};

BEAM_EXPORT void Method_3(const Method::PoolCreate& r)
{
    Env::Halt_if(r.m_Pid.m_Aid1 >= r.m_Pid.m_Aid2); // key must be well-ordered

    Pool::Key key;
    key.m_ID = r.m_Pid;

    MyPool p;
    _POD_(p).SetZero();
    _POD_(p.m_pkCreator) = r.m_pkCreator;

    // generate unique metadata
    static const char s_szMeta[] = "STD:SCH_VER=1;N=Amm Control Token;SN=AmmC;UN=AMMC;NTHUN=GROTH";

#pragma pack (push, 1)
    struct Meta {
        char m_szMeta[sizeof(s_szMeta)]; // including 0-terminator
        Pool::ID m_Pid;
    } md;
#pragma pack (pop)

    Env::Memcpy(md.m_szMeta, s_szMeta, sizeof(s_szMeta));
    md.m_Pid = r.m_Pid;

    p.m_aidCtl = Env::AssetCreate(&md, sizeof(md));
    Env::Halt_if(!p.m_aidCtl);

    Env::Halt_if(p.Save(key)); // fail if already existed
}

BEAM_EXPORT void Method_4(const Method::PoolDestroy& r)
{
    Pool::Key key;
    key.m_ID = r.m_Pid;

    MyPool p(key);

    Env::Halt_if(!Env::AssetDestroy(p.m_aidCtl)); // would fail unless fully burned
    assert(!p.m_Totals.m_Ctl && !p.m_Totals.m_Tok1 && !p.m_Totals.m_Tok2);

    Env::DelVar_T(key);
}

BEAM_EXPORT void Method_5(const Method::AddLiquidity& r)
{
    Pool::Key key;
    key.m_ID = r.m_Pid;

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
        p.m_Totals.AddInitial(r.m_Amounts);
        dCtl = p.m_Totals.m_Ctl;
        Env::Halt_if(!dCtl);
    }

    p.Save(key);

    Env::FundsLock(key.m_ID.m_Aid1, r.m_Amounts.m_Tok1);
    Env::FundsLock(key.m_ID.m_Aid2, r.m_Amounts.m_Tok2);

    Env::Halt_if(!Env::AssetEmit(p.m_aidCtl, dCtl, 1));
    Env::FundsUnlock(p.m_aidCtl, dCtl);
}

BEAM_EXPORT void Method_6(const Method::Withdraw& r)
{
    Pool::Key key;
    key.m_ID = r.m_Pid;
    MyPool p(key);

    auto dVals = p.m_Totals.Remove(r.m_Ctl);

    p.Save(key);

    Env::FundsUnlock(key.m_ID.m_Aid1, dVals.m_Tok1);
    Env::FundsUnlock(key.m_ID.m_Aid2, dVals.m_Tok2);

    Env::FundsLock(p.m_aidCtl, r.m_Ctl);
    Env::Halt_if(!Env::AssetEmit(p.m_aidCtl, r.m_Ctl, 0));
}

BEAM_EXPORT void Method_7(const Method::Trade& r)
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

    Env::FundsUnlock(r.m_Pid.m_Aid1, r.m_Buy1);
    Env::FundsLock(r.m_Pid.m_Aid2, valPay);

    p.Save(key);
}

} // namespace Amm

namespace Upgradable3 {

    const uint32_t g_CurrentVersion = _countof(Amm::s_pSID) - 1;

    uint32_t get_CurrentVersion()
    {
        return g_CurrentVersion;
    }

    void OnUpgraded(uint32_t nPrevVersion)
    {
        if constexpr (g_CurrentVersion)
            Env::Halt_if(nPrevVersion != g_CurrentVersion - 1);
        else
            Env::Halt();
    }
}
