#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Faucet_manager_create(macro) \
    macro(Height, backlogPeriod) \
    macro(Amount, withdrawLimit)

#define Faucet_manager_view(macro)
#define Faucet_manager_view_params(macro) macro(ContractID, cid)
#define Faucet_manager_view_funds(macro) macro(ContractID, cid)
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
    macro(manager, view_funds) \
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

typedef Env::Key_T<Faucet::Key> KeyAccount;

void DumpAccounts(Env::VarReader& r)
{
    Env::DocArray gr("accounts");

    while (true)
    {
        KeyAccount key;
        Faucet::AccountData d;

        if (!r.MoveNext_T(key, d))
            break;

        Env::DocGroup gr("");

        Env::DocAddBlob_T("Account", key.m_KeyInContract.m_Account);
        Env::DocAddNum("AssetID", key.m_KeyInContract.m_Aid);
        Env::DocAddNum("Amount", d.m_Amount);
        Env::DocAddNum("h0", d.m_h0);
    }
}

void DumpAccount(const PubKey& pubKey, const ContractID& cid)
{
    KeyAccount k0, k1;
    k0.m_Prefix.m_Cid = cid;
    k0.m_KeyInContract.m_Account = pubKey;
    k0.m_KeyInContract.m_Aid = 0;

    _POD_(k1) = k0;
    k1.m_KeyInContract.m_Aid = static_cast<AssetID>(-1);

    Env::VarReader r(k0, k1);
    DumpAccounts(r);
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(Faucet::s_SID);
}

ON_METHOD(manager, create)
{
    if (!backlogPeriod || !withdrawLimit)
        return OnError("backlog and withdraw limit should be nnz");

    Faucet::Params pars;
    pars.m_BacklogPeriod = backlogPeriod;
    pars.m_MaxWithdraw = withdrawLimit;

    Env::GenerateKernel(nullptr, pars.s_iMethod, &pars, sizeof(pars), nullptr, 0, nullptr, 0, "create Faucet contract", 0);
}

ON_METHOD(manager, destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, "destroy Faucet contract", 0);
}

ON_METHOD(manager, view_params)
{
    Env::Key_T<uint8_t> k;
    k.m_Prefix.m_Cid = cid;
    k.m_KeyInContract = 0;

    Faucet::Params pars;
    if (!Env::VarReader::Read_T(k, pars))
        return OnError("failed to read");

    Env::DocGroup gr("params");
    Env::DocAddNum("backlogPeriod", pars.m_BacklogPeriod);
    Env::DocAddNum("withdrawLimit", pars.m_MaxWithdraw);
}

ON_METHOD(manager, view_funds)
{
    Env::DocArray gr0("funds");

    WalkerFunds wlk;
    for (wlk.Enum(cid); wlk.MoveNext(); )
    {
        Env::DocGroup gr("");

        Env::DocAddNum("Aid", wlk.m_Aid);
        Env::DocAddNum("Amount", wlk.m_Val.m_Lo);
    }
}


ON_METHOD(manager, view_accounts)
{
    Env::KeyPrefix k0, k1;
    k0.m_Cid = cid;
    k1.m_Cid = cid;
    k1.m_Tag = KeyTag::Internal + 1;

    Env::VarReader r(k0, k1); // enum all internal contract vars
    DumpAccounts(r);
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

        Env::GenerateKernel(&cid, Faucet::Deposit::s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "deposit to Faucet", 0);
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

        Env::GenerateKernel(&cid, Faucet::Withdraw::s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, "withdraw from Faucet", 0);
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

