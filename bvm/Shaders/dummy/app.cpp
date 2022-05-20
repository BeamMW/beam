#include "../common.h"
#include "../app_common_impl.h"

#define Dummy_manager_get_Sid(macro)

#define DummyRole_manager(macro) \
    macro(manager, get_Sid)

#define DummyRoles_All(macro) \
    macro(manager)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Dummy_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); DummyRole_##name(THE_METHOD) }
        
        DummyRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Dummy_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

ON_METHOD(manager, get_Sid)
{
    ShaderID sid;
    if (!Utils::get_ShaderID_FromArg(sid))
        return OnError("shader not specified");

    Env::DocGroup root("res");
    Env::DocAddBlob_T("sid", sid);

    // also print as formatted text
    char szBuf[sizeof(sid) * 5];
    char* sz = szBuf;

    for (uint32_t i = 0; ; )
    {
        sz[0] = '0';
        sz[1] = 'x';
        Utils::String::Hex::Print(sz + 2, sid.m_p[i], 2);

        if (++i == sizeof(sid))
            break;

        sz[4] = ',';
        sz += 5;
    }

    Env::DocAddText("sid-txt", szBuf);
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
            Dummy_##role##_##name(PAR_READ) \
            On_##role##_##name(Dummy_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        DummyRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    DummyRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}
