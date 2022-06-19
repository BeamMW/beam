#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable3/app_common_impl.h"

#define DaoVault_manager_deploy(macro) Upgradable3_deploy(macro)
#define DaoVault_manager_view(macro)
#define DaoVault_manager_view_funds(macro) macro(ContractID, cid)
#define DaoVault_manager_my_admin_key(macro)

#define DaoVault_manager_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define DaoVault_manager_replace_admin(macro) Upgradable3_replace_admin(macro)
#define DaoVault_manager_set_min_approvers(macro) Upgradable3_set_min_approvers(macro)
#define DaoVault_manager_explicit_upgrade(macro) macro(ContractID, cid)

#define DaoVault_manager_withdraw(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define DaoVaultRole_manager(macro) \
    macro(manager, view) \
    macro(manager, deploy) \
    macro(manager, schedule_upgrade) \
    macro(manager, explicit_upgrade) \
    macro(manager, replace_admin) \
    macro(manager, set_min_approvers) \
    macro(manager, view_funds) \
    macro(manager, my_admin_key) \
    macro(manager, withdraw)

#define DaoVaultRoles_All(macro) \
    macro(manager)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  DaoVault_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); DaoVaultRole_##name(THE_METHOD) }
        
        DaoVaultRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(DaoVault_##role##_##name(THE_FIELD) int unused = 0)

namespace DaoVault {

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
    PubKey pkMy;
    AdminKeyID().get_Pk(pkMy);

    if (!g_VerInfo.FillDeployArgs(args.m_Upgradable, &pkMy))
        return;

    const uint32_t nCharge =
        Upgradable3::Manager::get_ChargeDeploy() +
        Env::Cost::Cycle * 50;

    Env::GenerateKernel(nullptr, 0, &args, sizeof(args), nullptr, 0, nullptr, 0, "Deploy DaoVault contract", nCharge);
}

ON_METHOD(manager, withdraw)
{
    if (!amount)
        return OnError("amount not specified");

    Method::Withdraw arg;
    arg.m_Aid = aid;
    arg.m_Amount = amount;

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Consume = 0;

    Upgradable3::Manager::MultiSigRitual msp;
    msp.m_szComment = "DaoVault withdraw";
    msp.m_iMethod = arg.s_iMethod;
    msp.m_nArg = sizeof(arg);
    msp.m_pCid = &cid;
    msp.m_Kid = AdminKeyID();
    msp.m_Charge += Env::Cost::FundsLock;
    msp.m_pFc = &fc;
    msp.m_nFc = 1;

    msp.Perform(arg);
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

ON_METHOD(manager, view_funds)
{
    Env::DocArray gr("res");

    WalkerFunds wlk;
    for (wlk.Enum(cid); wlk.MoveNext(); )
    {
        Env::DocGroup gr1("");

        Env::DocAddNum("aid", wlk.m_Aid);
        Env::DocAddNum("amount", wlk.m_Val.m_Lo);
    }
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
            DaoVault_##role##_##name(PAR_READ) \
            On_##role##_##name(DaoVault_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        DaoVaultRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    DaoVaultRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace DaoVault