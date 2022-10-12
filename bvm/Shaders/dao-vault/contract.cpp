////////////////////////
#include "../common.h"
#include "contract.h"
#include "../upgradable3/contract_impl.h"

namespace DaoVault {

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    r.m_Upgradable.TestNumApprovers();
    r.m_Upgradable.Save();
}

BEAM_EXPORT void Dtor(void*)
{
    // N/A
}


BEAM_EXPORT void Method_3(const Method::Deposit& r)
{
    Env::FundsLock(r.m_Aid, r.m_Amount);
}

BEAM_EXPORT void Method_4(const Method::Withdraw& r)
{
    Upgradable3::Settings stg;
    stg.Load();
    stg.TestAdminSigs(r.m_ApproveMask);

    Env::FundsUnlock(r.m_Aid, r.m_Amount);

}

} // namespace DaoVault

namespace Upgradable3 {

    const uint32_t g_CurrentVersion = _countof(DaoVault::s_pSID) - 1;

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
