////////////////////////
#include "Shaders/common.h"
#include "test_contract.h"

template <bool bAdd>
bool RefManageSafe(const ContractID& cid)
{
    if (_POD_(cid).IsZero())
        return false;

    if constexpr (bAdd)
        Env::Halt_if(!Env::RefAdd(cid));
    else
        Env::Halt_if(!Env::RefRelease(cid));
    return true;
}

void ResetNext(Upgradable::Next& n)
{
    _POD_(n.m_cidNext).SetZero();
    n.m_hNextActivate = static_cast<Height>(-1);
}

void InvokeNext(uint32_t iMethod, void* pArg)
{
    Upgradable::State s;
    const uint8_t key = Upgradable::State::s_Key;
    Env::LoadVar_T(key, s);

    if (s.m_hNextActivate <= Env::get_Height())
    {
        RefManageSafe<false>(s.m_Cid);
        _POD_(s.m_Cid) = s.m_cidNext;
        RefManageSafe<true>(s.m_Cid);

        ResetNext(s);
        Env::SaveVar_T(key, s);

        Env::CallFar(s.m_Cid, Upgradable::ScheduleUpgrade::s_iMethod, nullptr, 0, 1);
    }

    Env::CallFar(s.m_Cid, iMethod, pArg, 0, 1);
}

export void Ctor(Upgradable::Create& r)
{
    Upgradable::State s;
    _POD_(Cast::Down<Upgradable::Current>(s)) = Cast::Down<Upgradable::Current>(r);
    ResetNext(s);

    const uint8_t key = Upgradable::State::s_Key;
    Env::SaveVar_T(key, s);

    if (RefManageSafe<true>(r.m_Cid))
        Env::CallFar(s.m_Cid, 0, &r + 1, 0, 1);
}

export void Dtor(void* pArg)
{
    Upgradable::State s;
    const uint8_t key = Upgradable::State::s_Key;

    Env::LoadVar_T(key, s);
    Env::DelVar_T(key);

    if (RefManageSafe<false>(s.m_Cid))
        Env::CallFar(s.m_Cid, 1, pArg, 0, 1);
}

export void Method_2(const Upgradable::ScheduleUpgrade& r)
{
    Upgradable::State s;
    const uint8_t key = Upgradable::State::s_Key;
    Env::LoadVar_T(key, s);

    Env::Halt_if(r.m_hNextActivate < Env::get_Height() + s.m_hMinUpgadeDelay);

    _POD_(Cast::Down<Upgradable::Next>(s)) = Cast::Down<Upgradable::Next>(r);
    Env::SaveVar_T(key, s);

    Env::AddSig(s.m_Pk);
}

#define UPGR_REDIRECT(iMethod) export void Method_##iMethod(void* pArg) { InvokeNext(iMethod, pArg); }

UPGR_REDIRECT(3)
UPGR_REDIRECT(4)
UPGR_REDIRECT(5)
UPGR_REDIRECT(6)
UPGR_REDIRECT(7)
UPGR_REDIRECT(8)
UPGR_REDIRECT(9)
UPGR_REDIRECT(10)
UPGR_REDIRECT(11)
UPGR_REDIRECT(12)
UPGR_REDIRECT(13)
UPGR_REDIRECT(14)
UPGR_REDIRECT(15)
UPGR_REDIRECT(16)

#undef UPGR_REDIRECT