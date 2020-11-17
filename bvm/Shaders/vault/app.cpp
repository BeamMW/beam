#include "../common.h"
#include "contract.h"

struct DocGroup {
    DocGroup(const char* sz) {
        Env::DocAddGroup(sz);
    }
    ~DocGroup() {
        Env::DocCloseGroup();
    }
};

#define Vault_manager_create(macro)
#define Vault_manager_view(macro)
#define Vault_manager_destroy(macro) macro(ContractID, cid)
#define Vault_manager_view_accounts(macro) macro(ContractID, cid)

#define Vault_manager_view_account(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pubKey)

#define VaultRole_manager(macro) \
    macro(manager, create) \
    macro(manager, destroy) \
    macro(manager, view) \
    macro(manager, view_accounts) \
    macro(manager, view_account)

#define Vault_my_account_view(macro) macro(ContractID, cid)

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
    macro(my_account, deposit) \
    macro(my_account, withdraw)

#define VaultRoles_All(macro) \
    macro(manager) \
    macro(my_account)

export void Method_0()
{
    // scheme
    {   DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { DocGroup grMethod(#name);  Vault_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { DocGroup grRole(#name); VaultRole_##name(THE_METHOD) }
        
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

#pragma pack (push, 1)

struct KeyPrefix
{
    ContractID m_Cid;
    uint8_t m_Tag;
};

struct KeyRaw
{
    KeyPrefix m_Prefix;
    Vault::Key m_Key;
};

#pragma pack (pop)

void EnumAndDump()
{
    while (true)
    {
        const KeyRaw* pRawKey;
        const Amount* pVal;

        uint32_t nKey, nVal;
        if (!Env::VarsMoveNext((const void**) &pRawKey, &nKey, (const void**) &pVal, &nVal))
            break;

        if ((sizeof(*pRawKey) == nKey) && (sizeof(*pVal) == nVal))
        {
            // alignment isn't important for bvm
            Env::DocAddGroup("elem");
            Env::DocAddBlob("Account", &pRawKey->m_Key.m_Account, sizeof(pRawKey->m_Key.m_Account));
            Env::DocAddNum("AssetID", pRawKey->m_Key.m_Aid);
            Env::DocAddNum("Amount", *pVal);
            Env::DocCloseGroup();
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
    k0.m_Prefix.m_Cid = Vault::s_SID;
    k0.m_Prefix.m_Tag = 0x10; // sid-cid tag
    k1.m_Prefix = k0.m_Prefix;

    Env::Memset(&k0.m_Cid, 0, sizeof(k0.m_Cid));
    Env::Memset(&k1.m_Cid, 0xff, sizeof(k1.m_Cid));

    Env::VarsEnum(&k0, sizeof(k0), &k1, sizeof(k1));

    while (true)
    {
        const Key* pKey;
        const void* pVal;
        uint32_t nKey, nVal;

        if (!Env::VarsMoveNext((const void**) &pKey, &nKey, &pVal, &nVal))
            break;

        if ((sizeof(Key) != nKey) || (1 != nVal))
            continue;

        Env::DocAddBlob("Cid", &pKey->m_Cid, sizeof(pKey->m_Cid));
    }
}

ON_METHOD(manager, create)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, 1000000U);
}

ON_METHOD(manager, destroy)
{
    Env::GenerateKernel(&cid, 1, nullptr, 0, nullptr, 0, nullptr, 0, 1000000U);
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
        Env::GenerateKernel(&cid, Vault::Deposit::s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, 2000000U);
    else
    {
        MyAccountID myid;
        myid.m_Cid = cid;

        SigRequest sig;
        sig.m_pID = &myid;
        sig.m_nID = sizeof(myid);

        Env::GenerateKernel(&cid, Vault::Withdraw::s_iMethod, &arg, sizeof(arg), &fc, 1, &sig, 1, 2000000U);
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
    char szRole[0x10], szAction[0x10];
    ContractID cid;

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

