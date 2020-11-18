#include "../common.h"
#include "contract.h"

#define Faucet_manager_create(macro) \
    macro(Height, backlogPeriod) \
    macro(Amount, withdrawLimit)

#define Faucet_manager_view(macro)
#define Faucet_manager_view_params(macro) macro(ContractID, cid)
#define Faucet_manager_destroy(macro) macro(ContractID, cid)
#define Faucet_manager_view_accounts(macro) macro(ContractID, cid)

#define Faucet_manager_view_account(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pubKey)

#define FaucetRole_manager(macro) \
    macro(manager, create) \
    macro(manager, destroy) \
    macro(manager, view) \
    macro(manager, view_params) \
    macro(manager, view_accounts) \
    macro(manager, view_account)

#define Faucet_my_account_view(macro) macro(ContractID, cid)

#define Faucet_my_account_deposit(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount) \
    macro(AssetID, aid)

#define Faucet_my_account_withdraw(macro) Faucet_my_account_deposit(macro)

#define Faucet_my_account_move(macro) \
    macro(uint8_t, isDeposit) \
    Faucet_my_account_deposit(macro)

#define FaucetRole_my_account(macro) \
    macro(my_account, view) \
    macro(my_account, deposit) \
    macro(my_account, withdraw)

#define FaucetRoles_All(macro) \
    macro(manager) \
    macro(my_account)

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Faucet_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); FaucetRole_##name(THE_METHOD) }
        
        FaucetRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Faucet_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

#pragma pack (push, 1)

struct KeyPrefix
{
    ContractID m_Cid;
    uint8_t m_Tag;
};

struct KeyRaw
{
    KeyPrefix m_Prefix;
    Faucet::Key m_Key;
};

struct KeyGlobal
{
    KeyPrefix m_Prefix;
    uint8_t m_Val = 0;
};

#pragma pack (pop)

void EnumAndDump()
{
    Env::DocArray gr("accounts");

    while (true)
    {
        const KeyRaw* pRawKey;
        const Faucet::AccountData* pVal;

        uint32_t nKey, nVal;
        if (!Env::VarsMoveNext((const void**) &pRawKey, &nKey, (const void**) &pVal, &nVal))
            break;

        if ((sizeof(*pRawKey) == nKey) && (sizeof(*pVal) == nVal))
        {
            Env::DocGroup gr("");

            Env::DocAddBlob_T("Account", pRawKey->m_Key.m_Account);
            Env::DocAddNum("AssetID", pRawKey->m_Key.m_Aid);
            Env::DocAddNum("Amount", pVal->m_Amount);
            Env::DocAddNum("h0", pVal->m_h0);
        }
    }
}

void DumpAccount(const PubKey& pubKey, const ContractID& cid)
{
    KeyRaw k0, k1;
    k0.m_Prefix.m_Cid = cid;
    k0.m_Prefix.m_Tag = 0;
    k0.m_Key.m_Account = pubKey;
    k0.m_Key.m_Aid = 0;

    Env::Memcpy(&k1, &k0, sizeof(k0));
    k1.m_Key.m_Aid = static_cast<AssetID>(-1);

    Env::VarsEnum(&k0, sizeof(k0), &k1, sizeof(k1));
    EnumAndDump();
}

ON_METHOD(manager, view)
{

#pragma pack (push, 1)
    struct Key {
        KeyPrefix m_Prefix;
        ContractID m_Cid;
    };
#pragma pack (pop)

    Key k0, k1;
    k0.m_Prefix.m_Cid = Faucet::s_SID;
    k0.m_Prefix.m_Tag = 0x10; // sid-cid tag
    k1.m_Prefix = k0.m_Prefix;

    Env::Memset(&k0.m_Cid, 0, sizeof(k0.m_Cid));
    Env::Memset(&k1.m_Cid, 0xff, sizeof(k1.m_Cid));

    Env::VarsEnum(&k0, sizeof(k0), &k1, sizeof(k1));

    Env::DocArray gr("Cids");

    while (true)
    {
        const Key* pKey;
        const void* pVal;
        uint32_t nKey, nVal;

        if (!Env::VarsMoveNext((const void**) &pKey, &nKey, &pVal, &nVal))
            break;

        if ((sizeof(Key) != nKey) || (1 != nVal))
            continue;

        Env::DocAddBlob_T("", pKey->m_Cid);
    }
}

ON_METHOD(manager, create)
{
    if (!backlogPeriod || !withdrawLimit)
        return OnError("backlog and withdraw limit should be nnz");

    Faucet::Params pars;
    pars.m_BacklogPeriod = backlogPeriod;
    pars.m_MaxWithdraw = withdrawLimit;

    Env::GenerateKernel(nullptr, pars.s_iMethod, &pars, sizeof(pars), nullptr, 0, nullptr, 0, 1000000U);
}

ON_METHOD(manager, destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, 1000000U);
}

ON_METHOD(manager, view_params)
{
    KeyGlobal k;
    k.m_Prefix.m_Cid = cid;
    k.m_Prefix.m_Tag = 0;

    Env::VarsEnum(&k, sizeof(k), &k, sizeof(k));

    const void* pK;
    const Faucet::Params* pVal;

    uint32_t nKey, nVal;
    if (!Env::VarsMoveNext(&pK, &nKey, (const void**) &pVal, &nVal) || (sizeof(*pVal) != nVal))
        return OnError("failed to read");

    Env::DocGroup gr("params");
    Env::DocAddNum("backlogPeriod", pVal->m_BacklogPeriod);
    Env::DocAddNum("withdrawLimit", pVal->m_MaxWithdraw);
}


ON_METHOD(manager, view_accounts)
{
    KeyPrefix k0, k1;
    k0.m_Cid = cid;
    k0.m_Tag = 0;
    k1.m_Cid = cid;
    k1.m_Tag = 1;

    Env::VarsEnum(&k0, sizeof(k0), &k1, sizeof(k1)); // enum all internal contract vars
    EnumAndDump();
}

ON_METHOD(manager, view_account)
{
    DumpAccount(pubKey, cid);
}

void DeriveMyPk(PubKey& pubKey, const ContractID& cid)
{
    Env::DerivePk(pubKey, &cid, sizeof(cid));
}

ON_METHOD(my_account, move)
{
    if (!amount)
        return OnError("amount should be nnz");

    FundsChange fc;
    fc.m_Amount = amount;
    fc.m_Aid = aid;
    fc.m_Consume = isDeposit;

    if (isDeposit)
    {
        Faucet::Deposit arg;
        arg.m_Aid = fc.m_Aid;
        arg.m_Amount = fc.m_Amount;

        Env::GenerateKernel(&cid, Faucet::Deposit::s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, 1000000U);
    }
    else
    {
        Faucet::Withdraw arg;
        arg.m_Amount = fc.m_Amount;
        arg.m_Key.m_Aid = fc.m_Aid;
        DeriveMyPk(arg.m_Key.m_Account, cid);

        SigRequest sig;
        sig.m_pID = &cid;
        sig.m_nID = sizeof(cid);

        Env::GenerateKernel(&cid, Faucet::Withdraw::s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, 2000000U);
    }
}

ON_METHOD(my_account, deposit)
{
    On_my_account_move(1, cid, amount, aid);
}

ON_METHOD(my_account, withdraw)
{
    On_my_account_move(0, cid, amount, aid);
}

ON_METHOD(my_account, view)
{
    PubKey pubKey;
    DeriveMyPk(pubKey, cid);
    DumpAccount(pubKey, cid);
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
            Faucet_##role##_##name(PAR_READ) \
            On_##role##_##name(Faucet_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        FaucetRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    FaucetRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

