#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Sidechain_manager_create(macro) \
    macro(HashValue, rulesCfg) \
    macro(uint32_t, verifyPoW) \
    macro(Amount, comission) \
    macro(BlockHeader::Full, hdr0) \

#define Sidechain_manager_view(macro)
#define Sidechain_manager_view_params(macro) macro(ContractID, cid)

#define SidechainRole_manager(macro) \
    macro(manager, create) \
    macro(manager, view) \
    macro(manager, view_params)

#define SidechainRoles_All(macro) \
    macro(manager)

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Sidechain_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); SidechainRole_##name(THE_METHOD) }
        
        SidechainRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Sidechain_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}


ON_METHOD(manager, view)
{
    EnumAndDumpContracts(Sidechain::s_SID);
}

ON_METHOD(manager, create)
{
    Sidechain::Init pars;

    Utils::Copy(pars.m_Rules, rulesCfg);
    pars.m_VerifyPoW = !!verifyPoW;
    pars.m_ComissionForProof = comission;
    Utils::Copy(pars.m_Hdr0, hdr0);

    // Create kernel with all the required parameters
    // 
    Env::GenerateKernel(nullptr, pars.s_iMethod, &pars, sizeof(pars), nullptr, 0, nullptr, 0, "generate Sidechain contract", 2000000U);
}

ON_METHOD(manager, view_params)
{
    const Sidechain::Global* pG;
    if (!Env::VarRead_T((uint8_t) 0, pG))
    {
        OnError("no global data");
        return;
    }

    Env::DocGroup gr("params");

    Env::DocAddNum("Height", pG->m_Height);

    HashValue hv;
    pG->m_Chainwork.ToBE_T(hv);
    Env::DocAddBlob_T("Chainwork", hv);
}

#undef ON_METHOD
#undef THE_FIELD

namespace Env {

    inline bool DocGet(const char* szID, BlockHeader::Full& val) {
        return DocGetBlobEx(szID, &val, sizeof(val));
    }

} // namespace Env

export void Method_1() 
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
            Sidechain_##role##_##name(PAR_READ) \
            On_##role##_##name(Sidechain_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        SidechainRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    SidechainRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

