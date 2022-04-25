////////////////////////
#include "../contract_impl.h"
#include "test.h"

BEAM_EXPORT void Ctor(const Upgradable3::Settings& r)
{
    r.TestNumApprovers();
    r.Save();
}

BEAM_EXPORT void Dtor(void* pArg)
{
}

void Upgradable3::OnUpgraded(uint32_t nPrevVersion)
{
    Env::Halt_if(nPrevVersion != 0);
}

uint32_t Upgradable3::get_CurrentVersion()
{
    return 1;
}


BEAM_EXPORT void Method_3(const Upgradable3::Test::SomeMethod& r)
{
    Env::Halt_if(r.m_ExpectedVer != 0);
}
