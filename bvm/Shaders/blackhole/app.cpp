#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define BlackHole_manager_deploy(macro)
#define BlackHole_manager_view(macro)
#define BlackHole_manager_view_funds(macro) macro(ContractID, cid)

#define BlackHole_manager_deposit(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define BlackHoleRole_manager(macro) \
    macro(manager, deploy) \
    macro(manager, view) \
    macro(manager, view_funds) \
    macro(manager, deposit)

#define BlackHoleRoles_All(macro) \
    macro(manager)

namespace BlackHole
{

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  BlackHole_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); BlackHoleRole_##name(THE_METHOD) }
        
        BlackHoleRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(BlackHole_##role##_##name(THE_FIELD) int unused = 0)

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
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy Black hole contract contract", 0);
}

ON_METHOD(manager, view_funds)
{
    Env::DocArray gr("res");

    WalkerFunds wlk;
    for (wlk.Enum(cid); wlk.MoveNext(); )
    {
        Env::DocGroup gr1("");

        Env::DocAddNum("aid", wlk.m_Aid);
        Env::DocAddNum("amount", wlk.m_Val.m_Lo);
    }
}

ON_METHOD(manager, deposit)
{
    if (!amount)
        return OnError("amount not specified");

    Method::Deposit arg;
    arg.m_Aid = aid;
    arg.m_Amount = amount;

    FundsChange fc;
    fc.m_Consume = 1;
    fc.m_Aid = aid;
    fc.m_Amount = amount;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Send to Black hole contract", 0);
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
            BlackHole_##role##_##name(PAR_READ) \
            On_##role##_##name(BlackHole_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        BlackHoleRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    BlackHoleRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace BlackHole