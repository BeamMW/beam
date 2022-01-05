////////////////////////
#include "../common.h"
#include "contract.h"

namespace DaoVote {

struct MyState
    :public State
{
    void Load() {
        Env::LoadVar_T((uint8_t) Tags::s_State, *this);
    }

    void Save() {
        Env::SaveVar_T((uint8_t) Tags::s_State, *this);
    }

    bool MaybeChangeEpoch()
    {
        auto iEpoch = get_Epoch();
        if (m_iCurrentEpoch == iEpoch)
            return false;

        m_CurrentProposals = m_NextProposals;
        m_NextProposals = 0;
        m_iCurrentEpoch = iEpoch;

        return true;
    }
};

struct MyProposal
{
    Proposal::Key m_Key;
    uint32_t m_Variants;
    Amount m_pVals[Proposal::s_VariantsMax];

    void Load() {
        m_Variants = Env::LoadVar(&m_Key, sizeof(m_Key), m_pVals, sizeof(m_pVals), KeyTag::Internal) / sizeof(*m_pVals);
    }

    void Save() {
        Env::SaveVar(&m_Key, sizeof(m_Key), m_pVals, sizeof(*m_pVals) * m_Variants, KeyTag::Internal);
    }

};

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    MyState s;
    _POD_(s).SetZero();
    _POD_(s.m_Cfg) = r.m_Cfg;
    s.m_hStart = Env::get_Height();
    s.Save();
}

BEAM_EXPORT void Dtor(void*)
{
    // N/A
}

BEAM_EXPORT void Method_2(void*)
{
    // to be called on update
}

BEAM_EXPORT void Method_3(Method::AddProposal& r)
{
    Env::Halt_if(r.m_Data.m_Variants > Proposal::s_VariantsMax);

    MyState s;
    s.Load();
    s.MaybeChangeEpoch();

    MyProposal p;
    p.m_Variants = r.m_Data.m_Variants;
    Env::Memset(p.m_pVals, 0, sizeof(*p.m_pVals) * p.m_Variants);

    p.m_Key.m_ID = ++s.m_iLastProposal;
    s.m_NextProposals++;

    p.Save();
    s.Save();
    Env::AddSig(s.m_Cfg.m_pkAdmin);

    Events::Proposal::Key key;
    key.m_ID_be = Utils::FromBE(p.m_Key.m_ID);

    Env::EmitLog(&key, sizeof(key), &r.m_Data, sizeof(&r.m_Data) + r.m_TxtLen, KeyTag::Internal);
}


} // namespace DaoVote
