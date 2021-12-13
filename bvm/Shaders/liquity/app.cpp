#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable2/app_common_impl.h"

#define Liquity_manager_view(macro)
#define Liquity_manager_view_params(macro) macro(ContractID, cid)
#define Liquity_manager_my_admin_key(macro)
#define Liquity_manager_explicit_upgrade(macro) macro(ContractID, cid)

#define LiquityRole_manager(macro) \
    macro(manager, view) \
    macro(manager, explicit_upgrade) \
    macro(manager, view_params) \
    macro(manager, my_admin_key) \

#define Liquity_user_my_key(macro) macro(ContractID, cid)

#define LiquityRole_user(macro) \
    macro(user, my_key)

#define LiquityRoles_All(macro) \
    macro(manager) \
    macro(user)

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

namespace Liquity {

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr2-liquity";

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

struct TroveKeyID :public Env::KeyID
{
#pragma pack (push, 1)
    struct Blob {
        ContractID m_Cid;
        uint32_t m_Magic;
    } m_Blob;
#pragma pack (pop)

    TroveKeyID(const ContractID& cid)
    {
        m_pID = &m_Blob;
        m_nID = sizeof(m_Blob);

        _POD_(m_Blob.m_Cid) = cid;
        m_Blob.m_Magic = 0x21260ac2;

    }
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

    AdminKeyID kid;
    wlk.ViewAll(&kid);
}

ON_METHOD(manager, explicit_upgrade)
{
    ManagerUpgadable2::MultiSigRitual::Perform_ExplicitUpgrade(cid);
}

struct MyGlobal
    :public Global
{
    bool Load(const ContractID& cid)
    {
        Env::Key_T<uint8_t> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        key.m_KeyInContract = Tags::s_State;

        if (Env::VarReader::Read_T(key, Cast::Down<Global>(*this)))
            return true;

        OnError("no such a contract");
        return false;
    }
};

void DocAddPair(const char* sz, const Pair& p)
{
    Env::DocGroup gr(sz);

    Env::DocAddNum("tok", p.Tok);
    Env::DocAddNum("col", p.Col);

}

ON_METHOD(manager, view_params)
{
    MyGlobal g;
    if (!g.Load(cid))
        return;

    Env::DocGroup gr("params");
    Env::DocAddNum("aid", g.m_Aid);
    DocAddPair("totals", g.m_Troves.m_Totals);
}

ON_METHOD(manager, my_admin_key)
{
    PubKey pk;
    AdminKeyID kid;
    kid.get_Pk(pk);
    Env::DocAddBlob_T("admin_key", pk);
}

ON_METHOD(user, my_key)
{
    PubKey pk;
    TroveKeyID(cid).get_Pk(pk);
    Env::DocAddBlob_T("key", pk);
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

} // namespace Liquity
