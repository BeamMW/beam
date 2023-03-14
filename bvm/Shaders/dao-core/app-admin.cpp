#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable2/app_common_impl.h"

#define DaoCore_manager_deploy_version(macro)
#define DaoCore_manager_view(macro)
#define DaoCore_manager_view_params(macro) macro(ContractID, cid)
#define DaoCore_manager_my_admin_key(macro)
#define DaoCore_manager_deploy_contract(macro) Upgradable2_deploy(macro)
#define DaoCore_manager_schedule_upgrade(macro) Upgradable2_schedule_upgrade(macro)
#define DaoCore_manager_explicit_upgrade(macro) macro(ContractID, cid)
#define DaoCore_manager_replace_admin(macro) Upgradable2_replace_admin(macro)
#define DaoCore_manager_set_min_approvers(macro) Upgradable2_set_min_approvers(macro)

#define DaoCoreRole_manager(macro) \
    macro(manager, deploy_version) \
    macro(manager, view) \
    macro(manager, deploy_contract) \
    macro(manager, schedule_upgrade) \
    macro(manager, explicit_upgrade) \
    macro(manager, replace_admin) \
    macro(manager, set_min_approvers) \
    macro(manager, view_params) \
    macro(manager, my_admin_key) \

#define DaoCoreRoles_All(macro) \
    macro(manager)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  DaoCore_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); DaoCoreRole_##name(THE_METHOD) }
        
        DaoCoreRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(DaoCore_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr2-dao-core";

struct MyKeyID :public Env::KeyID {
    MyKeyID() :Env::KeyID(&g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

ON_METHOD(manager, view)
{
    static const ShaderID s_SidMigrate = {
        0x67,0x78,0xde,0x40,0xc6,0xac,0xb5,0xd8,0x05,0xf7,0x29,0x8b,0x77,0xf6,0x18,0xb3,0x32,0x6c,0x76,0x7b,0x81,0xe9,0x43,0x3a,0x81,0x81,0x27,0x5f,0x08,0x9e,0xb0,0xcc
    };

    static const ShaderID s_pSid[] = {
        DaoCore::s_SID,
        s_SidMigrate
    };

    ContractID pVerCid[_countof(s_pSid)];
    Height pVerDeploy[_countof(s_pSid)];

    ManagerUpgadable2::Walker wlk;
    wlk.m_VerInfo.m_Count = _countof(s_pSid);
    wlk.m_VerInfo.s_pSid = s_pSid;
    wlk.m_VerInfo.m_pCid = pVerCid;
    wlk.m_VerInfo.m_pHeight = pVerDeploy;

    MyKeyID kid;
    wlk.ViewAll(&kid);
}

ON_METHOD(manager, deploy_version)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy DaoCore bytecode", 0);
}

static const Amount g_DepositCA = 3000 * g_Beam2Groth; // 3K beams

ON_METHOD(manager, deploy_contract)
{
    FundsChange fc;
    fc.m_Aid = 0; // asset id
    fc.m_Amount = g_DepositCA; // amount of the input or output
    fc.m_Consume = 1; // contract consumes funds (i.e input, in this case)

    MyKeyID kid;
    PubKey pk;
    kid.get_Pk(pk);

    Upgradable2::Create arg;
    if (!ManagerUpgadable2::FillDeployArgs(arg, &pk))
        return;

    Env::GenerateKernel(nullptr, 0, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Deploy DaoCore contract", 0);
}

ON_METHOD(manager, schedule_upgrade)
{
    MyKeyID kid;
    ManagerUpgadable2::MultiSigRitual::Perform_ScheduleUpgrade(cid, kid, cidVersion, hTarget);
}

ON_METHOD(manager, explicit_upgrade)
{
    ManagerUpgadable2::MultiSigRitual::Perform_ExplicitUpgrade(cid, 800000);
}

ON_METHOD(manager, replace_admin)
{
    MyKeyID kid;
    ManagerUpgadable2::MultiSigRitual::Perform_ReplaceAdmin(cid, kid, iAdmin, pk);
}

ON_METHOD(manager, set_min_approvers)
{
    MyKeyID kid;
    ManagerUpgadable2::MultiSigRitual::Perform_SetApprovers(cid, kid, newVal);
}

AssetID get_TrgAid(const ContractID& cid)
{
    Env::Key_T<uint8_t> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_KeyInContract = 0;

    DaoCore::State s;
    if (!Env::VarReader::Read_T(key, s))
    {
        OnError("no such contract");
        return 0;
    }

    return s.m_Aid;
}

ON_METHOD(manager, view_params)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    Env::DocGroup gr("params");
    Env::DocAddNum("aid", aid);
    Env::DocAddNum("locked_beamX", WalkerFunds::FromContract_Lo(cid, aid));
    Env::DocAddNum("locked_beams", WalkerFunds::FromContract_Lo(cid, 0));
}

ON_METHOD(manager, my_admin_key)
{
    PubKey pk;
    MyKeyID kid;
    kid.get_Pk(pk);
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
            DaoCore_##role##_##name(PAR_READ) \
            On_##role##_##name(DaoCore_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        DaoCoreRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    DaoCoreRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

