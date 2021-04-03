#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Vault_manager_create(macro)
#define Vault_manager_view(macro)
#define Vault_manager_destroy(macro) macro(ContractID, cid)
#define Vault_manager_view_accounts(macro) macro(ContractID, cid)
#define Vault_manager_view_logs(macro) macro(ContractID, cid)

#define Vault_manager_view_account(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pubKey)

#define VaultRole_manager(macro) \
    macro(manager, create) \
    macro(manager, destroy) \
    macro(manager, view) \
    macro(manager, view_logs) \
    macro(manager, view_accounts) \
    macro(manager, view_account)

#define Vault_my_account_view(macro) macro(ContractID, cid)
#define Vault_my_account_get_proof(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid)

#define Vault_my_account_deposit(macro) \
    macro(ContractID, cid) \
    macro(Amount, amount) \
    macro(AssetID, aid)

#define Vault_my_account_withdraw(macro) Vault_my_account_deposit(macro)

#define Vault_my_account_move(macro) \
    macro(uint8_t, isDeposit) \
    Vault_my_account_deposit(macro)

#define VaultRole_my_account(macro) \
    macro(my_account, view) \
    macro(my_account, get_proof) \
    macro(my_account, deposit) \
    macro(my_account, withdraw)

#define VaultRoles_All(macro) \
    macro(manager) \
    macro(my_account)

export void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Vault_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); VaultRole_##name(THE_METHOD) }
        
        VaultRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Vault_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

typedef Env::Key_T<Vault::Key> KeyAccount;


void DumpAccounts()
{
    Env::DocArray gr("accounts");

    while (true)
    {
        const KeyAccount* pAccount;
        const Amount* pAmount;
        
        if (!Env::VarsMoveNext_T(pAccount, pAmount))
            break;

        Env::DocGroup gr("");

        Env::DocAddBlob_T("Account", pAccount->m_KeyInContract.m_Account);
        Env::DocAddNum("AssetID", pAccount->m_KeyInContract.m_Aid);
        Env::DocAddNum("Amount", *pAmount);
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

    Env::VarsEnum_T(k0, k1);
    DumpAccounts();
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(Vault::s_SID);
}

ON_METHOD(manager, create)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "create Vault contract", 0);
}

ON_METHOD(manager, destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, "destroy Vault contract", 0);
}

ON_METHOD(manager, view_logs)
{
    Env::Log_T<Vault::Key> k0, k1;
    _POD_(k0.m_Cid) = cid;
    _POD_(k0.m_Key).SetZero();
    _POD_(k1.m_Cid) = cid;
    _POD_(k1.m_Key).SetObject(0xff);

    Env::LogsEnum(&k0, sizeof(k0), &k1, sizeof(k1), nullptr, nullptr);

    Env::DocArray gr("logs");

    while (true)
    {
        HeightPos pos;
        const Env::Log_T<Vault::Key>* pKey;
        const Amount* pVal;

        if (!Env::LogsMoveNext_T(pKey, pVal, pos))
            break;

        Env::DocGroup gr("");

        Env::DocAddNum("Height", pos.m_Height);
        Env::DocAddNum("Pos", pos.m_Pos);
        Env::DocAddBlob_T("Account", pKey->m_Key.m_Account);
        Env::DocAddNum("AssetID", pKey->m_Key.m_Aid);
        Env::DocAddNum("Amount", *pVal);
    }
}


ON_METHOD(manager, view_accounts)
{
    Env::KeyPrefix k0, k1;
    _POD_(k0.m_Cid) = cid;
    _POD_(k1.m_Cid) = cid;
    k1.m_Tag = KeyTag::Internal + 1;

    Env::VarsEnum_T(k0, k1); // enum all internal contract vars
    DumpAccounts();
}

ON_METHOD(manager, view_account)
{
    DumpAccount(pubKey, cid);
}

#pragma pack (push, 1)
struct MyAccountID
{
    ContractID m_Cid;
    uint8_t m_Ctx = 0;
};
#pragma pack (pop)

void DeriveMyPk(PubKey& pubKey, const ContractID& cid)
{
    MyAccountID myid;
    myid.m_Cid = cid;

    Env::DerivePk(pubKey, &myid, sizeof(myid));
}

ON_METHOD(my_account, move)
{
    if (!amount)
        return OnError("amount should be nnz");

    Vault::Request arg;
    arg.m_Amount = amount;
    arg.m_Aid = aid;
    DeriveMyPk(arg.m_Account, cid);

    FundsChange fc;
    fc.m_Amount = arg.m_Amount;
    fc.m_Aid = arg.m_Aid;
    fc.m_Consume = isDeposit;

    if (isDeposit)
        Env::GenerateKernel(&cid, Vault::Deposit::s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "deposit to Vault", 0);
    else
    {
        MyAccountID myid;
        myid.m_Cid = cid;

        SigRequest sig;
        sig.m_pID = &myid;
        sig.m_nID = sizeof(myid);

        Env::GenerateKernel(&cid, Vault::Withdraw::s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, "withdraw from Vault", 0);
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

ON_METHOD(my_account, get_proof)
{
    KeyAccount key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    DeriveMyPk(key.m_KeyInContract.m_Account, cid);
    key.m_KeyInContract.m_Aid = aid;

    Amount* pAmount;
    uint32_t nSizeVal;
    const Merkle::Node* pProof;
    uint32_t nProof = Env::VarGetProof(&key, sizeof(key), (const void**) &pAmount, &nSizeVal, &pProof);

    if (nProof && sizeof(*pAmount) == nSizeVal)
    {
        Env::DocAddNum("Amount", *pAmount);
        Env::DocAddBlob("proof", pProof, sizeof(*pProof) * nProof);
    }
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
            Vault_##role##_##name(PAR_READ) \
            On_##role##_##name(Vault_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        VaultRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    VaultRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

