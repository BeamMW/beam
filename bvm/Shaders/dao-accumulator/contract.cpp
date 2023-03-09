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

    // grab lp-token
    Env::Halt_if(!s.m_aidLpToken);
    s.m_aidLpToken = r.m_aidLpToken;
    Env::FundsLock(s.m_aidLpToken, s.m_Pool.m_Weight);

    // release locked beam+beamX
    Env::FundsUnlock(s.m_aidBeamX, s.m_Pool.m_Weight / State::s_InitialRatio);
    Env::FundsUnlock(0, s.m_Pool.m_Weight);

    // grab deposited beamX for farming
    Env::FundsLock(s.m_aidBeamX, r.m_FarmBeamX);
    
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
    if (!Env::LoadVar_T(uk, u))
        _POD_(u).SetZero();

    Amount valBeam = r.m_AmountBeamX * State::s_InitialRatio;
    Env::Halt_if(valBeam / State::s_InitialRatio != r.m_AmountBeamX); // overflow test

    Strict::Add(s.m_Pool.m_Weight, valBeam);
    u.m_PoolUser.m_Weight += valBeam; // should not overflow if the prev didn't

    Env::FundsLock(s.m_aidBeamX, r.m_AmountBeamX);
    Env::FundsLock(0, valBeam);

    Env::SaveVar_T(uk, u);
    s.Save();
}

BEAM_EXPORT void Method_5(const Method::UserUpdate& r)
{
    MyState s;
    Env::Halt_if(!s.m_aidLpToken); // post-phase didn't start
    s.m_Pool.Update(Env::get_Height());

    Env::AddSig(r.m_pkUser);

    User::Key uk;
    _POD_(uk.m_pk) = r.m_pkUser;
    User u;
    Amount valLpTokenTotal;

    if (Env::LoadVar_T(uk, u))
    {
        Amount valEarned = s.m_Pool.Remove(u.m_PoolUser);
        u.m_EarnedBeamX += valEarned; // should not overflow

        u.m_PoolUser.m_Weight -= u.get_WeightPostPhase(); // should not overflow
        valLpTokenTotal = u.m_PoolUser.m_Weight + u.m_LpTokenPostPhase; // should not overflow
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

                Strict::Sub(u.m_PoolUser.m_Weight, diff);
            }

            Env::FundsUnlock(s.m_aidLpToken, diff);
        }
    }

    if (r.m_WithdrawBeamX)
    {
        Strict::Sub(u.m_EarnedBeamX, r.m_WithdrawBeamX);
        Env::FundsUnlock(s.m_aidBeamX, r.m_WithdrawBeamX);
    }

    // recalculate the weight
    u.m_PoolUser.m_Weight += u.get_WeightPostPhase(); // should not overflow
    Strict::Add(s.m_Pool.m_Weight, u.m_PoolUser.m_Weight);

    if (u.m_PoolUser.m_Weight || u.m_EarnedBeamX)
        Env::SaveVar_T(uk, u);
    else
    {
        assert(!u.m_AmountLpToken);
        Env::DelVar_T(uk);
    }

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
        if constexpr (g_CurrentVersion)
            Env::Halt_if(nPrevVersion != g_CurrentVersion - 1);
        else
            Env::Halt();
    }
}
