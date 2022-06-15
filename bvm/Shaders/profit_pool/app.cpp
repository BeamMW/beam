#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable3/app_common_impl.h"

#define ProfitPool_manager_deploy(macro) \
    Upgradable3_deploy(macro) \
    macro(AssetID, aidStaking)

#define ProfitPool_manager_view(macro)
#define ProfitPool_manager_view_params(macro) macro(ContractID, cid)
#define ProfitPool_manager_my_admin_key(macro)

#define ProfitPool_manager_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define ProfitPool_manager_replace_admin(macro) Upgradable3_replace_admin(macro)
#define ProfitPool_manager_set_min_approvers(macro) Upgradable3_set_min_approvers(macro)
#define ProfitPool_manager_explicit_upgrade(macro) macro(ContractID, cid)

#define ProfitPoolRole_manager(macro) \
    macro(manager, view) \
    macro(manager, deploy) \
    macro(manager, schedule_upgrade) \
    macro(manager, explicit_upgrade) \
    macro(manager, replace_admin) \
    macro(manager, set_min_approvers) \
    macro(manager, view_params) \
    macro(manager, my_admin_key)

#define ProfitPool_user_my_key(macro) macro(ContractID, cid)
#define ProfitPool_user_view(macro) macro(ContractID, cid)

#define ProfitPool_user_update(macro) \
    macro(ContractID, cid) \
    macro(Amount, newStake)

#define ProfitPoolRole_user(macro) \
    macro(user, my_key) \
    macro(user, view) \
    macro(user, update)

#define ProfitPoolRoles_All(macro) \
    macro(manager) \
    macro(user)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  ProfitPool_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); ProfitPoolRole_##name(THE_METHOD) }
        
        ProfitPoolRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(ProfitPool_##role##_##name(THE_FIELD) int unused = 0)

namespace ProfitPool {

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr3-dao-vault";

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(&g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

struct UserKeyID :public Env::KeyID {
    UserKeyID(const ContractID& cid)
    {
        m_pID = &cid;
        m_nID = sizeof(cid);
    }
};

const Upgradable3::Manager::VerInfo g_VerInfo = { s_pSID, _countof(s_pSID) };

ON_METHOD(manager, view)
{
    AdminKeyID kid;
    g_VerInfo.DumpAll(&kid);
}

ON_METHOD(manager, deploy)
{
    Method::Create args;
    args.m_aidStaking = aidStaking;

    PubKey pkMy;
    AdminKeyID().get_Pk(pkMy);

    if (!g_VerInfo.FillDeployArgs(args.m_Upgradable, &pkMy))
        return;

    const uint32_t nCharge =
        Upgradable3::Manager::get_ChargeDeploy() +
        Env::Cost::SaveVar_For(sizeof(ProfitPool::Pool0)) +
        Env::Cost::Cycle * 50;

    Env::GenerateKernel(nullptr, 0, &args, sizeof(args), nullptr, 0, nullptr, 0, "Deploy ProfitPool contract", nCharge);
}

ON_METHOD(manager, schedule_upgrade)
{
    AdminKeyID kid;
    g_VerInfo.ScheduleUpgrade(cid, kid, hTarget);
}

ON_METHOD(manager, explicit_upgrade)
{
    Upgradable3::Manager::MultiSigRitual::Perform_ExplicitUpgrade(cid);
}

ON_METHOD(manager, replace_admin)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_ReplaceAdmin(cid, kid, iAdmin, pk);
}

ON_METHOD(manager, set_min_approvers)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_SetApprovers(cid, kid, newVal);
}


struct MyPool
    :public PoolMaxPlus
{
    bool Load(const ContractID& cid)
    {
        Env::Key_T<uint8_t> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        key.m_KeyInContract = Tags::s_Pool;

        Env::VarReader r(key, key);
        uint32_t nKey = 0, nVal = sizeof(PoolMax);
        if (!r.MoveNext(nullptr, nKey, this, nVal, 0))
        {
            OnError("no state");
            return false;
        }

        m_Assets = (nVal - sizeof(Pool0)) / sizeof(PerAsset);
        return true;
    }
};

void WriteAidAmount(AssetID aid, Amount amount)
{
    Env::DocAddNum("aid", aid);
    Env::DocAddNum("amount", amount);
}

ON_METHOD(manager, view_params)
{
    MyPool p;
    if (!p.Load(cid))
        return;

    Env::DocGroup gr("params");
    Env::DocAddNum("aidStake", p.m_aidStaking);
    Env::DocAddNum("amountStake", p.m_Weight);

    {
        Env::DocArray gr1("assets");

        for (uint32_t i = 0; i < p.m_Assets; i++)
        {
            const auto& x = p.m_p[i];
            if (!x.m_Amount)
                continue;

            Env::DocGroup gr2("");
            WriteAidAmount(x.m_Aid, x.m_Amount);
        }
    }
}

ON_METHOD(manager, my_admin_key)
{
    PubKey pk;
    AdminKeyID().get_Pk(pk);
    Env::DocAddBlob_T("admin_key", pk);
}

ON_METHOD(user, my_key)
{
    PubKey pk;
    UserKeyID(cid).get_Pk(pk);
    Env::DocAddBlob_T("key", pk);
}

struct MyUser
    :public UserMax
{
    Env::Key_T<User0::Key> m_Key; // contains user pk, good to save

    bool Load(const ContractID& cid, MyPool& p)
    {
        _POD_(m_Key.m_Prefix.m_Cid) = cid;
        UserKeyID(cid).get_Pk(m_Key.m_KeyInContract.m_pk);

        Env::VarReader r(m_Key, m_Key);
        uint32_t nKey = 0, nVal = sizeof(UserMax);
        if (!r.MoveNext(nullptr, nKey, this, nVal, 0))
            return false;

        uint32_t nAssets = (nVal - sizeof(User0)) / sizeof(PerAsset);
        Remove(p, nAssets);

        return true;
    }
};

ON_METHOD(user, view)
{
    MyPool p;
    if (!p.Load(cid))
        return;

    Env::DocGroup gr("res");

    MyUser u;
    if (!u.Load(cid, p))
        return;

    Env::DocAddNum("stake", u.m_Weight);

    {
        Env::DocArray gr1("avail");

        for (uint32_t i = 0; i < p.m_Assets; i++)
        {
            auto val = u.m_p[i].m_Value;
            if (!val)
                continue;

            Env::DocGroup gr2("");
            WriteAidAmount(p.m_p[i].m_Aid, val);
        }
    }
}

ON_METHOD(user, update)
{
    MyPool p;
    if (!p.Load(cid))
        return;

    Env::DocGroup gr("res");

    MyUser u;
    if (!u.Load(cid, p))
        _POD_(Cast::Down<UserMax>(u)).SetZero();

    FundsChange pFc[Pool0::s_AssetsMax + 1];
    uint32_t nFc = 0;
    for (uint32_t i = 0; i < p.m_Assets; i++)
    {
        auto val = u.m_p[i].m_Value;
        if (val)
        {
            pFc[nFc].m_Amount = val;
            pFc[nFc].m_Aid = p.m_p[i].m_Aid;
            pFc[nFc].m_Consume = 0;
            nFc++;
        }
    }

    if (newStake != u.m_Weight)
    {
        if (newStake > u.m_Weight)
        {
            pFc[nFc].m_Amount = newStake - u.m_Weight;
            pFc[nFc].m_Consume = 1;
        }
        else
        {
            pFc[nFc].m_Amount = u.m_Weight - newStake;
            pFc[nFc].m_Consume = 0;
        }
        nFc++;
    }

    if (!nFc)
        return OnError("no change");

    Method::UserUpdate arg;
    _POD_(arg.m_pkUser) = u.m_Key.m_KeyInContract.m_pk;
    arg.m_NewStaking = newStake;
    arg.m_WithdrawCount = static_cast<uint32_t>(-1);

    const uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::LoadVar_For(sizeof(Pool0) + p.m_Assets * sizeof(Pool0::PerAsset)) +
        Env::Cost::SaveVar_For(sizeof(Pool0) + p.m_Assets * sizeof(Pool0::PerAsset)) +
        Env::Cost::LoadVar_For(sizeof(User0) + p.m_Assets * sizeof(User0::PerAsset)) +
        Env::Cost::SaveVar_For(sizeof(User0) + p.m_Assets * sizeof(User0::PerAsset)) +
        Env::Cost::AddSig +
        Env::Cost::FundsLock * nFc +
        Env::Cost::Cycle * 1000 * p.m_Assets +
        Env::Cost::Cycle * 500;

    UserKeyID kid(cid);

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, nFc, &kid, 1, "ProfitPool user update", nCharge);
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
            ProfitPool_##role##_##name(PAR_READ) \
            On_##role##_##name(ProfitPool_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        ProfitPoolRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    ProfitPoolRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace ProfitPool