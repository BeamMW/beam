#include "app_impl.h"
#include "../app_common_impl.h"

#define VaultAnon_manager_view(macro)
#define VaultAnon_manager_deploy(macro)
#define VaultAnon_manager_view_account(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkOwner)

#define VaultAnonRole_manager(macro) \
    macro(manager, view) \
    macro(manager, deploy) \
    macro(manager, view_account)

#define VaultAnon_user_view_raw(macro) macro(ContractID, cid)
#define VaultAnon_user_view_anon(macro) macro(ContractID, cid)
#define VaultAnon_user_my_key(macro) macro(ContractID, cid)

#define VaultAnon_user_send_raw(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkOwner) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define VaultAnon_user_send_anon(macro) VaultAnon_user_send_raw(macro)

#define VaultAnon_user_receive_raw(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define VaultAnon_user_receive_anon(macro) \
    VaultAnon_user_receive_raw(macro) \
    macro(PubKey, pkOwner)

#define VaultAnonRole_user(macro) \
    macro(user, view_raw) \
    macro(user, view_anon) \
    macro(user, my_key) \
    macro(user, send_raw) \
    macro(user, send_anon) \
    macro(user, receive_raw) \
    macro(user, receive_anon)

#define VaultAnonRoles_All(macro) \
    macro(manager) \
    macro(user)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

namespace VaultAnon {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  VaultAnon_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); VaultAnonRole_##name(THE_METHOD) }
        
        VaultAnonRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(VaultAnon_##role##_##name(THE_FIELD) int unused = 0)

struct MyKeyID :public Env::KeyID
{
    MyKeyID(const ContractID& cid) {
        m_pID = &cid;
        m_nID = sizeof(cid);
    }
};


ON_METHOD(manager, view)
{
    EnumAndDumpContracts(s_SID);
}

ON_METHOD(manager, deploy)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy VaultAnon contract", 0);
}

struct MyAccountsPrinter
    :public WalkerAccounts_Print
{
    void PrintMsg(bool bIsAnon, const uint8_t* pMsg, uint32_t nMsg) override
    {
        if (nMsg)
            Env::DocAddBlob("custom", pMsg, nMsg);
    }
};

ON_METHOD(manager, view_account)
{
    Env::DocArray gr("accounts");
    MyAccountsPrinter wlk;
    wlk.Proceed(cid, _POD_(pkOwner).IsZero() ? nullptr : &pkOwner, nullptr);
}

ON_METHOD(user, view_raw)
{
    MyAccountsPrinter prnt;
    OnUser_view_raw(cid, MyKeyID(cid), prnt);
}

ON_METHOD(user, view_anon)
{
    MyAccountsPrinter prnt;
    OnUser_view_anon(cid, MyKeyID(cid), prnt);
}

ON_METHOD(user, my_key)
{
    PubKey pk;
    MyKeyID(cid).get_Pk(pk);

    Env::DocGroup gr("res");
    Env::DocAddBlob_T("key", pk);
}

ON_METHOD(user, send_raw)
{
    OnUser_send_raw(cid, pkOwner, aid, amount);
}

ON_METHOD(user, send_anon)
{
    uint8_t pMsg[s_MaxMsgSize];
    auto nMsg = Env::DocGetBlob("msg", pMsg, sizeof(pMsg));
    if (nMsg > sizeof(pMsg))
        return OnError("msg too long");

    OnUser_send_anon(cid, pkOwner, aid, amount, pMsg, nMsg);
}

ON_METHOD(user, receive_raw)
{
    OnUser_receive_raw(cid, MyKeyID(cid), aid, amount);

}

ON_METHOD(user, receive_anon)
{
    OnUser_receive_anon(cid, MyKeyID(cid), pkOwner, aid, amount);
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

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            VaultAnon_##role##_##name(PAR_READ) \
            On_##role##_##name(VaultAnon_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        VaultAnonRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    VaultAnonRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace VaultAnon
