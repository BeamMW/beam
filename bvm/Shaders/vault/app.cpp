////////////////////////
// Simple 'vault' shader
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

struct DocNamedGroup
    :public DocGroup
{
    DocNamedGroup(const char* sz, const char* szName)
        :DocGroup(sz)
    {
        Env::DocAddText("name", szName);
    }
};

export void Method_0()
{
    // scheme
    {   DocGroup gr("roles");
        {   DocNamedGroup gr0("manager", "Manager");
            {   DocNamedGroup gr1("create", "Create new");
            }
            {   DocNamedGroup gr1("destroy", "Destroy");
                Env::DocAddText("cid", "ContractID");
            }
            {   DocNamedGroup gr1("view", "View");
            }
        }
        {   DocNamedGroup gr0("my_account", "My account");
            {   DocNamedGroup gr1("view", "View");
                Env::DocAddText("cid", "ContractID");
            }
            {   DocNamedGroup gr1("deposit", "Deposit");
                Env::DocAddText("cid", "ContractID");
                Env::DocAddText("amount", "Amount");
                Env::DocAddText("asset", "AssetID");
            }
            {   DocNamedGroup gr1("withdraw", "Withdraw");
                Env::DocAddText("cid", "ContractID");
                Env::DocAddText("amount", "Amount");
                Env::DocAddText("asset", "AssetID");
            }
        }
        {   DocNamedGroup gr0("all_accounts", "All accounts");
            {   DocNamedGroup gr1("view", "View");
                Env::DocAddText("cid", "ContractID");
                Env::DocAddText("account", "PubKey");
            }
        }
    }
}

#ifndef assert
#   define assert(x) do {} while (false)
#endif // assert

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

        assert((sizeof(*pRawKey) == nKey) && (sizeof(*pVal) == nVal));

        // alignment isn't important for bvm

        Env::DocAddGroup("elem");
        Env::DocAddBlob("Account", &pRawKey->m_Key.m_Account, sizeof(pRawKey->m_Key.m_Account));
        Env::DocAddNum("AssetID", pRawKey->m_Key.m_Aid);
        Env::DocAddNum("Amount", *pVal);
        Env::DocCloseGroup();
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

void On_ManagerView()
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

void On_Manager(const char* szAction, const ContractID* pCid)
{
    if (!Env::Strcmp(szAction, "create")) {
        Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, 1000000U);
        return;
    }

    if (!Env::Strcmp(szAction, "view")) {
        On_ManagerView();
        return;
    }

    if (!Env::Strcmp(szAction, "destroy")) {

        if (!pCid) {
            Env::DocAddText("error", "cid missing");
            return;
        }

        Env::GenerateKernel(pCid, 1, nullptr, 0, nullptr, 0, nullptr, 0, 1000000U);
        return;
    }

    Env::DocAddText("error", "invalid action");
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

void On_MyAccount_MoveFunds(uint32_t iMethod, uint8_t nConsume, const ContractID& cid)
{
    Vault::Request arg;

    Env::DocGetNum("amount", arg.m_Amount);
    if (!arg.m_Amount)
    {
        Env::DocAddText("error", "amount should be nnz");
        return;
    }

    Env::DocGetNum("asset", arg.m_Aid);
    DeriveMyPk(arg.m_Account, cid);

    FundsChange fc;
    fc.m_Amount = arg.m_Amount;
    fc.m_Aid = arg.m_Aid;
    fc.m_Consume = nConsume;

    MyAccountID myid;
    myid.m_Cid = cid;

    SigRequest sig;
    sig.m_pID = &myid;
    sig.m_nID = sizeof(myid);

    Env::GenerateKernel(&cid, iMethod, &arg, sizeof(arg), &fc, 1, &sig, !nConsume, 2000000U);
}

void On_MyAccount(const char* szAction, const ContractID& cid)
{
    if (!Env::Strcmp(szAction, "view")) {

        PubKey pubKey;
        DeriveMyPk(pubKey, cid);
        DumpAccount(pubKey, cid);
        return;
    }

    if (!Env::Strcmp(szAction, "deposit"))
    {
        On_MyAccount_MoveFunds(Vault::Deposit::s_iMethod, 1, cid);
        return;
    }

    if (!Env::Strcmp(szAction, "withdraw"))
    {
        On_MyAccount_MoveFunds(Vault::Withdraw::s_iMethod, 0, cid);
        return;
    }

    Env::DocAddText("error", "invalid action");
}

void On_AllAccounts(const char* szAction, const ContractID& cid)
{
    if (!Env::Strcmp(szAction, "view")) {
        PubKey pubKey;

        if (Env::DocGetBlob("account", &pubKey, sizeof(pubKey)) == sizeof(pubKey))
            DumpAccount(pubKey, cid);
        else
        {
            KeyPrefix k0, k1;
            k0.m_Cid = cid;
            k0.m_Tag = 0;
            k1.m_Cid = cid;
            k1.m_Tag = 1;

            Env::VarsEnum(&k0, sizeof(k0), &k1, sizeof(k1)); // enum all internal contract vars
            EnumAndDump();
        }
    }
    else
        Env::DocAddText("error", "invalid action");
}

export void Method_1()
{
    char szRole[0x10], szAction[0x10];
    ContractID cid;

    if (!Env::DocGetText("role", szRole, sizeof(szRole))) {
        Env::DocAddText("error", "Role not specified");
        return;
    }

    Env::DocGetText("action", szAction, sizeof(szAction));
    bool bCid = Env::DocGetBlob("cid", &cid, sizeof(cid)) == sizeof(cid);

    if (!Env::Strcmp(szRole, "manager")) {
        On_Manager(szAction, bCid ? &cid : nullptr);
        return;
    }

    if (!bCid) {
        Env::DocAddText("error", "cid missing");
        return;
    }

    if (!Env::Strcmp(szRole, "my_account")) {
        On_MyAccount(szAction, cid);
        return;
    }

    if (!Env::Strcmp(szRole, "all_accounts")) {
        On_AllAccounts(szAction, cid);
        return;
    }

    Env::DocAddText("error", "unknown Role");
}

