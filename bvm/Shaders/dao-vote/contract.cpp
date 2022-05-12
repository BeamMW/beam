////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../upgradable3/contract_impl.h"

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

        if (m_Current.m_Stats.m_StakeVoted)
        {
            if (m_Current.m_iDividendEpoch)
            {
                MyDividend d;
                d.m_Key.m_iEpoch = m_Current.m_iDividendEpoch;

                d.Load();
                d.m_Stake = m_Current.m_Stats.m_StakeVoted;
                d.Save();

                m_Current.m_iDividendEpoch = 0;
            }

            m_Current.m_Stats.m_StakeVoted = 0;
        }

        if (!m_Current.m_iDividendEpoch)
        {
            m_Current.m_iDividendEpoch = m_Next.m_iDividendEpoch;
            m_Next.m_iDividendEpoch = 0;
        }

        Strict::Add(m_Current.m_Stats.m_StakeActive, m_Next.m_StakePassive);
        m_Next.m_StakePassive = 0;

        return true;
    }

    bool LoadUpd_NoSave()
    {
        Load();
        return MaybeCloseEpoch();
    }
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

    void AdjustVotes(UserMax& u, const uint8_t* p1, Amount v0)
    {
        Amount s0 = m_Current.m_Stats.m_StakeVoted;
        if (u.m_iDividendEpoch)
        {
            assert(m_Current.m_Stats.m_StakeVoted >= v0);
            m_Current.m_Stats.m_StakeVoted -= v0;

            u.m_iDividendEpoch = 0;
        }

        MyProposal p;
        p.m_Key.m_ID = get_Proposal0();

        Amount v1 = u.m_Stake;
        bool bAllVoted = true;

        for (uint32_t i = 0; i < m_Current.m_Proposals; i++)
        {
            ++p.m_Key.m_ID;

            auto& n0 = u.m_pVotes[i];
            auto n1 = p1[i];

            if (User::s_NoVote == n1)
                bAllVoted = false;

            if (n0 == n1)
            {
                if (v0 == v1)
                    continue;
                if (User::s_NoVote == n1)
                    continue;
            }

            p.Load();
            if (User::s_NoVote != n0)
            {
                assert(n0 < p.m_Variants);
                assert(p.m_pVariant[n0] >= v0);
                p.m_pVariant[n0] -= v0;
            }

            if (User::s_NoVote != n1)
            {
                Env::Halt_if(n1 >= p.m_Variants);
                p.m_pVariant[n1] += v1;
            }

            n0 = n1;

            p.Save();
        }

        if (bAllVoted && m_Current.m_iDividendEpoch)
        {
            u.m_iDividendEpoch = m_Current.m_iDividendEpoch;
            m_Current.m_Stats.m_StakeVoted += v1;
        }

        assert(m_Current.m_Stats.m_StakeVoted <= m_Current.m_Stats.m_StakeActive);

        if (s0 != m_Current.m_Stats.m_StakeVoted)
            m_Dirty = true;
    }
};

struct MyUser
    :public UserMax
{
    User::Key m_Key;

    void Save(const State& s)
    {
        Env::SaveVar(&m_Key, sizeof(m_Key), this, sizeof(User) + sizeof(*m_pVotes) * s.m_Current.m_Proposals, KeyTag::Internal);
    }

    void TakeDividends() const
    {
        if (!m_Stake)
            return;

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

    void EmitVotes(uint32_t nSizeVotes) const
    {
        if (!nSizeVotes)
            return;

        Events::UserVoteMax uv;
        uv.m_Stake = m_Stake;
        Env::Memcpy(uv.m_pVotes, m_pVotes, nSizeVotes);

        Events::UserVote::Key uvk;
        _POD_(uvk.m_pk) = m_Key.m_pk;
        uvk.m_ID_0_be = Utils::FromBE(m_iProposal0);

        Env::EmitLog(&uvk, sizeof(uvk), &uv, sizeof(Events::UserVote) + nSizeVotes, KeyTag::Internal);
    }

    void LoadPlus(const State& s, const PubKey& pk)
    {
        _POD_(m_Key.m_pk) = pk;
        uint32_t nSize = Env::LoadVar(&m_Key, sizeof(m_Key), this, sizeof(UserMax), KeyTag::Internal);
        if (nSize)
        {
            if (m_iEpoch == s.m_Current.m_iEpoch)
                return;

            EmitVotes(nSize - sizeof(User));

            if (m_iDividendEpoch)
            {
                TakeDividends();
                m_iDividendEpoch = 0;
            }

            m_Stake += m_StakeNext;
            m_StakeNext = 0;
        }
        else
            // new user
            _POD_(Cast::Down<User>(*this)).SetZero();

        // init epoch
        m_iEpoch = s.m_Current.m_iEpoch;
        m_iProposal0 = s.get_Proposal0();

        Env::Memset(m_pVotes, s_NoVote, sizeof(*m_pVotes) * s.m_Current.m_Proposals);
    }
};


BEAM_EXPORT void Ctor(const Method::Create& r)
{
    r.m_Upgradable.TestNumApprovers();
    r.m_Upgradable.Save();

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

BEAM_EXPORT void Method_3(const Method::AddProposal& r)
{
    Env::Halt_if(r.m_Data.m_Variants > Proposal::s_VariantsMax);

    {
        Moderator::Key mk;
        _POD_(mk.m_pk) = r.m_pkModerator;

        Moderator m;
        Env::Halt_if(!Env::LoadVar_T(mk, m));

        Env::AddSig(r.m_pkModerator);
    }

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

    Events::Proposal::Key key;
    key.m_ID_be = Utils::FromBE(p.m_Key.m_ID);

    Env::EmitLog(&key, sizeof(key), &r.m_Data, sizeof(&r.m_Data) + r.m_TxtLen, KeyTag::Internal);
}

BEAM_EXPORT void Method_4(const Method::MoveFunds& r)
{
    MyState_AutoSave s;
    s.m_Dirty = true;

    MyUser u;
    u.LoadPlus(s, r.m_pkUser);

    if (r.m_Lock)
    {
        Strict::Add(s.m_Next.m_StakePassive, r.m_Amount);
        u.m_StakeNext += r.m_Amount;
        Env::FundsLock(s.m_Cfg.m_Aid, r.m_Amount);
    }
    else
    {
        if (u.m_StakeNext >= r.m_Amount)
        {
            u.m_StakeNext -= r.m_Amount;
            assert(s.m_Next.m_StakePassive >= r.m_Amount);
            s.m_Next.m_StakePassive -= r.m_Amount;
        }
        else
        {
            Amount delta = r.m_Amount - u.m_StakeNext;

            assert(s.m_Next.m_StakePassive >= u.m_StakeNext);
            s.m_Next.m_StakePassive -= u.m_StakeNext;
            u.m_StakeNext = 0;

            Amount val0 = u.m_Stake;
            Strict::Sub(u.m_Stake, delta);

            assert(s.m_Current.m_Stats.m_StakeActive >= delta);
            s.m_Current.m_Stats.m_StakeActive -= delta;

            s.AdjustVotes(u, u.m_pVotes, val0);
        }

        Env::FundsUnlock(s.m_Cfg.m_Aid, r.m_Amount);
    }

    Env::AddSig(r.m_pkUser);

    if (u.m_Stake || u.m_StakeNext)
        u.Save(s);
    else
        Env::DelVar_T(u.m_Key);
}

BEAM_EXPORT void Method_5(const Method::Vote& r)
{
    MyState_AutoSave s;

    Env::Halt_if((s.m_Current.m_iEpoch != r.m_iEpoch) || !s.m_Current.m_Proposals);

    MyUser u;
    u.LoadPlus(s, r.m_pkUser);

    Amount val = u.m_Stake;
    Env::Halt_if(!val);

    s.AdjustVotes(u, reinterpret_cast<const uint8_t*>(&r + 1), val);

    u.Save(s);

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

BEAM_EXPORT void Method_8(Method::SetModerator& r)
{
    MyState s;
    s.Load();
    Env::AddSig(s.m_Cfg.m_pkAdmin);

    Moderator::Key mk;
    _POD_(mk.m_pk) = r.m_pk;

    if (r.m_Enable)
    {
        Moderator m;
        m.m_Height = Env::get_Height();
        Env::Halt_if(Env::SaveVar_T(mk, m)); // fail if existed
    }
    else
        Env::Halt_if(!Env::DelVar_T(mk)); // fail unless existed
}

} // namespace DaoVote

namespace Upgradable3 {

    uint32_t get_CurrentVersion()
    {
        return 1;
    }

    void OnUpgraded(uint32_t nPrevVersion)
    {
        Env::Halt_if(nPrevVersion != 0);
    }
}
