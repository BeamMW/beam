////////////////////////
// Simple 'vault' shader
#include "common.h"
#include "vault.h"
#include "Math.h"

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
//    {   DocGroup gr("roles");
        {   DocNamedGroup gr0("create", "Create new");
            {
            }
        }
        {   DocNamedGroup gr0("my_account", "My account");
            {
//                {   DocGroup("actions");
                    {   DocNamedGroup gr1("view", "View");
                    }
                    {   DocNamedGroup gr1("deposit", "Deposit");
                        Env::DocAddText("amount", "Amount");
                        Env::DocAddText("asset", "AssetID");
                    }
                    {   DocNamedGroup gr1("withdraw", "Withdraw");
                        Env::DocAddText("amount", "Amount");
                        Env::DocAddText("asset", "AssetID");
                    }
  //              }

            }
        }
        {   DocNamedGroup gr0("all_accounts", "All accounts");
            {
//                {   DocGroup("actions");
                    {   DocNamedGroup gr1("view", "View");
                        Env::DocAddText("account", "PubKey");
                    }
//                }
            }
        }
//    }
}

#ifndef assert
#   define assert(x) do {} while (false)
#endif // assert

void EnumAndDump()
{
    while (true)
    {
        uint8_t nTag;
        const void* pKey, * pVal;
        uint32_t nKey, nVal;

        if (!Env::VarsMoveNext(&nTag, &pKey, &nKey, &pVal, &nVal))
            break;

        assert(!nTag);
        assert((sizeof(Vault::Key) == nKey) && (sizeof(Amount) == nVal));

        // alignment isn't important for bvm
        const Vault::Key& key = *(Vault::Key*) pKey;
        const Amount& amount = *(Amount*)pVal;

        Env::DocAddGroup("elem");
        Env::DocAddBlob("Account", &key.m_Account, sizeof(key.m_Account));
        Env::DocAddNum32("AssetID", key.m_Aid);
        Env::DocAddNum64("Amount", amount);
        Env::DocCloseGroup();
    }
}

void DumpAccount(const PubKey& pubKey)
{
    Vault::Key k0, k1;
    k0.m_Account = pubKey;
    k0.m_Aid = 0;
    k1.m_Account = pubKey;
    k1.m_Aid = static_cast<AssetID>(-1);

    Env::VarsEnum(0, &k0, sizeof(k0), 0, &k1, sizeof(k1));
}

void DeriveMyPk(PubKey& pubKey)
{
    static const char szKeyID[] = "my";
    Env::DerivePk(pubKey, szKeyID, sizeof(szKeyID));
}

void On_MyAccount(const char* szAction)
{
    if (!Env::Strcmp(szAction, "view")) {

        PubKey pubKey;
        DeriveMyPk(pubKey);
        DumpAccount(pubKey);
        return;
    }

    if (!Env::Strcmp(szAction, "deposit")) {

        return;
    }

    if (!Env::Strcmp(szAction, "withdraw")) {

        return;
    }

    Env::DocAddText("error", "invalid action");
}

void On_AllAccounts(const char* szAction)
{
    if (!Env::Strcmp(szAction, "view")) {
        PubKey pubKey;

        if (Env::DocGetBlob("account", &pubKey, sizeof(pubKey)) == sizeof(pubKey))
            DumpAccount(pubKey);
        else
        {
            Env::VarsEnum(0, nullptr, 0, 1, nullptr, 0); // enum all internal contract vars
            EnumAndDump();
        }
    }
    else
        Env::DocAddText("error", "invalid action");
}

export void Method_1()
{
    char szRole[0x10], szAction[0x10];

    if (!Env::DocGetText("role", szRole, sizeof(szRole))) {
        Env::DocAddText("error", "Role not specified");
        return;
    }

    Env::DocGetText("action", szAction, sizeof(szAction));

    if (!Env::Strcmp(szRole, "my_account")) {
        On_MyAccount(szAction);
        return;
    }

    if (!Env::Strcmp(szRole, "all_accounts")) {
        On_AllAccounts(szAction);
        return;
    }

    Env::DocAddText("error", "unknown Role");
}

