////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../upgradable3/contract_impl.h"
#include "../dao-vault/contract.h"

namespace Amm {

#pragma pack (push, 1)
    struct MySettings :public Settings
    {
        MySettings() {
            Env::LoadVar_T((uint8_t)Tags::s_Settings, *this);
        }
    };
#pragma pack (pop)

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    r.m_Upgradable.TestNumApprovers();
    r.m_Upgradable.Save();

    Env::Halt_if(!Env::RefAdd(r.m_Settings.m_cidDaoVault));
    Env::SaveVar_T((uint8_t) Tags::s_Settings, r.m_Settings);
}

BEAM_EXPORT void Dtor(void*)
{
    Upgradable3::Settings::Key key;
    Env::DelVar_T(key);

    MySettings s;
    Env::Halt_if(!Env::RefRelease(s.m_cidDaoVault));
    Env::DelVar_T((uint8_t) Tags::s_Settings);
}


struct MyPool :public Pool
{
    MyPool() {}

    MyPool(const Key& key) { Env::Halt_if(!Env::LoadVar_T(key, *this)); }

    bool Save(const Key& key) { return Env::SaveVar_T(key, *this); }
};


BEAM_EXPORT void Method_3(const Method::PoolCreate& r)
{
    Env::Halt_if((r.m_Pid.m_Aid1 >= r.m_Pid.m_Aid2) || (r.m_Pid.m_Fees.m_Kind >= FeeSettings::s_Kinds)); // key must be well-ordered

    Pool::Key key;
    key.m_ID = r.m_Pid;

    MyPool p;
    _POD_(p).SetZero();
    _POD_(p.m_pkCreator) = r.m_pkCreator;

    // generate unique metadata
    static const char s_szMeta1[] = "STD:SCH_VER=1;N=Amm Liquidity Token ";
    static const char s_szMeta2[] = ";SN=AmmL;UN=AMML;NTHUN=GROTH";

    typedef Utils::String::Decimal D;
    char szMeta[_countof(s_szMeta1) + _countof(s_szMeta1) + D::DigitsMax<AssetID>::N * 2 + D::Digits<FeeSettings::s_Kinds - 1>::N + 2];

    Env::Memcpy(szMeta, s_szMeta1, sizeof(s_szMeta1) - sizeof(char));
    uint32_t nMeta = _countof(s_szMeta1) - 1;
    nMeta += D::Print(szMeta + nMeta, key.m_ID.m_Aid1);
    szMeta[nMeta++] = '-';
    nMeta += D::Print(szMeta + nMeta, key.m_ID.m_Aid2);
    szMeta[nMeta++] = '-';
    nMeta += D::Print(szMeta + nMeta, key.m_ID.m_Fees.m_Kind);
    Env::Memcpy(szMeta + nMeta, s_szMeta2, sizeof(s_szMeta2) - sizeof(char));
    nMeta += _countof(s_szMeta2) - 1;

    p.m_aidCtl = Env::AssetCreate(szMeta, nMeta);
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

    Env::AddSig(p.m_pkCreator);
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
        Env::Halt_if(!(r.m_Amounts.m_Tok1 && r.m_Amounts.m_Tok2));
        p.m_Totals.AddInitial(r.m_Amounts);
        dCtl = p.m_Totals.m_Ctl;
        assert(dCtl);
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
    key.m_ID = r.m_Pid;
    bool bReverse = (key.m_ID.m_Aid1 > key.m_ID.m_Aid2);
    if (bReverse)
        std::swap(key.m_ID.m_Aid1, key.m_ID.m_Aid2);
    
    MyPool p(key);

    if (bReverse)
        p.m_Totals.Swap();

    TradeRes res;
    p.m_Totals.Trade(res, r.m_Buy1, key.m_ID.m_Fees);

    if (bReverse)
        p.m_Totals.Swap();

    Env::FundsUnlock(r.m_Pid.m_Aid1, r.m_Buy1);
    Env::FundsLock(r.m_Pid.m_Aid2, res.m_PayPool);

    p.Save(key);

    if (res.m_DaoFee)
    {
        DaoVault::Method::Deposit arg;
        arg.m_Aid = r.m_Pid.m_Aid2;
        arg.m_Amount = res.m_DaoFee;

        MySettings s;
        Env::CallFar_T(s.m_cidDaoVault, arg);
    }
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
