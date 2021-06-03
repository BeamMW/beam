#include "../common.h"
#include "../app_common_impl.h"

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

/*
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
    Env::Key_T<Vault::Key> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract).SetZero();
    _POD_(k1.m_Prefix.m_Cid) = cid;
    _POD_(k1.m_KeyInContract).SetObject(0xff);

    Env::LogsEnum(&k0, sizeof(k0), &k1, sizeof(k1), nullptr, nullptr);

    Env::DocArray gr("logs");

    while (true)
    {
        HeightPos pos;
        const Env::Key_T<Vault::Key>* pKey;
        const Amount* pVal;

        if (!Env::LogsMoveNext_T(pKey, pVal, pos))
            break;

        Env::DocGroup gr("");

        Env::DocAddNum("Height", pos.m_Height);
        Env::DocAddNum("Pos", pos.m_Pos);
        Env::DocAddBlob_T("Account", pKey->m_KeyInContract.m_Account);
        Env::DocAddNum("AssetID", pKey->m_KeyInContract.m_Aid);
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
*/
bool get_Sid(ShaderID& sid)
{
    static const char szName[] = "contract.shader";
    uint32_t nSize = Env::DocGetBlob(szName, nullptr, 0);
    if (!nSize)
        return 0;

    void* p = Env::StackAlloc(nSize);
    Env::DocGetBlob(szName, p, nSize);

    HashProcessor::Sha256 hp;
    hp
        << "bvm.shader.id"
        << nSize;

    hp.Write(p, nSize);
    hp >> sid;

    Env::StackFree(nSize); // no necessary, unless the compiler will inline this func

    return true;
}

export void Method_0()
{
    ShaderID sid;
    if (!get_Sid(sid))
        return OnError("contract shader must be specified");

    Env::DocAddBlob_T("sid", sid);

    ContractID cid;
    uint32_t nDeployedCount = 0;

    {
        Env::DocArray gr("contracts");

        WalkerContracts wlk;
        for (wlk.Enum(sid); wlk.MoveNext(); nDeployedCount++)
        {
            Env::DocGroup root("");

            Env::DocAddBlob_T("cid", wlk.m_Key.m_KeyInContract.m_Cid);
            Env::DocAddNum("Height", wlk.m_Height);

            _POD_(cid) = wlk.m_Key.m_KeyInContract.m_Cid;
        }
    }

    if (!nDeployedCount)
    {
        uint32_t nVal = 0;
        Env::DocGetNum32("deploy", &nVal);

        if (!nVal)
        {
            Env::DocAddText("res", "Not deployed. Specify deploy=1 to auto deploy");
            return;
        }

        Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "create playground contract", 0);

        HashProcessor::Sha256 hp;
        hp
            << "bvm.cid"
            << sid
            << 0U // args size
            >> cid;
    }

    uint32_t nRun = 0;
    Env::DocGetNum32("run", &nRun);

    if (!nRun)
        return;

    if (nDeployedCount > 1)
    {
        if (!Env::DocGet("cid", cid))
        {
            Env::DocAddText("res", "cid is ambiguous. Please sepcify");
            return;
        }
    }

    Env::GenerateKernel(&cid, 2, nullptr, 0, nullptr, 0, nullptr, 0, "play in playground", 100000000);
}

export void Method_1()
{
    Method_0(); // make no difference
}
