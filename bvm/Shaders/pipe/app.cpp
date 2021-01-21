#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Pipe_manager_view(macro)

#define Pipe_manager_create(macro) \
    macro(uint32_t, Out_CheckpointMaxMsgs) \
    macro(uint32_t, Out_CheckpointMaxDH) \
    macro(HashValue, In_RulesRemote) \
    macro(Amount, In_ComissionPerMsg) \
    macro(Amount, In_StakeForRemote) \
    macro(Height, In_hDisputePeriod) \
    macro(Height, In_hContenderWaitPeriod) \
    macro(uint32_t, In_FakePoW)

#define Pipe_manager_set_remote(macro) \
    macro(ContractID, cid) \
    macro(ContractID, cidRemote)

#define PipeRole_manager(macro) \
    macro(manager, view) \
    macro(manager, create) \
    macro(manager, set_remote)

#define PipeRoles_All(macro) \
    macro(manager) \

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Pipe_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); PipeRole_##name(THE_METHOD) }
        
        PipeRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Pipe_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(Pipe::s_SID);
}

ON_METHOD(manager, create)
{
    Pipe::Create arg;
    arg.m_Cfg.m_Out.m_CheckpointMaxMsgs = Out_CheckpointMaxMsgs;
    arg.m_Cfg.m_Out.m_CheckpointMaxDH = Out_CheckpointMaxDH;
    arg.m_Cfg.m_In.m_RulesRemote = In_RulesRemote;
    arg.m_Cfg.m_In.m_ComissionPerMsg = In_ComissionPerMsg;
    arg.m_Cfg.m_In.m_StakeForRemote = In_StakeForRemote;
    arg.m_Cfg.m_In.m_hDisputePeriod = In_hDisputePeriod;
    arg.m_Cfg.m_In.m_hContenderWaitPeriod = In_hContenderWaitPeriod;
    arg.m_Cfg.m_In.m_FakePoW = !!In_FakePoW;

    Env::GenerateKernel(nullptr, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "create Pipe contract", 1000000U);
}

ON_METHOD(manager, set_remote)
{
    Pipe::SetRemote arg;
    arg.m_cid = cidRemote;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Pipe contract 2nd-stage init", 1000000U);
}


#undef ON_METHOD
#undef THE_FIELD

export void Method_1()
{
    Env::DocGroup root("");

    char szRole[0x10], szAction[0x10];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Pipe_##role##_##name(PAR_READ) \
            On_##role##_##name(Pipe_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        PipeRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    PipeRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

