#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define NameService_manager_view(macro)
#define NameService_manager_deploy(macro)
#define NameService_manager_view_account(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pk)

#define NameService_manager_view_name(macro) macro(ContractID, cid)

#define NameServiceRole_manager(macro) \
    macro(manager, view) \
    macro(manager, deploy) \
    macro(manager, view_account) \
    macro(manager, view_name)

#define NameService_user_my_key(macro) macro(ContractID, cid) macro(ContractID, cidVault)
#define NameService_user_view(macro) macro(ContractID, cid) macro(ContractID, cidVault)

#define NameServiceRole_user(macro) \
    macro(user, my_key) \
    macro(user, view)

#define NameServiceRoles_All(macro) \
    macro(manager) \
    macro(user)

namespace NameService {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  NameService_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); NameServiceRole_##name(THE_METHOD) }
        
        NameServiceRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(NameService_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(s_SID);
}

ON_METHOD(manager, deploy)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy NameService contract", 0);
}

void DumpName(const Domain& d)
{
    Env::DocAddBlob_T("key", d.m_pkOwner);
    Env::DocAddBlob_T("hExpire", d.m_hExpire);
}

#pragma pack (push, 1)
struct KeyMaxPlus :public Domain::KeyMax {
    char m_chExtra;
};
#pragma pack (pop)

void DumpDomains(const ContractID& cid, const PubKey* pPk)
{
    Env::Key_T<Domain::Key0> k0;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_sz).SetZero();

    Env::Key_T<KeyMaxPlus> k1;
    _POD_(k1.m_Prefix.m_Cid) = cid;
    Env::Memset(k1.m_KeyInContract.m_sz, 0xff, Domain::s_MaxLen);

    Env::DocArray gr0("res");

    for (Env::VarReader r(k0, k1); ; )
    {
        Domain d;
        uint32_t nKey = sizeof(k1) - 1, nVal = sizeof(d);

        if (!r.MoveNext(&k1, nKey, &d, nVal, 0))
            break;

        if (pPk && (_POD_(*pPk) != d.m_pkOwner))
            continue;

        Env::DocGroup gr("");

        uint32_t nNameLen = nKey - (sizeof(Env::KeyPrefix) + 1);
        k1.m_KeyInContract.m_sz[nNameLen] = 0;

        Env::DocAddText("name", k1.m_KeyInContract.m_sz);
        DumpName(d);
    }
}

ON_METHOD(manager, view_account)
{
    DumpDomains(cid, _POD_(pk).IsZero() ? nullptr : &pk);
}

ON_METHOD(manager, view_name)
{
    Env::Key_T<KeyMaxPlus> key;

    uint32_t nSize = Env::DocGetText("name", key.m_KeyInContract.m_sz, Domain::s_MaxLen + 1);
    if (!nSize)
        return OnError("name not specified");

    nSize += sizeof(Env::KeyPrefix);

    Env::VarReader r(&key, nSize, &key, nSize);
    Domain d;
    uint32_t nKey = 0, nVal = sizeof(d);
    if (r.MoveNext(nullptr, nKey, &d, nVal, 0))
    {
        Env::DocGroup gr("res");
        DumpName(d);
    }
}

struct MyKeyID :public Env::KeyID
{
    MyKeyID(const ContractID& cidVault) {
        m_pID = &cidVault;
        m_nID = sizeof(cidVault);
    }
};


ON_METHOD(user, view)
{
    PubKey pk;
    MyKeyID(cidVault).get_Pk(pk);

    DumpDomains(cid, &pk);
}

ON_METHOD(user, my_key)
{
    PubKey pk;
    MyKeyID(cid).get_Pk(pk);

    Env::DocGroup gr("res");
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

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            NameService_##role##_##name(PAR_READ) \
            On_##role##_##name(NameService_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        NameServiceRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    NameServiceRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace NameService
