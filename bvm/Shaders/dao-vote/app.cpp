#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable2/app_common_impl.h"

#define DaoVote_manager_view(macro)
#define DaoVote_manager_view_params(macro) macro(ContractID, cid)
#define DaoVote_manager_my_admin_key(macro)
#define DaoVote_manager_my_uid(macro) macro(ContractID, cid)
#define DaoVote_manager_explicit_upgrade(macro) macro(ContractID, cid)

#define DaoVoteRole_manager(macro) \
    macro(manager, view) \
    macro(manager, explicit_upgrade) \
    macro(manager, view_params) \
    macro(manager, my_uid) \
    macro(manager, my_admin_key)

#define DaoVoteRoles_All(macro) \
    macro(manager)

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
};

void WriteDividend(const ContractID& cid, uint32_t iDividendEpoch, const Amount* pStake)
{
    if (!iDividendEpoch)
        return;

    MyDividend d;
    d.Load(cid, iDividendEpoch);

    Env::DocArray gr("dividend");

    for (uint32_t i = 0; i < d.m_Assets; i++)
    {
        const auto& x = d.m_pArr[i];
        Env::DocGroup gr1("");

        Env::DocAddNum("aid", x.m_Aid);
        Env::DocAddNum("amount", x.m_Amount);
    }
}

ON_METHOD(manager, view_params)
{
    MyState s;
    if (!s.Load(cid))
        return;

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

        WriteDividend(cid, s.m_Current.m_iDividendEpoch, &s.m_Current.m_Stake);
    }
    {
        Env::DocGroup gr("next");
        Env::DocAddNum("proposals", s.m_Next.m_Proposals);

        WriteDividend(cid, s.m_Next.m_iDividendEpoch, nullptr);
    }

}

struct UserKeyID :public Env::KeyID {
    UserKeyID(const ContractID& cid)
    {
        m_pID = &cid;
        m_nID = sizeof(cid);
    }
};

ON_METHOD(manager, my_uid)
{
    PubKey pk;
    UserKeyID(cid).get_Pk(pk);
    Env::DocAddBlob_T("uid", pk);
}

ON_METHOD(manager, my_admin_key)
{
    PubKey pk;
    AdminKeyID().get_Pk(pk);
    Env::DocAddBlob_T("admin_key", pk);
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