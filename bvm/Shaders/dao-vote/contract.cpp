////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"

namespace DaoVote {

struct MyDividend
    :public DividendMax
{
    Key m_Key;
    uint32_t m_Assets;

    void Load() {
        uint32_t nSize = Env::LoadVar(&m_Key, sizeof(m_Key), this, sizeof(DividendMax), KeyTag::Internal);
        assert(nSize >= sizeof(Dividend0));
        m_Assets = (nSize - sizeof(Dividend0)) / sizeof(*m_pArr);
    }

    void Save() {
        Env::SaveVar(&m_Key, sizeof(m_Key), this, sizeof(Dividend0) + sizeof(*m_pArr) * m_Assets, KeyTag::Internal);
    }
};

struct MyState
    :public State
{
    void Load() {
        Env::LoadVar_T((uint8_t) Tags::s_State, *this);
    }

    void Save() {
        Env::SaveVar_T((uint8_t) Tags::s_State, *this);
    }

    bool MaybeCloseEpoch()
    {
        auto iEpoch = get_Epoch();
        if (m_Current.m_iEpoch == iEpoch)
            return false;

        m_Current.m_iEpoch = iEpoch;

        m_Current.m_Proposals = m_Next.m_Proposals;
        m_Next.m_Proposals = 0;

        if (m_Current.m_Stake)
        {
            if (m_Current.m_iDividendEpoch)
            {
                MyDividend d;
                d.m_Key.m_iEpoch = m_Current.m_iDividendEpoch;

                d.Load();
                d.m_Stake = m_Current.m_Stake;
                d.Save();

                m_Current.m_iDividendEpoch = 0;
            }

            m_Current.m_Stake = 0;
        }

        if (!m_Current.m_iDividendEpoch)
        {
            m_Current.m_iDividendEpoch = m_Next.m_iDividendEpoch;
            m_Next.m_iDividendEpoch = 0;
        }

        return true;
    }

    bool LoadUpd_NoSave()
    {
        Load();
        return MaybeCloseEpoch();
    }

    void AdjustVotes(const uint8_t* p0, const uint8_t* p1, Amount v0, Amount v1, bool bHasPrev) const;
};

struct MyProposal
    :public ProposalMax
{
    Proposal::Key m_Key;
    uint32_t m_Variants;

    void Load() {
        uint32_t nSize = Env::LoadVar(&m_Key, sizeof(m_Key), this, sizeof(ProposalMax), KeyTag::Internal);
        m_Variants = nSize / sizeof(*m_pVariant);
    }

    void Save() {
        Env::SaveVar(&m_Key, sizeof(m_Key), this, sizeof(*m_pVariant) * m_Variants, KeyTag::Internal);
    }
};

void MyState::AdjustVotes(const uint8_t* p0, const uint8_t* p1, Amount v0, Amount v1, bool bHasPrev) const
{
    MyProposal p;
    p.m_Key.m_ID = m_iLastProposal - m_Next.m_Proposals - m_Current.m_Proposals;

    bool bTrySkip = (bHasPrev && (v0 == v1));

    for (uint32_t i = 0; i < m_Current.m_Proposals; i++)
    {
        ++p.m_Key.m_ID;

        if (bTrySkip && (p0[i] == p1[i]))
            continue;

        p.Load();
        if (bHasPrev)
        {
            auto n0 = p0[i];
            assert(n0 < p.m_Variants);
            assert(p.m_pVariant[n0] >= v0);
            p.m_pVariant[n0] -= v0;
        }

        auto n1 = p1[i];
        Env::Halt_if(n1 >= p.m_Variants);
        p.m_pVariant[n1] += v1;

        p.Save();
    }
}

struct MyState_AutoSave
    :public MyState
{
    bool m_Dirty;

    MyState_AutoSave()
    {
        Load();
        m_Dirty = MaybeCloseEpoch();
    }

    ~MyState_AutoSave()
    {
        if (m_Dirty)
            Save();
    }
};

struct MyUser
    :public UserMax
{
    User::Key m_Key;
    uint32_t m_Proposals;

    bool Load()
    {
        uint32_t nSize = Env::LoadVar(&m_Key, sizeof(m_Key), this, sizeof(UserMax), KeyTag::Internal);
        if (!nSize)
            return false;

        m_Proposals = (nSize - sizeof(User)) / sizeof(*m_pVotes);
        return true;
    }

    void Save() {
        Env::SaveVar(&m_Key, sizeof(m_Key), this, sizeof(User) + sizeof(*m_pVotes) * m_Proposals, KeyTag::Internal);
    }

    void OnEpoch(uint32_t iEpoch)
    {
        if (m_iEpoch != iEpoch)
        {
            if (m_iDividendEpoch)
            {
                if (m_Stake)
                {
                    MyDividend d;
                    d.m_Key.m_iEpoch = m_iDividendEpoch;
                    d.Load();

                    bool bLast = (m_Stake == d.m_Stake);
                    MultiPrecision::Float k;

                    if (!bLast)
                    {
                        assert(m_Stake < d.m_Stake);
                        k = MultiPrecision::Float(m_Stake) / MultiPrecision::Float(d.m_Stake);
                    }

                    for (uint32_t i = 0; i < d.m_Assets; i++)
                    {
                        auto& v = d.m_pArr[i];

                        Amount val = v.m_Amount;
                        if (!bLast)
                        {
                            val = MultiPrecision::Float(val) * k;
                            v.m_Amount -= val;
                        }

                        Env::FundsUnlock(v.m_Aid, val);
                    }

                    if (bLast)
                        Env::DelVar_T(d.m_Key);
                    else
                    {
                        d.m_Stake -= m_Stake;
                        d.Save();
                    }
                }

                m_iDividendEpoch = 0;
            }

            m_iEpoch = iEpoch;
            m_Stake += m_StakeNext;
            m_StakeNext = 0;
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
    s.LoadUpd_NoSave();

    MyProposal p;
    p.m_Variants = r.m_Data.m_Variants;
    Env::Memset(p.m_pVariant, 0, sizeof(*p.m_pVariant) * p.m_Variants);

    p.m_Key.m_ID = ++s.m_iLastProposal;

    s.m_Next.m_Proposals++;
    Env::Halt_if(s.m_Next.m_Proposals > Proposal::s_ProposalsPerEpochMax);

    p.Save();
    s.Save();
    Env::AddSig(s.m_Cfg.m_pkAdmin);

    Events::Proposal::Key key;
    key.m_ID_be = Utils::FromBE(p.m_Key.m_ID);

    Env::EmitLog(&key, sizeof(key), &r.m_Data, sizeof(&r.m_Data) + r.m_TxtLen, KeyTag::Internal);
}

BEAM_EXPORT void Method_4(const Method::MoveFunds& r)
{
    MyState_AutoSave s;

    MyUser u;
    _POD_(u.m_Key.m_pk) = r.m_pkUser;
    if (u.Load())
        u.OnEpoch(s.m_Current.m_iEpoch);
    else
    {
        _POD_(Cast::Down<User>(u)).SetZero();
        u.m_iEpoch = s.m_Current.m_iEpoch;
        u.m_Proposals = 0;
    }

    if (r.m_Lock)
    {
        u.m_StakeNext += r.m_Amount;
        Env::FundsLock(s.m_Cfg.m_Aid, r.m_Amount);
    }
    else
    {
        if (u.m_StakeNext >= r.m_Amount)
            u.m_StakeNext -= r.m_Amount;
        else
        {
            Amount delta = r.m_Amount - u.m_StakeNext;
            u.m_StakeNext = 0;

            Amount val0 = u.m_Stake;
            Strict::Sub(u.m_Stake, delta);

            if (u.m_Proposals)
            {
                s.AdjustVotes(u.m_pVotes, u.m_pVotes, val0, u.m_Stake, true);
                s.m_Current.m_Stake -= delta;
                s.m_Dirty = true;
            }
        }

        Env::FundsUnlock(s.m_Cfg.m_Aid, r.m_Amount);
    }

    Env::AddSig(r.m_pkUser);

    if (u.m_Stake || u.m_StakeNext)
        u.Save();
    else
        Env::DelVar_T(u.m_Key);
}

BEAM_EXPORT void Method_5(const Method::Vote& r)
{
    MyState_AutoSave s;

    Env::Halt_if((s.m_Current.m_iEpoch != r.m_iEpoch) || !s.m_Current.m_Proposals);

    MyUser u;
    _POD_(u.m_Key.m_pk) = r.m_pkUser;
    Env::Halt_if(!u.Load());

    u.OnEpoch(s.m_Current.m_iEpoch);

    Amount val = u.m_Stake;
    Env::Halt_if(!val);

    auto pV = reinterpret_cast<const uint8_t*>(&r + 1);

    bool bHasPrev = (u.m_Proposals == s.m_Current.m_Proposals);
    s.AdjustVotes(u.m_pVotes, pV, val, val, bHasPrev);

    if (!bHasPrev)
    {
        u.m_iDividendEpoch = s.m_Current.m_iDividendEpoch;
        s.m_Current.m_Stake += val;
        s.m_Dirty = true;
    }

    u.m_Proposals = s.m_Current.m_Proposals;
    Env::Memcpy(u.m_pVotes, pV, sizeof(*pV) * u.m_Proposals);

    u.Save();

    Env::AddSig(r.m_pkUser);
}

BEAM_EXPORT void Method_6(const Method::AddDividend& r)
{
    Env::Halt_if(!r.m_Val.m_Amount);
    Env::FundsLock(r.m_Val.m_Aid, r.m_Val.m_Amount);

    MyState_AutoSave s;

    MyDividend d;
    if (s.m_Next.m_iDividendEpoch)
    {
        d.m_Key.m_iEpoch = s.m_Next.m_iDividendEpoch;
        d.Load();
    }
    else
    {
        s.m_Next.m_iDividendEpoch = s.m_Current.m_iEpoch + 1;
        s.m_Dirty = true;
        d.m_Key.m_iEpoch = s.m_Next.m_iDividendEpoch;
        d.m_Assets = 0;
        d.m_Stake = 0;
    }

    for (uint32_t i = 0; ; i++)
    {
        if (d.m_Assets == i)
        {
            Env::Halt_if(d.m_Assets == d.s_AssetsMax);
            d.m_pArr[d.m_Assets++] = r.m_Val;
            break;
        }

        auto& v = d.m_pArr[i];
        if (v.m_Aid == r.m_Val.m_Aid)
        {
            Strict::Add(v.m_Amount, r.m_Val.m_Amount);
            break;
        }
    }

    d.Save();
}

BEAM_EXPORT void Method_7(Method::GetResults& r)
{
    MyState s;
    s.Load();

    r.m_Finished = 0;

    if (r.m_ID > s.m_iLastProposal)
        r.m_Variants = 0;
    else
    {
        MyProposal p;
        p.m_Key.m_ID = r.m_ID;
        p.Load();

        uint32_t nVars = std::min(r.m_Variants, p.m_Variants);
        r.m_Variants = p.m_Variants;

        Env::Memcpy(&r + 1, p.m_pVariant, sizeof(*p.m_pVariant) * nVars);

        uint32_t nUnfinished = s.m_Next.m_Proposals;
        if (s.get_Epoch() == s.m_Current.m_iEpoch)
            nUnfinished += s.m_Current.m_Proposals;

        if (r.m_ID <= s.m_iLastProposal - nUnfinished)
            r.m_Finished = 1;
    }
}

} // namespace DaoVote
