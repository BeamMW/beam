#include "../app_common_impl.h"
#include "test.h"

#define UpgrTest_manager_deploy_version(macro)
#define UpgrTest_manager_view(macro)
#define UpgrTest_manager_view_params(macro) macro(ContractID, cid)
#define UpgrTest_manager_my_admin_key(macro)
#define UpgrTest_manager_deploy_contract(macro) Upgradable2_deploy(macro)

#define UpgrTest_manager_schedule_upgrade(macro) Upgradable2_schedule_upgrade(macro)
#define UpgrTest_manager_explicit_upgrade(macro) macro(ContractID, cid)

#define UpgrTestRole_manager(macro) \
    macro(manager, deploy_version) \
    macro(manager, view) \
    macro(manager, deploy_contract) \
    macro(manager, schedule_upgrade) \
    macro(manager, explicit_upgrade) \
    macro(manager, view_params) \
    macro(manager, my_admin_key)

#define UpgrTestRoles_All(macro) \
    macro(manager)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  UpgrTest_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); UpgrTestRole_##name(THE_METHOD) }
        
        UpgrTestRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(UpgrTest_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "test-11433";

struct MyKeyID :public Env::KeyID {
    MyKeyID() :Env::KeyID(&g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

ON_METHOD(manager, view)
{
    static const ShaderID s_pSid[] = {
        Upgradable2::Test::s_SID_0,
        Upgradable2::Test::s_SID_1 // latest version
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
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy test bytecode", 0);
}

static const Amount g_DepositCA = 3000 * g_Beam2Groth; // 3K beams


ON_METHOD(manager, deploy_contract)
{
    MyKeyID kid;
    PubKey pk;
    kid.get_Pk(pk);

    Upgradable2::Create arg;
    if (!ManagerUpgadable2::FillDeployArgs(arg, &pk))
        return;

    Env::GenerateKernel(nullptr, 0, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Deploy testo contract",
        ManagerUpgadable2::get_ChargeDeploy() +
        Env::Cost::SaveVar_For(sizeof(Upgradable2::Test::State)));
}

ON_METHOD(manager, schedule_upgrade)
{
    MyKeyID kid;
    ManagerUpgadable2::MultiSigRitual::Perform_ScheduleUpgrade(cid, kid, cidVersion, hTarget);
}

ON_METHOD(manager, explicit_upgrade)
{
    const uint32_t nChargeExtra =
        Env::Cost::LoadVar_For(sizeof(Upgradable2::Settings)) +
        Env::Cost::LoadVar_For(sizeof(Upgradable2::State)) +
        Env::Cost::SaveVar * 2 + // delete vars of Upgradable2
        Env::Cost::SaveVar_For(sizeof(Upgradable2::State)) +
        Env::Cost::UpdateShader_For(2000);

    ManagerUpgadable2::MultiSigRitual::Perform_ExplicitUpgrade(cid, nChargeExtra);
}

ON_METHOD(manager, view_params)
{
    Env::Key_T<uint8_t> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_KeyInContract = Upgradable2::Test::State::s_Key;

    Upgradable2::Test::State s;
    if (Env::VarReader::Read_T(key, s))
        Env::DocAddNum("iVer", s.m_iVer);
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
            UpgrTest_##role##_##name(PAR_READ) \
            On_##role##_##name(UpgrTest_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        UpgrTestRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    UpgrTestRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

