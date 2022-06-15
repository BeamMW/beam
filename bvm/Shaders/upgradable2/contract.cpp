////////////////////////
#include "../common.h"
#include "contract.h"

struct StatePlus
    :public Upgradable2::State
{
    void Load()
    {
        Env::LoadVar_T((uint16_t) s_Key, *this);
    }

    void Save() const
    {
        Env::SaveVar_T((uint16_t) s_Key, *this);
    }

    void ResetNext()
    {
        _POD_(m_Next.m_Cid).SetZero();
        m_Next.m_hTarget = static_cast<Height>(-1);
    }

    template <bool bAdd>
    bool RefManageSafe() const
    {
        const ContractID& cid = m_Active.m_Cid;

        if (_POD_(cid).IsZero())
            return false;

        if constexpr (bAdd)
            Env::Halt_if(!Env::RefAdd(cid));
        else
            Env::Halt_if(!Env::RefRelease(cid));
        return true;
    }

    bool Load_UpgradeIfNecessary()
    {
        Load();

        if (Env::get_Height() < m_Next.m_hTarget)
            return false;

        RefManageSafe<false>();
        _POD_(m_Active.m_Cid) = m_Next.m_Cid;
        RefManageSafe<true>();

        ResetNext();
        Save();

        CallActive(Upgradable2::Control::s_iMethod, nullptr);
        return true;
    }

    void CallActive(uint32_t iMethod, void* pArg)
    {
        Env::CallFar(m_Active.m_Cid, iMethod, pArg, 0, 1);
    }
};

struct SettingsPlus
    :public Upgradable2::Settings
{
    void Load()
    {
        Env::LoadVar_T((uint16_t) s_Key, *this);
    }

    void Save() const
    {
        Env::SaveVar_T((uint16_t) s_Key, *this);
    }

    void TestNumApprovers() const
    {
        uint32_t val = m_MinApprovers - 1; // would overflow if zero
        Env::Halt_if(val >= s_AdminsMax);
    }

};

BEAM_EXPORT void Ctor(Upgradable2::Create& r)
{
    StatePlus s;
    _POD_(s.m_Active) = r.m_Active;
    s.ResetNext();
    s.Save();

    const auto& stg = Cast::Up<SettingsPlus>(r.m_Settings);
    static_assert(sizeof(stg) == sizeof(r.m_Settings)); // just wrapper, no new members

    stg.TestNumApprovers();
    stg.Save();

    if (s.RefManageSafe<true>())
        s.CallActive(0, &r + 1);
}

BEAM_EXPORT void Dtor(void* pArg)
{
    StatePlus s;
    s.Load();
    Env::DelVar_T((uint16_t) s.s_Key);
    Env::DelVar_T((uint16_t) SettingsPlus::s_Key);

    if (s.RefManageSafe<false>())
        s.CallActive(1, pArg);
}

BEAM_EXPORT void Method_2(const Upgradable2::Control::Base& r_)
{
    typedef Upgradable2::Control Ctl;

    if (Ctl::ExplicitUpgrade::s_Type == r_.m_Type)
    {
        // the only method that does not require admin signature
        StatePlus s;
        Env::Halt_if(!s.Load_UpgradeIfNecessary());
        return;
    }

    const auto& rs_ = Cast::Up<Ctl::Signed>(r_);

    SettingsPlus stg;
    stg.Load();

    uint32_t nMask = rs_.m_ApproveMask;
    uint32_t nSigned = 0;

    for (uint32_t iAdmin = 0; iAdmin < stg.s_AdminsMax; iAdmin++, nMask >>= 1)
    {
        if (1 & nMask)
        {
            Env::AddSig(stg.m_pAdmin[iAdmin]);
            nSigned++;
        }
    }

    Env::Halt_if(nSigned < stg.m_MinApprovers);

    switch (r_.m_Type)
    {
    case Ctl::ScheduleUpgrade::s_Type:
        {
            auto& r = Cast::Up<Ctl::ScheduleUpgrade>(rs_);
            Env::Halt_if(r.m_Next.m_hTarget < Env::get_Height() + stg.m_hMinUpgadeDelay);

            StatePlus s;
            s.Load(); // don't attempt to upgrade here! Newly-scheduled upgrade removes the old one, even if its height already reached

            _POD_(s.m_Next) = r.m_Next;
            s.Save();

        }
        break;

    case Ctl::ReplaceAdmin::s_Type:
        {
            auto& r = Cast::Up<Ctl::ReplaceAdmin>(rs_);
            Env::Halt_if(r.m_iAdmin >= stg.s_AdminsMax);

            _POD_(stg.m_pAdmin[r.m_iAdmin]) = r.m_Pk;
            stg.Save();
        }
        break;


    case Ctl::SetApprovers::s_Type:
        {
            auto& r = Cast::Up<Ctl::SetApprovers>(rs_);
            stg.m_MinApprovers = r.m_NewVal;
            stg.TestNumApprovers();
            stg.Save();
        }
        break;

    default:
        Env::Halt();
    };
}


void InvokeActive(uint32_t iMethod, void* pArg)
{
    StatePlus s;
    s.Load_UpgradeIfNecessary();
    s.CallActive(iMethod, pArg);
}

#define UPGR_REDIRECT(iMethod) BEAM_EXPORT void Method_##iMethod(void* pArg) { InvokeActive(iMethod, pArg); }

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