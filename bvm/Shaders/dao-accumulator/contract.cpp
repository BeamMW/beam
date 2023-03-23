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
            Env::SaveVar_T((uint8_t)Tags::s_State, *this);
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

BEAM_EXPORT void Method_4(const Method::UserLockPrePhase& r)
{
    Height h = Env::get_Height();
    MyState s;
    Env::Halt_if(h >= s.m_hPreEnd);

    User::Key uk;
    _POD_(uk.m_pk) = r.m_pkUser;
    User u;
    if (Env::LoadVar_T(uk, u))
        s.m_Pool.m_Weight -= u.m_PoolUser.m_Weight;
    else
        _POD_(u).SetZero();

    Amount valBeam = r.m_AmountBeamX * State::s_InitialRatio;
    Env::Halt_if(valBeam / State::s_InitialRatio != r.m_AmountBeamX); // overflow test

    Env::Halt_if(r.m_PrePhaseLockPeriods > User::s_PreLockPeriodsMax);
    u.m_PrePhaseLockPeriods = r.m_PrePhaseLockPeriods;

    Strict::Add(u.m_LpTokenPrePhase, valBeam);
    u.m_PoolUser.m_Weight = u.get_WeightPrePhase();

    Strict::Add(s.m_Pool.m_Weight, u.m_PoolUser.m_Weight);

    Env::FundsLock(s.m_aidBeamX, r.m_AmountBeamX);
    Env::FundsLock(0, valBeam);

    Env::SaveVar_T(uk, u);
    s.Save();
}

BEAM_EXPORT void Method_5(const Method::UserUpdate& r)
{
    Height h = Env::get_Height();

    MyState s;
    Env::Halt_if(!s.m_aidLpToken); // post-phase didn't start
    s.m_Pool.Update(h);

    Env::AddSig(r.m_pkUser);

    User::Key uk;
    _POD_(uk.m_pk) = r.m_pkUser;
    User u;
    Amount valLpTokenTotal;

    if (Env::LoadVar_T(uk, u))
    {
        Amount valEarned = s.m_Pool.Remove(u.m_PoolUser);
        u.m_EarnedBeamX += valEarned; // should not overflow

        valLpTokenTotal = u.m_LpTokenPrePhase + u.m_LpTokenPostPhase; // should not overflow
    }
    else
    {
        _POD_(u).SetZero();
        valLpTokenTotal = 0;
    }

    if (r.m_NewLpToken > valLpTokenTotal)
    {
        Amount diff = r.m_NewLpToken - valLpTokenTotal;
        u.m_LpTokenPostPhase += diff; // should not overflow
        Env::FundsLock(s.m_aidLpToken, diff);
    }
    else
    {
        Amount diff = valLpTokenTotal - r.m_NewLpToken;
        if (diff)
        {
            if (u.m_LpTokenPostPhase >= diff)
                u.m_LpTokenPostPhase -= diff;
            else
            {
                diff -= u.m_LpTokenPostPhase;
                u.m_LpTokenPostPhase = 0;

                Env::Halt_if(h < u.get_UnlockHeight(s));

                Strict::Sub(u.m_LpTokenPrePhase, diff);
            }

            Env::FundsUnlock(s.m_aidLpToken, diff);
        }
    }

    if (r.m_WithdrawBeamX)
    {
        Strict::Sub(u.m_EarnedBeamX, r.m_WithdrawBeamX);
        Env::FundsUnlock(s.m_aidBeamX, r.m_WithdrawBeamX);
    }

    u.m_PoolUser.m_Weight = u.get_WeightPrePhase() + u.get_WeightPostPhase();

    if (u.m_PoolUser.m_Weight || u.m_EarnedBeamX)
    {
        u.m_PoolUser.m_Sigma0 = s.m_Pool.m_Sigma;
        Strict::Add(s.m_Pool.m_Weight, u.m_PoolUser.m_Weight);

        Env::SaveVar_T(uk, u);
    }
    else
        Env::DelVar_T(uk);

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
    }
}
