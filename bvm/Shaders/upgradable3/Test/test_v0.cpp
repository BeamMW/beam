////////////////////////
#include "../contract_impl.h"
#include "test.h"

BEAM_EXPORT void Ctor(const Upgradable3::Test::Method::Create& r)
{
    r.m_Stgs.TestNumApprovers();
    r.m_Stgs.Save();
}

BEAM_EXPORT void Dtor(void* pArg)
{
}

void Upgradable3::OnUpgraded(uint32_t nPrevVersion)
{
    Env::Halt();
}

uint32_t Upgradable3::get_CurrentVersion()
{
    return 0;
}


BEAM_EXPORT void Method_3(const Upgradable3::Test::Method::Some& r)
{
    Env::Halt_if(r.m_ExpectedVer != 0);
}
