#include "../common.h"
#include "../app_common_impl.h"
#include "../app_comm.h"
#include "../upgradable3/app_common_impl.h"
#include "contract.h"

#define Oracle2_settings(macro) \
    macro(Height, hValidity) \
    macro(uint32_t, nMinProviders)

#define Oracle2_manager_deploy(macro) \
    Upgradable3_deploy(macro) \
    Oracle2_settings(macro)

#define Oracle2_manager_view(macro)
#define Oracle2_manager_upd_settings(macro) macro(ContractID, cid) Oracle2_settings(macro)
#define Oracle2_manager_view_params(macro) macro(ContractID, cid)
#define Oracle2_manager_view_median(macro) macro(ContractID, cid)
#define Oracle2_manager_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define Oracle2_manager_replace_admin(macro) Upgradable3_replace_admin(macro)
#define Oracle2_manager_set_min_approvers(macro) Upgradable3_set_min_approvers(macro)
#define Oracle2_manager_explicit_upgrade(macro) macro(ContractID, cid)

#define Oracle2_manager_prov_add(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pk)

#define Oracle2_manager_prov_del(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, iProv)

#define Oracle2Role_manager(macro) \
    macro(manager, deploy) \
    macro(manager, view) \
    macro(manager, schedule_upgrade) \
    macro(manager, explicit_upgrade) \
    macro(manager, replace_admin) \
    macro(manager, set_min_approvers) \
    macro(manager, upd_settings) \
    macro(manager, prov_add) \
    macro(manager, prov_del) \
    macro(manager, view_params) \
    macro(manager, view_median)

#define Oracle2_provider_get_key(macro)
#define Oracle2_provider_set(macro) \
    macro(ContractID, cid) \
    macro(uint64_t, val_n)

#define Oracle2Role_provider(macro) \
    macro(provider, get_key) \
    macro(provider, set)

#define Oracle2Roles_All(macro) \
    macro(manager) \
    macro(provider)

namespace Oracle2
{

    MultiPrecision::Float get_Norm_n()
    {
        return MultiPrecision::Float(1e9);
    }

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Oracle2_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); Oracle2Role_##name(THE_METHOD) }
        
        Oracle2Roles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Oracle2_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szKeyMaterial[] = "oracle2-prov";

struct ProviderKeyID
    :public Env::KeyID
{
    ProviderKeyID() :Env::KeyID(g_szKeyMaterial, sizeof(g_szKeyMaterial)) {}
};

const char g_szAdminSeed[] = "upgr3-oracle2";

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

const Upgradable3::Manager::VerInfo g_VerInfo = { s_pSID, _countof(s_pSID) };

ON_METHOD(manager, view)
{
    AdminKeyID kid;
    g_VerInfo.DumpAll(&kid);
}

ON_METHOD(manager, deploy)
{
    AdminKeyID kid;
    PubKey pk;
    kid.get_Pk(pk);

    Oracle2::Method::Create arg;

    if (!g_VerInfo.FillDeployArgs(arg.m_Upgradable, &pk))
        return;

    arg.m_Settings.m_hValidity = hValidity;
    arg.m_Settings.m_MinProviders = nMinProviders;

    const uint32_t nCharge =
        Upgradable3::Manager::get_ChargeDeploy() +
        Env::Cost::SaveVar_For(sizeof(State0)) +
        Env::Cost::SaveVar_For(sizeof(Median)) +
        Env::Cost::Cycle * 300;

    Env::GenerateKernel(nullptr, 0, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Deploy Oracle2 contract", nCharge);
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

struct MyState
    :public StateMax
{
    uint32_t m_Provs;
    uint32_t m_iProv;

    bool IsProvider() const {
        return m_iProv < m_Provs;
    }

    bool ReadInternal(const ContractID& cid)
    {
        Env::Key_T<uint8_t> k;
        k.m_Prefix.m_Cid = cid;
        k.m_KeyInContract = Tags::s_StateFull;

        Env::VarReader r(k, k);

        uint32_t nKey = sizeof(k), nVal = sizeof(StateMax);
        if (!r.MoveNext(&k, nKey, this, nVal, KeyTag::Internal))
            return false;

        m_Provs = (nVal - sizeof(State0)) / sizeof(Entry);
        if (m_Provs > s_ProvsMax)
            return false;

        ProviderKeyID kid;
        PubKey pk;
        kid.get_Pk(pk);

        for (m_iProv = 0; m_iProv < m_Provs; m_iProv++)
            if (_POD_(pk) == m_pE[m_iProv].m_Pk)
                break;

        return true;
    }

    bool Read(const ContractID& cid)
    {
        if (ReadInternal(cid))
            return true;

        OnError("state not found");
        return false;
    }

    uint32_t get_ChargeUpd_NoCall() const
    {
        uint32_t nSize = sizeof(State0) + sizeof(Entry) * m_Provs;

        return
            Env::Cost::CallFar +
            Env::Cost::LoadVar_For(nSize + sizeof(Entry)) +
            Env::Cost::SaveVar_For(nSize) +
            Env::Cost::SaveVar_For(sizeof(Median)) +
            Env::Cost::Cycle * (1000 + 1000 * m_Provs);
    }

    uint32_t get_ChargeUpd() const
    {
        return
            Env::Cost::CallFar +
            get_ChargeUpd_NoCall();
    }
};

ON_METHOD(manager, upd_settings)
{
    MyState s;
    if (!s.Read(cid))
        return;

    Oracle2::Method::SetSettings arg;
    arg.m_Settings.m_hValidity = hValidity;
    arg.m_Settings.m_MinProviders = nMinProviders;

    Upgradable3::Manager::MultiSigRitual msp;
    msp.m_szComment = "Oracle2 update settings";
    msp.m_iMethod = arg.s_iMethod;
    msp.m_nArg = sizeof(arg);
    msp.m_pCid = &cid;
    msp.m_Kid = AdminKeyID();
    msp.m_Charge += s.get_ChargeUpd_NoCall();
    msp.Perform(arg);
}

ON_METHOD(manager, prov_add)
{
    MyState s;
    if (!s.Read(cid))
        return;

    Oracle2::Method::ProviderAdd arg;
    _POD_(arg.m_pk) = pk;

    Upgradable3::Manager::MultiSigRitual msp;
    msp.m_szComment = "Oracle2 provider add";
    msp.m_iMethod = arg.s_iMethod;
    msp.m_nArg = sizeof(arg);
    msp.m_pCid = &cid;
    msp.m_Kid = AdminKeyID();
    msp.m_Charge += s.get_ChargeUpd_NoCall();
    msp.Perform(arg);
}

ON_METHOD(manager, prov_del)
{
    MyState s;
    if (!s.Read(cid))
        return;

    Oracle2::Method::ProviderDel arg;
    arg.m_iProvider = iProv;

    Upgradable3::Manager::MultiSigRitual msp;
    msp.m_szComment = "Oracle2 provider del";
    msp.m_iMethod = arg.s_iMethod;
    msp.m_nArg = sizeof(arg);
    msp.m_pCid = &cid;
    msp.m_Kid = AdminKeyID();
    msp.m_Charge += s.get_ChargeUpd_NoCall();
    msp.Perform(arg);
}

ON_METHOD(manager, view_params)
{
    MyState s;
    if (!s.Read(cid))
        return;

    Env::DocGroup gr("params");
    if (s.IsProvider())
        Env::DocAddNum("iProv", s.m_iProv);

    Env::DocArray gr1("provs");

    for (uint32_t iProv = 0; iProv < s.m_Provs; iProv++)
    {
        Env::DocGroup gr2("");

        const auto& x = s.m_pE[iProv];
        Env::DocAddBlob_T("pk", x.m_Pk);
        Env::DocAddNum("val", x.m_Val * get_Norm_n());
        Env::DocAddNum("hUpd", x.m_hUpdated);
    }
}

ON_METHOD(manager, view_median)
{
    Env::Key_T<uint8_t> k;
    _POD_(k.m_Prefix.m_Cid) = cid;
    k.m_KeyInContract = Tags::s_Median;

    Median med;
    if (Env::VarReader::Read_T(k, med))
    {
        Env::DocArray gr("res");
        Env::DocAddNum("val", med.m_Res * get_Norm_n());
        Env::DocAddNum("hEnd", med.m_hEnd);
    }
    else
        OnError("not found");
}


ON_METHOD(provider, get_key)
{
    ProviderKeyID kid;
    PubKey pk;
    kid.get_Pk(pk);

    Env::DocArray gr("res");
    Env::DocAddBlob_T("pk", pk);
}

ON_METHOD(provider, set)
{
    if (!val_n)
        return OnError("val can't be zero");

    MyState s;
    if (!s.Read(cid))
        return;

    if (!s.IsProvider())
        return OnError("not a provider");

    Method::FeedData arg;
    arg.m_iProvider = s.m_iProv;
    arg.m_Value = MultiPrecision::Float(val_n) / get_Norm_n();

    const uint32_t nCharge = s.get_ChargeUpd();

    ProviderKeyID kid;
    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, &kid, 1, "Oracle2 set val", nCharge);
}


#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
{
    Env::DocGroup root("");

    char szRole[0x10], szAction[0x10];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

    const char* szErr = nullptr;

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Oracle2_##role##_##name(PAR_READ) \
            On_##role##_##name(Oracle2_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        Oracle2Role_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    Oracle2Roles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace Oracle2