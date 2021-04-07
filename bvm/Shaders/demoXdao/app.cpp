#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../Math.h"

#define DemoXdao_manager_create(macro)
#define DemoXdao_manager_view(macro)
#define DemoXdao_manager_view_params(macro) macro(ContractID, cid)
#define DemoXdao_manager_lock(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount)

#define DemoXdao_manager_view_stake(macro) macro(ContractID, cid)

#define DemoXdaoRole_manager(macro) \
    macro(manager, create) \
    macro(manager, view) \
    macro(manager, view_params) \
    macro(manager, view_stake) \
    macro(manager, lock)

#define DemoXdaoRoles_All(macro) \
    macro(manager)

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  DemoXdao_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); DemoXdaoRole_##name(THE_METHOD) }
        
        DemoXdaoRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(DemoXdao_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(DemoXdao::s_SID);
}

static const Amount g_DepositCA = 3000 * g_Beam2Groth; // 3K beams

ON_METHOD(manager, create)
{
    FundsChange fc;
    fc.m_Aid = 0; // asset id
    fc.m_Amount = g_DepositCA; // amount of the input or output
    fc.m_Consume = 1; // contract consumes funds (i.e input, in this case)

    // Create kernel with all the required parameters
    // 
    Env::GenerateKernel(nullptr, 0, nullptr, 0, &fc, 1, nullptr, 0, "Deploy demoXdao contract", 0);
}

Amount get_ContractLocked(AssetID aid, const ContractID& cid)
{
    Env::Key_T<AssetID> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_Prefix.m_Tag = KeyTag::LockedAmount;
    key.m_KeyInContract = Utils::FromBE(aid);

    struct AmountBig {
        Amount m_Hi;
        Amount m_Lo;
    };

    const auto* pVal = Env::VarRead_T<AmountBig>(key);
    if (!pVal)
        return 0;

    return Utils::FromBE(pVal->m_Lo);
}

AssetID get_TrgAid(const ContractID& cid)
{
    Env::Key_T<uint8_t> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    key.m_KeyInContract = 0;

    const auto* pS = Env::VarRead_T<DemoXdao::State>(key);
    if (!pS)
    {
        OnError("no such contract");
        return 0;
    }

    return pS->m_Aid;
}

ON_METHOD(manager, view_params)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    Env::DocGroup gr("params");
    Env::DocAddNum("aid", aid);
    Env::DocAddNum("locked_demoX", get_ContractLocked(aid, cid));
    Env::DocAddNum("locked_beams", get_ContractLocked(0, cid));
}

ON_METHOD(manager, view_stake)
{
    Env::Key_T<PubKey> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    Env::DerivePk(key.m_KeyInContract, &cid, sizeof(cid));

    const auto* pAmount = Env::VarRead_T<Amount>(key);
    Env::DocAddNum("stake", pAmount ? *pAmount : 0);
}

ON_METHOD(manager, lock)
{
    auto aid = get_TrgAid(cid);
    if (!aid)
        return;

    DemoXdao::LockAndGet arg;
    arg.m_Amount = amount;

    Env::DerivePk(arg.m_Pk, &cid, sizeof(cid));

    SigRequest sig;
    sig.m_pID = &cid;
    sig.m_nID = sizeof(cid);

    FundsChange pFc[2];
    pFc[0].m_Aid = aid;
    pFc[0].m_Amount = DemoXdao::State::s_ReleasePerLock;
    pFc[0].m_Consume = 0;
    pFc[1].m_Aid = 0;
    pFc[1].m_Amount = amount;
    pFc[1].m_Consume = 1;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), &sig, 1, "Lock-and-get demoX tokens", 0);
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

    const char* szErr = nullptr;

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            DemoXdao_##role##_##name(PAR_READ) \
            On_##role##_##name(DemoXdao_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        DemoXdaoRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    DemoXdaoRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

