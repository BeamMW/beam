#include "../common.h"
#include "../app_common_impl.h"
#include "../app_comm.h"
#include "contract.h"

#define Oracle2_manager_create(macro) macro(uint64_t, initialVal_n)
#define Oracle2_manager_view(macro)
#define Oracle2_manager_view_params(macro) macro(ContractID, cid)
#define Oracle2_manager_view_median(macro) macro(ContractID, cid)

#define Oracle2Role_manager(macro) \
    macro(manager, create) \
    macro(manager, view) \
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

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(Oracle2::s_SID);
}

ON_METHOD(manager, create)
{
    if (!initialVal_n)
        return OnError("val can't be zero");

    ProviderKeyID kid;

    Method::Create<StateFull::s_ProvsMax> arg;
    arg.m_InitialValue = MultiPrecision::Float(initialVal_n) / get_Norm_n();

    arg.m_Providers = 1;
    kid.get_Pk(arg.m_pPk[0]);

    static const char s_szPrefix[] = "prov_";
    char szBuf[_countof(s_szPrefix) + Utils::String::Decimal::Digits<StateFull::s_ProvsMax>::N];
    Env::Memcpy(szBuf, s_szPrefix, sizeof(s_szPrefix) - sizeof(char));

    for (; arg.m_Providers < StateFull::s_ProvsMax; arg.m_Providers++)
    {
        Utils::String::Decimal::Print(szBuf + _countof(s_szPrefix) - 1, arg.m_Providers);
        if (!Env::DocGetBlob(szBuf, arg.m_pPk + arg.m_Providers, sizeof(arg.m_pPk[arg.m_Providers])))
            break;
    }

    const uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::SaveVar_For(sizeof(Median)) +
        Env::Cost::SaveVar_For(sizeof(StateFull)) +
        Env::Cost::Cycle * (200 + 200 * StateFull::s_ProvsMax);

    Env::GenerateKernel(nullptr, arg.s_iMethod, &arg, sizeof(Method::Create<0>) + sizeof(arg.m_pPk[0]) * arg.m_Providers, nullptr, 0, nullptr, 0, "create Oracle2 contract", nCharge);
}

struct MyState
    :public StateFull
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

        uint32_t nKey = sizeof(k), nVal = sizeof(StateFull);
        if (!r.MoveNext(&k, nKey, this, nVal, KeyTag::Internal))
            return false;

        m_Provs = nVal / sizeof(StateFull::Entry);
        if ((m_Provs > s_ProvsMax) || !m_Provs)
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
};

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

        auto iPos = x.m_iPos;
        Env::DocAddNum("val", s.m_pE[iPos].m_Val * get_Norm_n());
    }
}

ON_METHOD(manager, view_median)
{
    Env::Key_T<uint8_t> k;
    _POD_(k.m_Prefix.m_Cid) = cid;
    k.m_KeyInContract = Tags::s_Median;

    ValueType val;
    if (Env::VarReader::Read_T(k, val))
    {
        Env::DocArray gr("res");
        Env::DocAddNum("val", val * get_Norm_n());
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

    Method::Set arg;
    arg.m_iProvider = s.m_iProv;
    arg.m_Value = MultiPrecision::Float(val_n) / get_Norm_n();

    const uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::LoadVar_For(sizeof(StateFull)) +
        Env::Cost::SaveVar_For(sizeof(StateFull)) +
        Env::Cost::AddSig +
        Env::Cost::SaveVar_For(sizeof(Median)) +
        Env::Cost::Cycle * (1500 + 500 * StateFull::s_ProvsMax);

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