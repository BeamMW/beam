////////////////////////
#include "../common.h"
#include "../Math.h" // Strict
#include "contract.h"
#include "../upgradable3/contract_impl.h"

namespace L2Tst1 {

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
    _POD_(s.m_Settings) = r.m_Settings;
    s.Save();
}

BEAM_EXPORT void Dtor(void*)
{
    // ignore
}

BEAM_EXPORT void Method_3(const Method::UserStake& r)
{
    Env::Halt_if(!r.m_Amount);

    MyState s;

    Height h = Env::get_Height();
    Env::Halt_if(h >= s.m_Settings.m_hPreEnd);

    User::Key uk;
    _POD_(uk.m_pk) = r.m_pkUser;

    User u;
    if (Env::LoadVar_T(uk, u))
        Strict::Add(u.m_Stake, r.m_Amount);
    else
        u.m_Stake = r.m_Amount;

    Env::SaveVar_T(uk, u);

    Env::FundsLock(s.m_Settings.m_aidStaking, r.m_Amount);
}

} // namespace L2Tst1

namespace Upgradable3 {

    const uint32_t g_CurrentVersion = _countof(L2Tst1::s_pSID) - 1;

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
