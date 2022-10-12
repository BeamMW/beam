////////////////////////
#include "../common.h"
#include "contract.h"
#include "../upgradable2/contract.h"

template <typename T>
T ToValue(const T& x)
{
    return x;
}

BEAM_EXPORT void Ctor(void*)
{
}

BEAM_EXPORT void Dtor(void*)
{
}

BEAM_EXPORT void Method_2(void*)
{
    // called on upgrade by Upgradable2
    // perform the migration of admin structs, and do the upgrade as necessary

    Upgradable2::State st2;
    Env::Halt_if(!Env::LoadVar_T(ToValue(Upgradable2::State::s_Key), st2));
    Env::DelVar_T(ToValue(Upgradable2::State::s_Key));

    Env::Halt_if(!Env::RefRelease(st2.m_Active.m_Cid));

    Upgradable2::Settings stg2;
    Env::Halt_if(!Env::LoadVar_T(ToValue(Upgradable2::Settings::s_Key), stg2));
    Env::DelVar_T(ToValue(Upgradable2::Settings::s_Key));

    Upgradable3::Settings stg3;
    _POD_(stg3.m_pAdmin) = stg2.m_pAdmin;
    stg3.m_hMinUpgradeDelay = stg2.m_hMinUpgadeDelay;
    stg3.m_MinApprovers = stg2.m_MinApprovers;

    Env::SaveVar_T(Upgradable3::Settings::Key(), stg3);

    Env::UpdateShader(s_pCocoon, sizeof(s_pCocoon));

    Upgradable3::OnUpgraded_From2();
}
