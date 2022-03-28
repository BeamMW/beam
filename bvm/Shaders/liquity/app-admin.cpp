#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable2/app_common_impl.h"

#define Liquity_manager_deploy_version(macro)
#define Liquity_manager_view(macro)
#define Liquity_manager_my_admin_key(macro)
#define Liquity_manager_deploy_contract(macro) \
    Upgradable2_deploy(macro) \
    macro(ContractID, cidOracle) \
    macro(Amount, troveLiquidationReserve) \
    macro(AssetID, aidProfit) \
    macro(uint32_t, hInitialPeriod)

#define Liquity_manager_schedule_upgrade(macro) Upgradable2_schedule_upgrade(macro)
#define Liquity_manager_explicit_upgrade(macro) macro(ContractID, cid)
#define Liquity_manager_replace_admin(macro) Upgradable2_replace_admin(macro)
#define Liquity_manager_set_min_approvers(macro) Upgradable2_set_min_approvers(macro)

#define LiquityRole_manager(macro) \
    macro(manager, deploy_version) \
    macro(manager, view) \
    macro(manager, deploy_contract) \
    macro(manager, schedule_upgrade) \
    macro(manager, explicit_upgrade) \
    macro(manager, replace_admin) \
    macro(manager, set_min_approvers) \
    macro(manager, my_admin_key) \

#define LiquityRoles_All(macro) \
    macro(manager)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Liquity_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); LiquityRole_##name(THE_METHOD) }
        
        LiquityRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Liquity_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr2-liquity";

struct MyKeyID :public Env::KeyID {
    MyKeyID() :Env::KeyID(&g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

ON_METHOD(manager, view)
{
    static const ShaderID s_pSid[] = {
        Liquity::s_SID,
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
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy Liquity bytecode", 0);
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

#pragma pack (push, 1)
    struct Arg :public Upgradable2::Create {
        Liquity::Method::Create m_Inner;
    } arg;
#pragma pack (pop)

    if (!ManagerUpgadable2::FillDeployArgs(arg, &pk))
        return;

    if (!troveLiquidationReserve)
        return OnError("trove Liquidation Reserve should not be zero");

    auto& s = arg.m_Inner.m_Settings; // alias
    s.m_AidProfit = aidProfit;
    s.m_TroveLiquidationReserve = troveLiquidationReserve;
    _POD_(s.m_cidOracle) = cidOracle;
    s.m_hMinRedemptionHeight = Env::get_Height() + hInitialPeriod;

    const uint32_t nCharge =
        ManagerUpgadable2::get_ChargeDeploy() +
        Env::Cost::AssetManage +
        Env::Cost::Refs +
        Env::Cost::SaveVar_For(sizeof(Liquity::Global)) +
        Env::Cost::Cycle * 300;

    Env::GenerateKernel(nullptr, 0, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Deploy Liquity contract", nCharge);
}

ON_METHOD(manager, schedule_upgrade)
{
    MyKeyID kid;
    ManagerUpgadable2::MultiSigRitual::Perform_ScheduleUpgrade(cid, kid, cidVersion, hTarget);
}

ON_METHOD(manager, explicit_upgrade)
{
    ManagerUpgadable2::MultiSigRitual::Perform_ExplicitUpgrade(cid);
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

Amount get_ContractLocked(AssetID aid, const ContractID& cid)
{
    Env::Key_T<AssetID> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_Prefix.m_Tag = KeyTag::LockedAmount;
    key.m_KeyInContract = Utils::FromBE(aid);

    struct AmountBig {
        Amount m_Hi;
        Amount m_Lo;
    };

    AmountBig val;
    if (!Env::VarReader::Read_T(key, val))
        return 0;

    return Utils::FromBE(val.m_Lo);
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
            Liquity_##role##_##name(PAR_READ) \
            On_##role##_##name(Liquity_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        LiquityRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    LiquityRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

