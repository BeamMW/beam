////////////////////////
#include "../common.h"
#include "contract.h"

namespace Upgradable3 {

void Settings::Save() const
{
    Key key;
    Env::SaveVar_T(key, *this);
}

typedef Method::Control Ctl;

BEAM_EXPORT void Method_2(const Ctl::Base& r_)
{
    // first - methods that don't require admin signature
    switch (r_.m_Type)
    {
    case Ctl::ExplicitUpgrade::s_Type:
        {
            NextVersion::Key nvk;
            uint32_t nVal = Env::LoadVar(&nvk, sizeof(nvk), nullptr, 0, KeyTag::Internal);
            Env::Halt_if(nVal < sizeof(NextVersion));

            auto* pVer = (NextVersion*) Env::Heap_Alloc(nVal);
            Env::LoadVar(&nvk, sizeof(nvk), pVer, nVal, KeyTag::Internal);

            Env::Halt_if(Env::get_Height() < pVer->m_hTarget);

            Env::DelVar_T(nvk);
            Env::UpdateShader(pVer + 1, nVal - sizeof(NextVersion));

            Env::Heap_Free(pVer);

            ContractID cid;
            Env::get_CallerCid(0, cid);

            Ctl::OnUpgraded args;
            args.m_PrevVersion = get_CurrentVersion();

            Env::CallFar(cid, Ctl::s_iMethod, &args, sizeof(args), 1);
        }
        return;

    case Ctl::OnUpgraded::s_Type:
        {
            // ensure we're being-called by ourselves
            ContractID cid0, cid1;
            Env::get_CallerCid(0, cid0);
            Env::get_CallerCid(1, cid1);
            Env::Halt_if(_POD_(cid0) != cid1);

            OnUpgraded(Cast::Up<Ctl::OnUpgraded>(r_).m_PrevVersion);
        }
        return;
    }

    const auto& rs_ = Cast::Up<Ctl::Signed>(r_);

    Settings stg;
    Settings::Key stgk;
    /*Env::Halt_if(!*/Env::LoadVar_T(stgk, stg)/*)*/;

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
            Env::Halt_if(r.m_Next.m_hTarget < Env::get_Height() + stg.m_hMinUpgradeDelay);

            NextVersion::Key nvk;
            Env::SaveVar(&nvk, sizeof(nvk), &r.m_Next, sizeof(r.m_Next) + r.m_SizeShader, KeyTag::Internal);
        }
        return; // settings remain unchanged

    case Ctl::ReplaceAdmin::s_Type:
        {
            auto& r = Cast::Up<Ctl::ReplaceAdmin>(rs_);
            Env::Halt_if(r.m_iAdmin >= stg.s_AdminsMax);

            _POD_(stg.m_pAdmin[r.m_iAdmin]) = r.m_Pk;
        }
        break;


    case Ctl::SetApprovers::s_Type:
        {
            auto& r = Cast::Up<Ctl::SetApprovers>(rs_);
            stg.m_MinApprovers = r.m_NewVal;
            stg.TestNumApprovers();
        }
        break;

    default:
        Env::Halt();
    };

    Env::SaveVar_T(stgk, stg);
}


} // namespace Upgradable3
