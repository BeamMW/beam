////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"

export void Ctor(void*)
{
}

export void Dtor(void*)
{
}

export void Method_2(const Voting::OpenProposal& r)
{
    Env::Halt_if(
        (r.m_Params.m_hMin > r.m_Params.m_hMax) ||
        (r.m_Params.m_hMax < Env::get_Height()) ||
        !r.m_Variants ||
        (r.m_Variants > Voting::Proposal::s_MaxVariants)
    );

    Voting::Proposal_MaxVars p;
    _POD_(p.m_Params) = r.m_Params;

    uint32_t nSizeExtra = sizeof(p.m_pAmount[0]) * r.m_Variants;
    Env::Memset(p.m_pAmount, 0, nSizeExtra);

    auto n = Env::SaveVar(&r.m_ID, sizeof(r.m_ID), &p, sizeof(Voting::Proposal) + nSizeExtra, KeyTag::Internal);
    Env::Halt_if(n); // already existed?
}

void HandleUserAccount(const Voting::UserRequest& r, bool bAdd)
{
    const auto& key = Cast::Down<Voting::UserKey>(r);

    Amount total = 0;
    Env::LoadVar_T(key, total);

    if (bAdd)
        Strict::Add(total, r.m_Amount);
    else
        Strict::Sub(total, r.m_Amount);

    if (total)
        Env::SaveVar_T(key, total);
    else
        Env::DelVar_T(key);
}

export void Method_3(const Voting::Vote& r)
{
    Voting::Proposal_MaxVars p;
    uint32_t n = Env::LoadVar(&r.m_ID, sizeof(r.m_ID), &p, sizeof(p), KeyTag::Internal);
    Env::Halt_if(n < sizeof(Voting::Proposal));

    Height h = Env::get_Height();
    Env::Halt_if((h < p.m_Params.m_hMin) || (h > p.m_Params.m_hMax));

    uint32_t nVariants = (n - sizeof(Voting::Proposal)) / sizeof(p.m_pAmount[0]);
    Env::Halt_if(r.m_Variant >= nVariants);

    Strict::Add(p.m_pAmount[r.m_Variant], r.m_Amount);
    Env::SaveVar(&r.m_ID, sizeof(r.m_ID), &p, n, KeyTag::Internal);

    HandleUserAccount(r, true);
    Env::FundsLock(p.m_Params.m_Aid, r.m_Amount);
}

export void Method_4(const Voting::Withdraw& r)
{
    Voting::Proposal p;
    uint32_t n = Env::LoadVar(&r.m_ID, sizeof(r.m_ID), &p, sizeof(p), KeyTag::Internal);
    Env::Halt_if(n < sizeof(Voting::Proposal));

    Env::Halt_if(p.m_Params.m_hMax >= Env::get_Height());

    HandleUserAccount(r, false);
    Env::FundsUnlock(p.m_Params.m_Aid, r.m_Amount);
    Env::AddSig(r.m_Pk);
}
