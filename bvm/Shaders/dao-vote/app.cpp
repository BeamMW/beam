#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable2/app_common_impl.h"
#include "../Math.h"

#define DaoVote_manager_view(macro)
#define DaoVote_manager_view_params(macro) macro(ContractID, cid)
#define DaoVote_manager_my_admin_key(macro)
#define DaoVote_manager_explicit_upgrade(macro) macro(ContractID, cid)

#define DaoVote_manager_view_proposals(macro) \
    macro(ContractID, cid) \
    macro(Proposal::ID, id0) \
    macro(uint32_t, nCount)

#define DaoVote_manager_view_proposal(macro) \
    macro(ContractID, cid) \
    macro(Proposal::ID, id)

#define DaoVote_manager_add_proposal(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, variants)

#define DaoVote_manager_add_dividend(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define DaoVoteRole_manager(macro) \
    macro(manager, view) \
    macro(manager, explicit_upgrade) \
    macro(manager, view_params) \
    macro(manager, view_proposals) \
    macro(manager, view_proposal) \
    macro(manager, add_proposal) \
    macro(manager, add_dividend) \
    macro(manager, my_admin_key)

#define DaoVote_user_my_key(macro) macro(ContractID, cid)
#define DaoVote_user_view(macro) macro(ContractID, cid)
#define DaoVote_user_vote(macro) macro(ContractID, cid)

#define DaoVote_user_move_funds(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount) \
    macro(uint32_t, bLock)

#define DaoVoteRole_user(macro) \
    macro(user, my_key) \
    macro(user, view) \
    macro(user, move_funds) \
    macro(user, vote) \

#define DaoVoteRoles_All(macro) \
    macro(manager) \
    macro(user)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  DaoVote_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); DaoVoteRole_##name(THE_METHOD) }
        
        DaoVoteRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(DaoVote_##role##_##name(THE_FIELD) int unused = 0)

namespace DaoVote {

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr2-dao-vote";

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(&g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

ON_METHOD(manager, view)
{
    static const ShaderID s_pSid[] = {
        DaoVote::s_SID,
    };

    ContractID pVerCid[_countof(s_pSid)];
    Height pVerDeploy[_countof(s_pSid)];

    ManagerUpgadable2::Walker wlk;
    wlk.m_VerInfo.m_Count = _countof(s_pSid);
    wlk.m_VerInfo.s_pSid = s_pSid;
    wlk.m_VerInfo.m_pCid = pVerCid;
    wlk.m_VerInfo.m_pHeight = pVerDeploy;

    AdminKeyID kid;
    wlk.ViewAll(&kid);
}

ON_METHOD(manager, explicit_upgrade)
{
    ManagerUpgadable2::MultiSigRitual::Perform_ExplicitUpgrade(cid);
}

struct MyState
    :public State
{
    bool Load(const ContractID& cid)
    {
        Env::Key_T<uint8_t> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        key.m_KeyInContract = Tags::s_State;

        if (Env::VarReader::Read_T(key, *this))
            return true;

        OnError("no state");
        return false;
    }

    struct PrevDividend {
        uint32_t m_iEpoch;
        Amount m_Stake;
    } m_PrevDividend;

    bool MaybeNextEpoch()
    {
        uint32_t iEpoch = get_Epoch();
        if (iEpoch == m_Current.m_iEpoch)
            return false;

        m_Current.m_iEpoch = iEpoch;

        m_Current.m_Proposals = m_Next.m_Proposals;
        m_Next.m_Proposals = 0;

        _POD_(m_PrevDividend).SetZero();

        if (m_Current.m_Stake)
        {
            if (m_Current.m_iDividendEpoch)
            {
                m_PrevDividend.m_iEpoch = m_Current.m_iDividendEpoch;
                m_PrevDividend.m_Stake = m_Current.m_Stake;

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
};

struct MyDividend
    :public DividendMax
{
    uint32_t m_Assets;

    void Load(const ContractID& cid, uint32_t iEpoch)
    {
        Env::Key_T<Dividend0::Key> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        key.m_KeyInContract.m_iEpoch = iEpoch;

        Env::VarReaderEx<false> r(key, key);

        uint32_t nKey = 0, nSize = sizeof(DividendMax);
        Env::Halt_if(!r.MoveNext(nullptr, nKey, this, nSize, 0));
        Env::Halt_if(nSize < sizeof(Dividend0));

        m_Assets = (nSize - sizeof(Dividend0)) / sizeof(*m_pArr);
    }

    void Write() const
    {
        Env::DocArray gr("dividend");

        for (uint32_t i = 0; i < m_Assets; i++)
        {
            const auto& x = m_pArr[i];
            Env::DocGroup gr1("");

            Env::DocAddNum("aid", x.m_Aid);
            Env::DocAddNum("amount", x.m_Amount);
        }
    }
};

void WriteDividend(const ContractID& cid, uint32_t iDividendEpoch)
{
    if (!iDividendEpoch)
        return;

    MyDividend d;
    d.Load(cid, iDividendEpoch);
    d.Write();
}

ON_METHOD(manager, view_params)
{
    MyState s;
    if (!s.Load(cid))
        return;
    s.MaybeNextEpoch();

    Env::DocGroup gr("params");
    Env::DocAddNum("epoch_dh", s.m_Cfg.m_hEpochDuration);
    Env::DocAddNum("aid", s.m_Cfg.m_Aid);
    Env::DocAddBlob_T("pkAdmin", s.m_Cfg.m_pkAdmin);

    {
        AdminKeyID kid;
        PubKey pk;
        kid.get_Pk(pk);
        Env::DocAddNum("is_admin", (uint32_t) (_POD_(pk) == s.m_Cfg.m_pkAdmin));
    }

    Env::DocAddNum("total_proposals", s.m_iLastProposal);

    {
        Env::DocGroup gr("current");
        Env::DocAddNum("iEpoch", s.m_Current.m_iEpoch);
        Env::DocAddNum("proposals", s.m_Current.m_Proposals);

        WriteDividend(cid, s.m_Current.m_iDividendEpoch);
    }
    {
        Env::DocGroup gr("next");
        Env::DocAddNum("proposals", s.m_Next.m_Proposals);

        WriteDividend(cid, s.m_Next.m_iDividendEpoch);
    }

}

ON_METHOD(manager, my_admin_key)
{
    PubKey pk;
    AdminKeyID().get_Pk(pk);
    Env::DocAddBlob_T("admin_key", pk);
}

ON_METHOD(manager, view_proposals)
{
    Env::Key_T<Events::Proposal::Key> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k1.m_Prefix.m_Cid) = cid;
    k0.m_KeyInContract.m_ID_be = Utils::FromBE(id0);

    uint32_t id1 = id0 + nCount - 1;
    k1.m_KeyInContract.m_ID_be = (id1 >= id0) ? Utils::FromBE(id1) : static_cast<uint32_t>(-1);

    Env::DocArray gr("res");

    for (Env::LogReader r(k0, k1); ; )
    {
        uint32_t nKey = sizeof(k0), nVal = 0;
        if (!r.MoveNext(&k0, nKey, nullptr, nVal, 0))
            break;

        if ((sizeof(k0) != nKey) || (nVal < sizeof(Events::Proposal)))
            continue;

        auto* pProp = (Events::Proposal*) Env::StackAlloc(nVal + 1);
        r.MoveNext(&k0, nKey, pProp, nVal, 1);

        Env::DocGroup gr1("");
        Env::DocAddNum("id", Utils::FromBE(k0.m_KeyInContract.m_ID_be));
        Env::DocAddNum("height", r.m_Pos.m_Height);
        Env::DocAddNum("variants", pProp->m_Variants);

        ((char*) pProp)[nVal] = 0; // text 0-term
        Env::DocAddText("text", (const char*) (pProp + 1));
        
        Env::StackFree(nVal + 1);
    }
}

ON_METHOD(manager, add_proposal)
{
    if (!variants || (variants > Proposal::s_VariantsMax))
        return OnError("num of variants invalid");

    MyState s;
    if (!s.Load(cid))
        return;
    s.MaybeNextEpoch();

    if (s.m_Next.m_Proposals >= Proposal::s_ProposalsPerEpochMax)
        return OnError("too many proposals");

    static const char s_szName[] = "text";
    uint32_t nLen = Env::DocGetText(s_szName, nullptr, 0);
    if (nLen <= 1)
        OnError("text missing");

    uint32_t nArgsSize = sizeof(Method::AddProposal) + nLen;
    auto* pArgs = (Method::AddProposal*) Env::Heap_Alloc(nArgsSize);
    Env::DocGetText(s_szName, (char*) (pArgs + 1), nLen);

    pArgs->m_TxtLen = nLen - 1;
    pArgs->m_Data.m_Variants = variants;

    AdminKeyID kid;
    Env::GenerateKernel(&cid, pArgs->s_iMethod, pArgs, nArgsSize, nullptr, 0, &kid, 1, "dao-vote add proposal", 0);

    Env::Heap_Free(pArgs);
}

ON_METHOD(manager, add_dividend)
{
    if (!amount)
        return OnError("amount not specified");
    MyState s;
    if (!s.Load(cid))
        return;
    s.MaybeNextEpoch();

    Method::AddDividend args;
    args.m_Val.m_Aid = aid;
    args.m_Val.m_Amount = amount;

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), &fc, 1, nullptr, 0, "dao-vote add dividend", 0);
}

ON_METHOD(manager, view_proposal)
{
    Env::Key_T<Proposal::Key> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_KeyInContract.m_ID = id;

    Env::VarReader r(key, key);

    ProposalMax p;
    uint32_t nKey = sizeof(key), nVal = sizeof(p);
    if (!r.MoveNext(&key, nKey, &p, nVal, 0))
        return OnError("no proposal");

    uint32_t nVariants = nVal / sizeof(*p.m_pVariant);

    Amount total = 0;
    {
        Env::DocArray gr("variants");
        for (uint32_t i = 0; i < nVariants; i++)
        {
            Env::DocAddNum("", p.m_pVariant[i]);
            total += p.m_pVariant[i];
        }
    }
    Env::DocAddNum("total", total);
}

struct UserKeyID :public Env::KeyID {
    UserKeyID(const ContractID& cid)
    {
        m_pID = &cid;
        m_nID = sizeof(cid);
    }
};

ON_METHOD(user, my_key)
{
    PubKey pk;
    UserKeyID(cid).get_Pk(pk);
    Env::DocAddBlob_T("key", pk);
}

struct MyUser
    :public UserMax
{
    uint32_t m_Votes = 0;
    uint32_t m_Charge = 0;

    MyDividend m_Dividend;

    bool Load(const ContractID& cid, const MyState& s)
    {
        m_Charge = Env::Cost::LoadVar;
        m_Dividend.m_Assets = 0;

        Env::Key_T<User::Key> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        UserKeyID(cid).get_Pk(key.m_KeyInContract.m_pk);

        Env::VarReader r(key, key);
        uint32_t nKey = 0, nVal = sizeof(UserMax);
        if (!r.MoveNext(nullptr, nKey, this, nVal, 0))
        {
            _POD_(Cast::Down<User>(*this)).SetZero();
            return false;
        }

        Env::Halt_if(nVal < sizeof(User));
        m_Votes = (nVal - sizeof(User)) / sizeof(*m_pVotes);

        m_Charge +=
            Env::Cost::LoadVarPerByte * nVal +
            Env::Cost::Cycle * 50;

        if (m_iEpoch != s.m_Current.m_iEpoch)
        {
            if (m_iDividendEpoch)
            {
                if (m_Stake)
                {
                    m_Dividend.Load(cid, m_iDividendEpoch);

                    if (s.m_PrevDividend.m_iEpoch == m_iDividendEpoch)
                        m_Dividend.m_Stake = s.m_PrevDividend.m_Stake;

                    m_Charge +=
                        Env::Cost::LoadVar_For(sizeof(Dividend0) + sizeof(*m_Dividend.m_pArr) * m_Dividend.m_Assets) +
                        Env::Cost::Cycle * 50 * (m_Dividend.m_Assets + 1) +
                        Env::Cost::FundsLock * m_Dividend.m_Assets +
                        Env::Cost::SaveVar;

                    if (m_Stake != m_Dividend.m_Stake)
                    {
                        m_Charge +=
                            Env::Cost::Cycle * 500 * (m_Dividend.m_Assets + 1);

                        assert(m_Stake < m_Dividend.m_Stake);
                        MultiPrecision::Float k = MultiPrecision::Float(m_Stake) / MultiPrecision::Float(m_Dividend.m_Stake);

                        for (uint32_t i = 0; i < m_Dividend.m_Assets; i++)
                        {
                            auto& v = m_Dividend.m_pArr[i];
                            v.m_Amount = MultiPrecision::Float(v.m_Amount) * k;
                        }
                    }
                }

                m_iDividendEpoch = 0;
            }

            m_iEpoch = s.m_Current.m_iEpoch;
            m_Stake += m_StakeNext;
            m_StakeNext = 0;
            m_Votes = 0;
        }

        return true;
    }

    void PrepareTx(FundsChange* pFc) const
    {
        for (uint32_t i = 0; i < m_Dividend.m_Assets; i++)
        {
            auto& dst = pFc[i];
            const auto& src = m_Dividend.m_pArr[i];

            dst.m_Aid = src.m_Aid;
            dst.m_Amount = src.m_Amount;
            dst.m_Consume = 0;
        }
    }
};

ON_METHOD(user, view)
{
    MyState s;
    if (!s.Load(cid))
        return;
    s.MaybeNextEpoch();

    Env::DocGroup gr("res");

    MyUser u;
    if (!u.Load(cid, s))
        return;

    Env::DocAddNum("stake_active", u.m_Stake);
    Env::DocAddNum("stake_passive", u.m_StakeNext);

    if (u.m_Votes)
    {
        Env::DocArray gr1("current_votes");

        for (uint32_t i = 0; i < u.m_Votes; i++)
            Env::DocAddNum32("", u.m_pVotes[i]);
    }

    if (u.m_Dividend.m_Assets)
        u.m_Dividend.Write();
}

ON_METHOD(user, move_funds)
{
    MyState s;
    if (!s.Load(cid))
        return;
    s.MaybeNextEpoch();

    MyUser u;
    u.Load(cid, s);

    if (!amount && !u.m_Dividend.m_Assets)
        return OnError("no funds to move");

    Method::MoveFunds args;
    args.m_Amount = amount;
    args.m_Lock = !!bLock;

    UserKeyID kid(cid);
    kid.get_Pk(args.m_pkUser);

    auto* pFc = Env::StackAlloc_T<FundsChange>(u.m_Dividend.m_Assets + 1);
    u.PrepareTx(pFc + 1);

    pFc->m_Aid = s.m_Cfg.m_Aid;
    pFc->m_Amount = amount;
    pFc->m_Consume = args.m_Lock;

    Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, u.m_Dividend.m_Assets + 1, &kid, 1, "dao-vote move funds", 0);
}

ON_METHOD(user, vote)
{
    MyState s;
    if (!s.Load(cid))
        return;
    s.MaybeNextEpoch();

    MyUser u;
    u.Load(cid, s);

    if (!u.m_Stake)
        return OnError("no voting power");
    if (!s.m_Current.m_Proposals)
        return OnError("no current proposals");

#pragma pack (push, 1)
    struct Args
        :public Method::Vote
    {
        uint8_t m_pVote[Proposal::s_ProposalsPerEpochMax];
    };
#pragma pack (pop)

    Env::Halt_if(s.m_Current.m_Proposals > Proposal::s_ProposalsPerEpochMax);

    Args args;
    args.m_iEpoch = s.m_Current.m_iEpoch;

    UserKeyID kid(cid);
    kid.get_Pk(args.m_pkUser);

    static const char s_szPrefix[] = "vote_";
    char szBuf[_countof(s_szPrefix) + Utils::String::Decimal::Digits<Proposal::s_ProposalsPerEpochMax>::N];
    Env::Memcpy(szBuf, s_szPrefix, sizeof(s_szPrefix) - sizeof(char));

    for (uint32_t i = 0; i < s.m_Current.m_Proposals; i++)
    {
        uint32_t nPrintLen = Utils::String::Decimal::Print(szBuf + _countof(s_szPrefix) - 1, i + 1);
        szBuf[_countof(s_szPrefix) - 1 + nPrintLen] = 0;

        uint32_t nVote = 0;
        if (!Env::DocGet(szBuf, nVote))
            return OnError("vote missing");

        args.m_pVote[i] = (uint8_t) nVote;
    }

    auto* pFc = Env::StackAlloc_T<FundsChange>(u.m_Dividend.m_Assets);
    u.PrepareTx(pFc);

    Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(Method::Vote) + sizeof(*args.m_pVote) * s.m_Current.m_Proposals, pFc, u.m_Dividend.m_Assets, &kid, 1, "dao-vote Vote", 0);
}

#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
{
    Env::DocGroup root("");

    char szRole[0x20], szAction[0x20];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

    const char* szErr = nullptr;

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            DaoVote_##role##_##name(PAR_READ) \
            On_##role##_##name(DaoVote_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        DaoVoteRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    DaoVoteRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace DaoVote