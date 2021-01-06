#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Perpetual_manager_create(macro) \
    macro(ContractID, oracle) \
    macro(uint32_t, nMargin_mp)

#define Perpetual_manager_view(macro)
#define Perpetual_manager_view_offers(macro) macro(ContractID, cid)

#define PerpetualRole_manager(macro) \
    macro(manager, create) \
    macro(manager, view) \
    macro(manager, view_offers)

#define Perpetual_my_account_view(macro) macro(ContractID, cid)

#define Perpetual_my_account_CreateOffer(macro) \
    macro(ContractID, cid) \
    macro(Amount, amountBeams) \
    macro(Amount, amountToken) \
    macro(Amount, amountLock)

#define PerpetualRole_my_account(macro) \
    macro(my_account, CreateOffer)

#define PerpetualRoles_All(macro) \
    macro(manager) \
    macro(my_account)

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Perpetual_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); PerpetualRole_##name(THE_METHOD) }
        
        PerpetualRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Perpetual_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

typedef Env::Key_T<PubKey> KeyOffer;


void PrintOffers()
{
    Env::DocArray gr("accounts");

    while (true)
    {
        const KeyOffer* pKey;
        const Perpetual::OfferState* pState;
        
        if (!Env::VarsMoveNext_T(pKey, pState))
            break;

        Env::DocGroup gr("");

        Env::DocAddBlob_T("User1", pKey->m_KeyInContract);
        Env::DocAddBlob_T("User2", pState->m_User2);
        Env::DocAddNum("Amount1", pState->m_Amount1);
        Env::DocAddNum("Amount2", pState->m_Amount2);
    }
}

void PrintOffer(const PubKey& pubKey, const ContractID& cid)
{
    KeyOffer k;
    k.m_Prefix.m_Cid = cid;
    k.m_KeyInContract = pubKey;

    Env::VarsEnum_T(k, k);
    PrintOffers();
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(Perpetual::s_SID);
}

ON_METHOD(manager, create)
{
    Perpetual::Create arg;
    arg.m_MarginRequirement_mp = nMargin_mp;
    arg.m_Oracle = oracle;

    Env::GenerateKernel(nullptr, 0, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "create Perpetual contract", 1000000U);
}

ON_METHOD(manager, view_offers)
{
    Env::KeyPrefix k0, k1;
    Utils::Copy(k0.m_Cid, cid);
    Utils::Copy(k1.m_Cid, cid);
    k1.m_Tag = KeyTag::Internal + 1;

    Env::VarsEnum_T(k0, k1); // enum all internal contract vars
    PrintOffers();
}

void DeriveMyPk(PubKey& pubKey, const ContractID& cid)
{
    Env::DerivePk(pubKey, &cid, sizeof(cid));
}

ON_METHOD(my_account, CreateOffer)
{
    Perpetual::CreateOffer arg;
    DeriveMyPk(arg.m_Account, cid);
    arg.m_TotalBeams = amountLock;
    arg.m_AmountBeam = amountBeams;
    arg.m_AmountToken = amountToken;

    FundsChange fc;
    fc.m_Aid = 0;
    fc.m_Consume = 1;
    fc.m_Amount = amountLock;

    SigRequest sig;
    sig.m_pID = &cid;
    sig.m_nID = sizeof(cid);

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, "Creating offer", 1000000U);
}

ON_METHOD(my_account, view)
{
    PubKey pubKey;
    DeriveMyPk(pubKey, cid);
    PrintOffer(pubKey, cid);
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
            Perpetual_##role##_##name(PAR_READ) \
            On_##role##_##name(Perpetual_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        PerpetualRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    PerpetualRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

