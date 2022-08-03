#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define AssetMan_manager_deploy(macro)
#define AssetMan_manager_view(macro)
#define AssetMan_manager_view_assets(macro) macro(ContractID, cid)
#define AssetMan_manager_destroy(macro) macro(ContractID, cid)
#define AssetMan_manager_asset_create(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, depositBeams)

#define AssetMan_manager_asset_destroy(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid) \
    macro(uint32_t, depositBeams)

#define AssetManRole_manager(macro) \
    macro(manager, deploy) \
    macro(manager, destroy) \
    macro(manager, view) \
    macro(manager, view_assets) \
    macro(manager, asset_create) \
    macro(manager, asset_destroy) \

#define AssetManRoles_All(macro) \
    macro(manager)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  AssetMan_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); AssetManRole_##name(THE_METHOD) }
        
        AssetManRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) type& name,
#define ON_METHOD(role, name) void On_##role##_##name(AssetMan_##role##_##name(THE_FIELD) int unused = 0)

namespace AssetMan {

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

bool VerifyCid(ContractID& cid)
{
    if (_POD_(cid).IsZero())
    {
        WalkerContracts wlk;
        wlk.Enum(s_SID);

        if (!wlk.MoveNext())
        {
            OnError("cid not specified, and couldn't find deployed instance");
            return false;
        }

        _POD_(cid) = wlk.m_Key.m_KeyInContract.m_Cid;
    }

    return true;
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(AssetMan::s_SID);
}

ON_METHOD(manager, deploy)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy AssetMan contract", 0);
}

ON_METHOD(manager, destroy)
{
    if (VerifyCid(cid))
        Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, "Destroy AssetMan contract", 0);
}

ON_METHOD(manager, view_assets)
{
    if (!VerifyCid(cid))
        return;

    Env::Key_T<AssetID> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    k0.m_KeyInContract = 0;
    k0.m_Prefix.m_Tag = KeyTag::OwnedAsset;
    _POD_(k1.m_Prefix) = k0.m_Prefix;
    k1.m_KeyInContract = static_cast<AssetID>(-1);

    Env::DocArray gr("assets");

    PubKey pkOwner;
    for (Env::VarReader r(k0, k1); r.MoveNext_T(k0, pkOwner); )
    {
        Env::DocGroup gr1("");
        Env::DocAddNum("aid", Utils::FromBE(k0.m_KeyInContract));
    }
}

struct MyFunds :public FundsChange
{
    MyFunds(uint32_t nBeams)
    {
        m_Aid = 0;
        m_Amount = g_Beam2Groth * nBeams;
    }
};

uint32_t get_StdCharge()
{
    return
        Env::Cost::CallFar +
        Env::Cost::AssetManage +
        Env::Cost::Cycle * 50;
}

ON_METHOD(manager, asset_create)
{
    if (!VerifyCid(cid))
        return;

    MyFunds fc(depositBeams);
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, Method::AssetReg::s_iMethod, nullptr, 0, &fc, 1, nullptr, 0, "AssetMan asset register", get_StdCharge());
}

ON_METHOD(manager, asset_destroy)
{
    if (!VerifyCid(cid))
        return;

    MyFunds fc(depositBeams);
    fc.m_Consume = 1;

    Method::AssetUnreg arg;
    arg.m_Aid = aid;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "AssetMan asset unregister", get_StdCharge());
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
            AssetMan_##role##_##name(PAR_READ) \
            On_##role##_##name(AssetMan_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        AssetManRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    AssetManRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace AssetMan