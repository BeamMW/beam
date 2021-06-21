#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Voting_manager_create(macro)
#define Voting_manager_view(macro)
#define Voting_manager_destroy(macro) macro(ContractID, cid)
#define Voting_manager_proposals_view_all(macro) macro(ContractID, cid)

#define Voting_manager_proposal_view(macro) \
    macro(ContractID, cid) \
    macro(HashValue, pid)

#define Voting_manager_proposal_open(macro) \
    macro(ContractID, cid) \
    macro(HashValue, pid) \
    macro(Height, hMin) \
    macro(Height, hMax) \
    macro(AssetID, aid) \
    macro(uint32_t, num_variants)

#define VotingRole_manager(macro) \
    macro(manager, create) \
    macro(manager, destroy) \
    macro(manager, view) \
    macro(manager, proposals_view_all) \
    macro(manager, proposal_view) \
    macro(manager, proposal_open)

#define Voting_my_account_view_staking(macro) macro(ContractID, cid)

#define Voting_my_account_proposal_view(macro) \
    macro(ContractID, cid) \
    macro(HashValue, pid)

#define Voting_my_account_proposal_vote(macro) \
    macro(ContractID, cid) \
    macro(HashValue, pid) \
    macro(Amount, amount) \
    macro(uint32_t, variant)

#define Voting_my_account_proposal_withdraw(macro) \
    macro(ContractID, cid) \
    macro(HashValue, pid) \
    macro(Amount, amount)

#define VotingRole_my_account(macro) \
    macro(my_account, view_staking) \
    macro(my_account, proposal_view) \
    macro(my_account, proposal_vote) \
    macro(my_account, proposal_withdraw)

#define VotingRoles_All(macro) \
    macro(manager) \
    macro(my_account)

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Voting_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); VotingRole_##name(THE_METHOD) }
        
        VotingRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Voting_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}


ON_METHOD(manager, view)
{
    EnumAndDumpContracts(Voting::s_SID);
}

ON_METHOD(manager, create)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "create Voting contract", 0);
}

ON_METHOD(manager, destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, "destroy Voting contract", 0);
}

template <typename T>
void PrintDec(char* sz, T x)
{
    uint32_t nDigs = 1;
    for (T y = x; ; nDigs++)
        if (!(y /= 10))
            break;

    sz[nDigs] = 0;
    while (true)
    {
        sz[--nDigs] = '0' + (x % 10);
        if (!nDigs)
            break;

        x /= 10;
    }
}

typedef Env::Key_T<Voting::Proposal::ID> KeyProposal;
typedef Env::Key_T<Voting::UserKey> KeyUser;

struct ProposalWrap
{
    Height m_Height;
    ProposalWrap()
    {
        m_Height = Env::get_Height() + 1;
    }

    KeyProposal m_Key;
    Voting::Proposal_MaxVars m_Proposal;
    uint32_t m_Variants;

    Env::VarReaderEx<true> m_Reader;

    void EnumAll(const ContractID& cid)
    {
        KeyProposal k0, k1;
        _POD_(k0.m_Prefix.m_Cid) = cid;
        _POD_(k0.m_KeyInContract).SetZero();
        _POD_(k1.m_Prefix.m_Cid) = cid;
        _POD_(k1.m_KeyInContract).SetObject(0xff);
        m_Reader.Enum_T(k0, k1);
    }

    bool MoveNext()
    {
        while (true)
        {
            uint32_t nKey = sizeof(m_Key), nVal = sizeof(m_Proposal);
            if (!m_Reader.MoveNext(&m_Key, nKey, &m_Proposal, nVal, 0))
                break;
            if ((sizeof(m_Key) == nKey) && (nVal >= sizeof(Voting::Proposal)))
            {
                m_Variants = (nVal - sizeof(Voting::Proposal)) / sizeof(m_Proposal.m_pAmount[0]);
                return true;
            }
        }
        return false;
    }

    bool Read(const ContractID& cid, const Voting::Proposal::ID& pid)
    {
        KeyProposal k;
        _POD_(k.m_Prefix.m_Cid) = cid;
        _POD_(k.m_KeyInContract) = pid;
        m_Reader.Enum_T(k, k);

        if (MoveNext())
            return true;

        OnError("no such a proposal");
        return false;
    }

    bool IsStarted() const {
        return m_Height >= m_Proposal.m_Params.m_hMin;
    }
    bool IsFinished() const {
        return m_Height > m_Proposal.m_Params.m_hMax;
    }

    void Print() const
    {
        Env::DocAddNum("Variants", m_Variants);
        Env::DocAddNum("hMin", m_Proposal.m_Params.m_hMin);
        Env::DocAddNum("hMax", m_Proposal.m_Params.m_hMax);
        Env::DocAddNum("Aid", m_Proposal.m_Params.m_Aid);
        Env::DocAddText("Status", IsFinished() ? "finished" : IsStarted() ? "in_progress" : "published");

        Env::DocGroup grVotes("votes");
        for (uint32_t i = 0; i < m_Variants; i++)
        {
            Amount val = m_Proposal.m_pAmount[i];
            if (val)
            {
                char sz[10];
                PrintDec(sz, i);
                Env::DocAddNum(sz, val);
            }
        }
    }
};

ON_METHOD(manager, proposals_view_all)
{
    Env::DocArray gr("proposals");
    
    ProposalWrap pw;
    pw.EnumAll(cid);
    while (true)
    {
        if (!pw.MoveNext())
            break;

        Env::DocGroup gr("");
    
        Env::DocAddBlob_T("ID", pw.m_Key.m_KeyInContract);
        pw.Print();
    }
}

ON_METHOD(manager, proposal_view)
{
    ProposalWrap pw;
    if (!pw.Read(cid, pid))
        return;

    pw.Print();

    Env::DocArray gr("funds_locked");

    KeyUser k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_ID) = pid;
    _POD_(k0.m_KeyInContract.m_Pk).SetZero();
    _POD_(k1) = k0;
    _POD_(k1.m_KeyInContract.m_Pk).SetObject(0xff);
    Env::VarReader r(k0, k1);

    while (true)
    {
        KeyUser key;
        Amount val;
        if (!r.MoveNext_T(key, val))
            break;

        Env::DocGroup gr("");

        Env::DocAddBlob_T("Pk", key.m_KeyInContract.m_Pk);
        Env::DocAddNum("Amount", val);
    }
}

ON_METHOD(manager, proposal_open)
{
    Voting::OpenProposal arg;
    arg.m_Params.m_hMin = hMin;
    arg.m_Params.m_hMax = hMax;
    arg.m_Params.m_Aid = aid;
    arg.m_Variants = num_variants;
    _POD_(arg.m_ID) = pid;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "open proposal", 0);
}

#pragma pack (push, 1)
struct MyPkMaterial
{
    ContractID m_Cid;
    Voting::Proposal::ID m_Pid;

    void Set(const Voting::Proposal::ID& pid, const ContractID& cid)
    {
        _POD_(m_Cid) = cid;
        _POD_(m_Pid) = pid;
    }

    void Set(const KeyUser& uk)
    {
        Set(uk.m_KeyInContract.m_ID, uk.m_Prefix.m_Cid);
    }

    void Get(PubKey& pk)
    {
        Env::DerivePk(pk, this, sizeof(*this));
    }

    static void SetGet(KeyUser& uk)
    {
        MyPkMaterial x;
        x.Set(uk);
        x.Get(uk.m_KeyInContract.m_Pk);
    }
};
#pragma pack (pop)

ON_METHOD(my_account, view_staking)
{
    Amount totalLocked = 0, totalAvail = 0;

    KeyUser uk;
    _POD_(uk.m_Prefix.m_Cid) = cid;

    {
        Env::DocArray gr0("pids");

        ProposalWrap pw;
        pw.EnumAll(cid);
        while (true)
        {
            if (!pw.MoveNext())
                break;

            if (!pw.IsStarted())
                continue;

            bool bFinished = pw.IsFinished();

            _POD_(uk.m_KeyInContract.m_ID) = pw.m_Key.m_KeyInContract;
            MyPkMaterial::SetGet(uk);

            Amount amount;
            if (Env::VarReader::Read_T(uk, amount))
            {
                Env::DocGroup gr("");
                Env::DocAddBlob_T("pid", uk.m_KeyInContract.m_ID);
                Env::DocAddNum("Amount", amount);

                Env::DocAddText("Status", bFinished ? "available" : "locked");

                (bFinished ? totalAvail : totalLocked) += amount;
            }
        }
    }

    Env::DocAddNum("total_locked", totalLocked);
    Env::DocAddNum("total_available", totalAvail);

}

ON_METHOD(my_account, proposal_view)
{
    bool bIsFinished;
    {
        ProposalWrap pw;
        if (!pw.Read(cid, pid))
            return;

        pw.Print();
        bIsFinished = pw.IsFinished();
    }

    KeyUser uk;
    _POD_(uk.m_Prefix.m_Cid) = cid;
    _POD_(uk.m_KeyInContract.m_ID) = pid;
    MyPkMaterial::SetGet(uk);

    Amount amount;
    if (Env::VarReader::Read_T(uk, amount))
    {
        Env::DocAddNum("My_Amount", amount);
        Env::DocAddText("Status", bIsFinished ? "available" : "locked");
    }
}

void VoteOrWithdraw(const ContractID& cid, const Voting::Proposal::ID& pid, Amount amount, const uint32_t* pVote)
{
    FundsChange fc;
    fc.m_Amount = amount;

    {
        ProposalWrap pw;
        if (!pw.Read(cid, pid))
            return;

        fc.m_Aid = pw.m_Proposal.m_Params.m_Aid;
    }

    Voting::Vote arg;
    arg.m_Amount = amount;
    _POD_(arg.m_ID) = pid;

    MyPkMaterial pkMat;
    pkMat.Set(pid, cid);
    pkMat.Get(arg.m_Pk);

    if (pVote)
    {
        arg.m_Variant = *pVote;
        fc.m_Consume = 1;

        Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "cast the vote", 0);
    }
    else
    {
        fc.m_Consume = 0;

        SigRequest sig;
        sig.m_pID = &pkMat;
        sig.m_nID = sizeof(pkMat);

        auto& arg_ = Cast::Down<Voting::UserRequest>(arg);
        static_assert(sizeof(arg_) == sizeof(Voting::Withdraw));

        Env::GenerateKernel(&cid, Voting::Withdraw::s_iMethod, &arg_, sizeof(arg_), &fc, 1, &sig, 1, "Withdraw after vote", 0);
    }
}

ON_METHOD(my_account, proposal_vote)
{
    VoteOrWithdraw(cid, pid, amount, &variant);
}

ON_METHOD(my_account, proposal_withdraw)
{
    VoteOrWithdraw(cid, pid, amount, nullptr);
}


#undef ON_METHOD
#undef THE_FIELD

export void Method_1()
{
    Env::DocGroup root("");

    char szRole[0x20], szAction[0x20];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Voting_##role##_##name(PAR_READ) \
            On_##role##_##name(Voting_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        VotingRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    VotingRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

