////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../upgradable3/contract_impl.h"
#include "../dao-vault/contract.h"

namespace DaoAccumulator {

#pragma pack (push, 1)
    struct MyState_NoLoad :public State
    {
        void Load()
        {
            Env::LoadVar_T((uint8_t) Tags::s_State, *this);
        }

        void Save()
        {
            Env::SaveVar_T((uint8_t) Tags::s_State, *this);
        }

    };

    struct MyState :public MyState_NoLoad
    {
        MyState() { Load(); }
    };
#pragma pack (pop)

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    r.m_Upgradable.TestNumApprovers();
    r.m_Upgradable.Save();

    MyState_NoLoad s;
    _POD_(s).SetZero();
    s.m_aidBeamX = r.m_aidBeamX;
    s.m_hPreEnd = r.m_hPrePhaseEnd;
    s.Save();
}

BEAM_EXPORT void Dtor(void*)
{
    // ignore
}

Amount get_LockedAmount(AssetID aid)
{
    struct AmountBig {
        Amount m_Hi;
        Amount m_Lo;
    } val;

    aid = Utils::FromBE(aid);

    if (!Env::LoadVar_T(aid, val, KeyTag::LockedAmount))
        return 0;

    Env::Halt_if(val.m_Hi != 0);
    return Utils::FromBE(val.m_Lo);
}

BEAM_EXPORT void Method_3(const Method::FarmStart& r)
{
    Height h = Env::get_Height();
    MyState s;
    Env::Halt_if(h < s.m_hPreEnd);

    {
        Upgradable3::Settings stg;
        stg.Load();
        stg.TestAdminSigs(r.m_ApproveMask);
    }

    Amount val = get_LockedAmount(0); // assume amount of LP-token is exactly equal to amount of Beams

    // grab lp-token
    s.m_aidLpToken = r.m_aidLpToken;
    Env::Halt_if(!s.m_aidLpToken);
    Env::FundsLock(s.m_aidLpToken, val);

    // release locked beam
    Env::FundsUnlock(0, val);

    // grab deposited beamX for farming, but also release locked beamX
    val /= State::s_InitialRatio;
    if (val > r.m_FarmBeamX)
        Env::FundsUnlock(s.m_aidBeamX, val - r.m_FarmBeamX);
    else
        Env::FundsLock(s.m_aidBeamX, r.m_FarmBeamX - val);
    
    s.m_Pool.m_hLast = h;
    s.m_Pool.m_hRemaining = r.m_hFarmDuration;
    s.m_Pool.m_AmountRemaining = r.m_FarmBeamX;

    s.Save();
}

void OnUserLock(const Method::UserLock& r, Pool& p, Height h, bool bPrePhase, uint8_t nTag)
{
    p.Update(h);

    Env::Halt_if(!r.m_LpToken || (r.m_hEnd <= h));

    uint64_t nPeriods = (r.m_hEnd - h) / User::s_LockPeriodBlocks;
    uint64_t val = nPeriods - 1;
    Env::Halt_if(val >= User::s_LockPeriodsMax);

    User u;
    _POD_(u).SetZero();
    u.m_LpToken = r.m_LpToken;
    u.m_hEnd = r.m_hEnd;

    u.set_Weight(nPeriods, !!bPrePhase);
    p.Add(u.m_PoolUser);

    User::Key uk(nTag);
    _POD_(uk.m_pk) = r.m_pkUser;
    Env::Halt_if(Env::SaveVar_T(uk, u)); // fail if already exists

}

BEAM_EXPORT void Method_4(const Method::UserLock& r)
{
    Height h = Env::get_Height();
    MyState s;

    switch (r.m_PoolType)
    {
    case Method::UserLock::Type::BeamX:
    {
        OnUserLock(r, s.m_Pool, h, false, Tags::s_User);

        Env::Halt_if(!s.m_aidLpToken);
        Env::FundsLock(s.m_aidLpToken, r.m_LpToken);
    }
    break;

    case Method::UserLock::Type::BeamX_PrePhase:
    {
        // deprecated, but nevermind
        Env::Halt_if(h >= s.m_hPreEnd);
        h = s.m_hPreEnd;

        OnUserLock(r, s.m_Pool, h, true, Tags::s_User);

        Env::FundsLock(0, r.m_LpToken);

        Amount valBeamX = r.m_LpToken / State::s_InitialRatio;
        Env::Halt_if(valBeamX * State::s_InitialRatio != r.m_LpToken); // must be exact multiple

        Env::FundsLock(s.m_aidBeamX, valBeamX);
    }
    break;

    default:
        Env::Halt();
    }

    s.Save();
}

void OnUserWithdraw(const Method::UserWithdraw_Base& r, Pool& p, uint8_t nTag, AssetID aidLpToken, AssetID aidBeamX)
{
    Height h = Env::get_Height();
    p.Update(h);

    Env::AddSig(r.m_pkUser);

    User::Key uk(nTag);
    _POD_(uk.m_pk) = r.m_pkUser;
    User u;
    Env::Halt_if(!Env::LoadVar_T(uk, u)); // must exist

    u.m_EarnedBeamX += p.Remove(u.m_PoolUser); // should not overflow

    if (r.m_WithdrawLPToken)
    {
        Env::Halt_if(h < u.m_hEnd);
        Env::FundsUnlock(aidLpToken, u.m_LpToken);

        u.m_LpToken = 0;
        u.m_PoolUser.m_Weight = 0;
    }

    if (r.m_WithdrawBeamX)
    {
        Strict::Sub(u.m_EarnedBeamX, r.m_WithdrawBeamX);
        Env::FundsUnlock(aidBeamX, r.m_WithdrawBeamX);
    }

    if (u.m_LpToken || u.m_EarnedBeamX)
    {
        p.Add(u.m_PoolUser);
        Env::SaveVar_T(uk, u);
    }
    else
        Env::DelVar_T(uk);
}

BEAM_EXPORT void Method_5(const Method::UserWithdraw_FromBeamBeamX& r)
{
    MyState s;
    Env::Halt_if(!s.m_aidLpToken); // post-phase didn't start

    OnUserWithdraw(r, s.m_Pool, Tags::s_User, s.m_aidLpToken, s.m_aidBeamX);

    s.Save();
}

} // namespace DaoAccumulator

namespace Upgradable3 {

    const uint32_t g_CurrentVersion = _countof(DaoAccumulator::s_pSID) - 1;

    uint32_t get_CurrentVersion()
    {
        return g_CurrentVersion;
    }

    void OnUpgraded(uint32_t nPrevVersion)
    {
        if constexpr (g_CurrentVersion > 0)
            Env::Halt_if(nPrevVersion != g_CurrentVersion - 1);
        else
            Env::Halt();

        // add farming time
        DaoAccumulator::MyState s;
        Env::Halt_if(s.m_aidLpToken);

        s.m_hPreEnd = Env::get_Height() + 500;
        s.Save();
    }
}
