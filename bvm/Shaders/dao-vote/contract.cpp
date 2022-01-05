////////////////////////
#include "../common.h"
#include "../Math.h"
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

    void LoadPlus()
    {
        Load();
        UpdateEpoch();
    }

    void AdjustVotes(const uint8_t* p0, const uint8_t* p1, Amount v0, Amount v1, bool bHasPrev) const;
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

void MyState::AdjustVotes(const uint8_t* p0, const uint8_t* p1, Amount v0, Amount v1, bool bHasPrev) const
{
    MyProposal p;
    p.m_Key.m_ID = m_iLastProposal - m_NextProposals - m_CurrentProposals;

    bool bTrySkip = (bHasPrev && (v0 == v1));

    for (uint32_t i = 0; i < m_CurrentProposals; i++)
    {
        ++p.m_Key.m_ID;

        if (bTrySkip && (p0[i] == p1[i]))
            continue;

        p.Load();
        if (bHasPrev)
        {
            auto n0 = p0[0];
            assert(n0 < p.m_Variants);
            assert(p.m_pVals[n0] >= v0);
            p.m_pVals[n0] -= v0;
        }

        auto n1 = p1[0];
        Env::Halt_if(n1 >= p.m_Variants);
        p.m_pVals[n1] += v1;

        p.Save();
    }
}

struct MyUser
{
    User::Key m_Key;
    uint32_t m_Proposals;

    struct UserPlus
        :public User
    {
        uint8_t m_pVotes[Proposal::s_ProposalsPerEpochMax];
    } m_U;

    bool Load()
    {
        uint32_t nSize = Env::LoadVar(&m_Key, sizeof(m_Key), &m_U, sizeof(m_U), KeyTag::Internal);
        if (!nSize)
            return false;

        m_Proposals = (nSize - sizeof(User)) / sizeof(*m_U.m_pVotes);
        return true;
    }

    void Save() {
        Env::SaveVar(&m_Key, sizeof(m_Key), &m_U, sizeof(User) + sizeof(*m_U.m_pVotes) * m_Proposals, KeyTag::Internal);
    }

    void OnEpoch(uint32_t iEpoch)
    {
        if (m_U.m_iEpoch != iEpoch)
        {
            m_U.m_iEpoch = iEpoch;
            m_U.m_Stake += m_U.m_StakeNext;
            m_U.m_StakeNext = 0;
            m_Proposals = 0;
        }
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

BEAM_EXPORT void Method_3(const Method::AddProposal& r)
{
    Env::Halt_if(r.m_Data.m_Variants > Proposal::s_VariantsMax);

    MyState s;
    s.LoadPlus();

    MyProposal p;
    p.m_Variants = r.m_Data.m_Variants;
    Env::Memset(p.m_pVals, 0, sizeof(*p.m_pVals) * p.m_Variants);

    p.m_Key.m_ID = ++s.m_iLastProposal;

    s.m_NextProposals++;
    Env::Halt_if(s.m_NextProposals > Proposal::s_ProposalsPerEpochMax);

    p.Save();
    s.Save();
    Env::AddSig(s.m_Cfg.m_pkAdmin);

    Events::Proposal::Key key;
    key.m_ID_be = Utils::FromBE(p.m_Key.m_ID);

    Env::EmitLog(&key, sizeof(key), &r.m_Data, sizeof(&r.m_Data) + r.m_TxtLen, KeyTag::Internal);
}

BEAM_EXPORT void Method_4(const Method::MoveFunds& r)
{
    MyState s;
    s.LoadPlus();

    MyUser u;
    _POD_(u.m_Key.m_pk) = r.m_pkUser;
    if (u.Load())
        u.OnEpoch(s.m_iCurrentEpoch);
    else
    {
        _POD_(Cast::Down<User>(u.m_U)).SetZero();
        u.m_U.m_iEpoch = s.m_iCurrentEpoch;
        u.m_Proposals = 0;
    }

    if (r.m_Lock)
    {
        u.m_U.m_StakeNext += r.m_Amount;
        Env::FundsLock(s.m_Cfg.m_Aid, r.m_Amount);
    }
    else
    {
        if (u.m_U.m_StakeNext >= r.m_Amount)
            u.m_U.m_StakeNext -= r.m_Amount;
        else
        {
            Amount val0 = u.m_U.m_Stake;

            Strict::Sub(u.m_U.m_Stake, r.m_Amount - u.m_U.m_StakeNext);
            u.m_U.m_StakeNext = 0;

            if (u.m_Proposals)
                s.AdjustVotes(u.m_U.m_pVotes, u.m_U.m_pVotes, val0, u.m_U.m_Stake, true);
        }

        Env::FundsUnlock(s.m_Cfg.m_Aid, r.m_Amount);

        Env::AddSig(r.m_pkUser);
    }

    if (u.m_U.m_Stake || u.m_U.m_StakeNext)
        u.Save();
    else
        Env::DelVar_T(u.m_Key);
}

BEAM_EXPORT void Method_5(const Method::Vote& r)
{
    MyState s;
    s.LoadPlus();

    Env::Halt_if((s.m_iCurrentEpoch != r.m_iEpoch) || !s.m_CurrentProposals);

    MyUser u;
    _POD_(u.m_Key.m_pk) = r.m_pkUser;
    Env::Halt_if(!u.Load());

    u.OnEpoch(s.m_iCurrentEpoch);

    Amount val = u.m_U.m_Stake;
    Env::Halt_if(!val);

    auto pV = reinterpret_cast<const uint8_t*>(&r + 1);

    s.AdjustVotes(u.m_U.m_pVotes, pV, val, val, (u.m_Proposals == s.m_CurrentProposals));

    u.m_Proposals = s.m_CurrentProposals;
    Env::Memcpy(u.m_U.m_pVotes, pV, sizeof(*pV) * u.m_Proposals);

    u.Save();

    Env::AddSig(r.m_pkUser);
}

} // namespace DaoVote
