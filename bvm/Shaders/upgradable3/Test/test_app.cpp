#include "../app_common_impl.h"
#include "test.h"

#define UpgrTest_manager_view(macro)
#define UpgrTest_manager_my_admin_key(macro)
#define UpgrTest_manager_deploy(macro) Upgradable3_deploy(macro)

#define UpgrTest_manager_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define UpgrTest_manager_explicit_upgrade(macro) macro(ContractID, cid)

#define UpgrTestRole_manager(macro) \
    macro(manager, view) \
    macro(manager, deploy) \
    macro(manager, schedule_upgrade) \
    macro(manager, explicit_upgrade) \
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

const ShaderID g_pSid[] = {
    Upgradable3::Test::s_SID_0,
    Upgradable3::Test::s_SID_1
};

const Upgradable3::Manager::VerInfo g_VerInfo = { g_pSid, _countof(g_pSid) };


ON_METHOD(manager, view)
{
    MyKeyID kid;
    g_VerInfo.DumpAll(&kid);
}

ON_METHOD(manager, deploy)
{
    MyKeyID kid;
    PubKey pk;
    kid.get_Pk(pk);

    Upgradable3::Test::Method::Create arg;
    if (!g_VerInfo.FillDeployArgs(arg.m_Stgs, &pk))
        return;

    Env::GenerateKernel(nullptr, 0, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Deploy test contract", Upgradable3::Manager::get_ChargeDeploy());
}

ON_METHOD(manager, schedule_upgrade)
{
    MyKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_ScheduleUpgrade(g_VerInfo, cid, kid, hTarget);
}

ON_METHOD(manager, explicit_upgrade)
{
    MyKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_ExplicitUpgrade(cid);
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

