////////////////////////
#include "../common.h"
#include "contract.h"
#include "../upgradable3/contract_impl.h"

namespace DaoVault {

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    r.m_Upgradable.TestNumApprovers();
    r.m_Upgradable.Save();

    Pool0 p;
    p.m_aidStaking = r.m_aidStaking;
    p.m_Weight = 0;

    auto key = Tags::s_Pool;
    Env::SaveVar_T(key, p);
}

BEAM_EXPORT void Dtor(void*)
{
    // N/A
}

struct MyPool
    :public PoolMaxPlus
{
    MyPool()
    {
        auto key = Tags::s_Pool;
        auto nSize = Env::LoadVar(&key, sizeof(key), this, sizeof(PoolMax), KeyTag::Internal);
        m_Assets = (nSize - sizeof(Pool0)) / sizeof(PerAsset);
    }

    ~MyPool()
    {
        auto key = Tags::s_Pool;
        Env::SaveVar(&key, sizeof(key), this, sizeof(Pool0) + m_Assets * sizeof(PerAsset), KeyTag::Internal);
    }
};

BEAM_EXPORT void Method_3(const Method::Deposit& r)
{
    MyPool p;
    p.Add(r.m_Aid, r.m_Amount);
    Env::FundsLock(r.m_Aid, r.m_Amount);
}

BEAM_EXPORT void Method_4(const Method::UserUpdate& r)
{
    MyPool p;

    User0::Key uk;
    _POD_(uk.m_pk) = r.m_pkUser;
    Env::AddSig(r.m_pkUser);

    UserMax u;
    auto nSize = Env::LoadVar(&uk, sizeof(uk), &u, sizeof(UserMax), KeyTag::Internal);
    if (nSize)
    {
        uint32_t nAssets = (nSize - sizeof(User0)) / sizeof(User0::PerAsset);
        u.Remove(p, nAssets);
    }
    else
        Env::Memset(&u, 0, sizeof(User0) + sizeof(User0::PerAsset) * p.m_Assets);

    if (u.m_Weight > r.m_NewStaking)
    {
        Env::Halt_if(Env::get_Height() == u.m_hLastDeposit);
        Env::FundsUnlock(p.m_aidStaking, u.m_Weight - r.m_NewStaking);
    }
    else
    {
        if (r.m_NewStaking > u.m_Weight)
        {
            u.m_hLastDeposit = Env::get_Height();
            Env::FundsLock(p.m_aidStaking, r.m_NewStaking - u.m_Weight);
        }
    }

    u.m_Weight = r.m_NewStaking;
    u.Add(p);

    uint32_t nCount = r.m_WithdrawCount;
    bool bEmpty = !u.m_Weight;
    const Amount* pWithdraw = (const Amount *) (&r + 1);

    if (nCount > p.m_Assets)
    {
        nCount = p.m_Assets;
        pWithdraw = nullptr;
    }

    for (uint32_t i = 0; i < nCount; i++)
    {
        auto& x = u.m_p[i];
        Amount val;

        if (pWithdraw)
        {
            val = pWithdraw[i];
            Strict::Sub(x.m_Value, val);

            if (x.m_Value)
                bEmpty = false;
        }
        else
        {
            val = x.m_Value;
            x.m_Value = 0;
        }

        Env::FundsUnlock(p.m_p[i].m_Aid, val);
    }


    if (bEmpty)
        Env::DelVar_T(uk);
    else
        Env::SaveVar(&uk, sizeof(uk), &u, sizeof(User0) + p.m_Assets * sizeof(User0::PerAsset), KeyTag::Internal);
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
