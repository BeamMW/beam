#include "../common.h"
#include "contract.h"
#include "../app_common_impl.h"
#include "../app_comm.h"

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
#define Vault_my_account_get_key(macro) macro(ContractID, cid)
#define Vault_my_account_get_proof(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid)

#define Vault_my_account_deposit(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkForeign) \
    macro(uint32_t, bCoSigner) \
    macro(Amount, amount) \
    macro(AssetID, aid)

#define Vault_my_account_withdraw(macro) \
    Vault_my_account_deposit(macro) \
    macro(Amount, amountCoSigner)

#define Vault_my_account_move(macro) \
    macro(uint8_t, isDeposit) \
    Vault_my_account_withdraw(macro)

#define VaultRole_my_account(macro) \
    macro(my_account, view) \
    macro(my_account, get_key) \
    macro(my_account, get_proof) \
    macro(my_account, deposit) \
    macro(my_account, withdraw)

#define VaultRoles_All(macro) \
    macro(manager) \
    macro(my_account)

BEAM_EXPORT void Method_0()
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


void DumpAccounts(Env::VarReader& r)
{
    Env::DocArray gr("accounts");

    while (true)
    {
        KeyAccount key;
        Amount amount;
        
        if (!r.MoveNext_T(key, amount))
            break;

        Env::DocGroup gr("");

        Env::DocAddBlob_T("Account", key.m_KeyInContract.m_Account);
        Env::DocAddNum("AssetID", key.m_KeyInContract.m_Aid);
        Env::DocAddNum("Amount", amount);
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

    Env::LogReader lr(k0, k1);

    Env::DocArray gr("logs");

    while (true)
    {
        Env::Key_T<Vault::Key> key;
        Amount val;

        if (!lr.MoveNext_T(key, val))
            break;

        Env::DocGroup gr("");

        Env::DocAddNum("Height", lr.m_Pos.m_Height);
        Env::DocAddNum("Pos", lr.m_Pos.m_Pos);
        Env::DocAddBlob_T("Account", key.m_KeyInContract.m_Account);
        Env::DocAddNum("AssetID", key.m_KeyInContract.m_Aid);
        Env::DocAddNum("Amount", val);
    }
}


ON_METHOD(manager, view_accounts)
{
    Env::KeyPrefix k0, k1;
    _POD_(k0.m_Cid) = cid;
    _POD_(k1.m_Cid) = cid;
    k1.m_Tag = KeyTag::Internal + 1;

    Env::VarReader r(k0, k1); // enum all internal contract vars
    DumpAccounts(r);
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

struct MultiSigProto
{
#pragma pack (push, 1)
    struct Msg1
    {
        Height m_hMin;
        PubKey m_pkSignerNonce;
        PubKey m_pkKrnBlind;
    };

    struct Msg2
    {
        PubKey m_pkFullNonce;
        PubKey m_pkKrnBlind;
        Secp_scalar_data m_kSig;
    };
#pragma pack (pop)

    Msg1 m_Msg1;
    Msg2 m_Msg2;
    PubKey m_pkFullCommitment;

    static const Height s_dh = 20;

    static const uint32_t s_iSlotNonceKey = 0;
    static const uint32_t s_iSlotKrnNonce = 1;
    static const uint32_t s_iSlotKrnBlind = 2;

    const ContractID* m_pCid;
    const Vault::Request* m_pArg;
    const FundsChange* m_pFc;

    void InvokeKrn(const Secp_scalar_data& kSig, Secp_scalar_data* pE, bool bCoSigner)
    {
        uint8_t nFlags = KernelFlag::FullCommitment;
        const PubKey* pPeer;
        if (bCoSigner)
        {
            nFlags |= KernelFlag::CoSigner;
            pPeer = &m_Msg1.m_pkKrnBlind;
        }
        else
            pPeer = &m_Msg2.m_pkKrnBlind;

        Env::GenerateKernelAdvanced2(
            m_pCid, Vault::Withdraw::s_iMethod, m_pArg, sizeof(*m_pArg), m_pFc, 1, &m_pArg->m_Account, 1, "withdraw from Vault", 0,
            m_Msg1.m_hMin, m_Msg1.m_hMin + s_dh, m_pkFullCommitment, m_Msg2.m_pkFullNonce, kSig, s_iSlotKrnBlind, s_iSlotKrnNonce, pE, nFlags, pPeer, 1);
    }

    void SetCommitment(const Secp::Point& ptMy, const PubKey& pkForeign)
    {
        Secp::Point pt;
        pt.Import(pkForeign);
        pt += ptMy;

        Secp::Scalar s;
        Env::Secp_Scalar_set(s, m_pArg->m_Amount);
        Secp::Point pt2;
        Env::Secp_Point_mul_H(pt2, s, m_pArg->m_Aid);
        pt2 = -pt2;

        pt += pt2;
        pt.Export(m_pkFullCommitment);
    }
};


ON_METHOD(my_account, move)
{
    if (!amount)
        return OnError("amount should be nnz");

    Vault::Request arg;
    arg.m_Amount = amount;
    arg.m_Aid = aid;

    MyAccountID myid;
    _POD_(myid.m_Cid) = cid;

    Env::KeyID kid(myid);
    PubKey pkMy;

    bool bIsMultisig = !_POD_(pkForeign).IsZero();
    if (bIsMultisig)
    {
        Secp::Point p0, p1;

        if (!p1.Import(pkForeign))
            return OnError("bad foreign key");

        kid.get_Pk(p0);
        p0.Export(pkMy);

        p0 += p1;
        p0.Export(arg.m_Account);
    }
    else
        kid.get_Pk(arg.m_Account);

    FundsChange fc;
    fc.m_Amount = arg.m_Amount;
    fc.m_Aid = arg.m_Aid;
    fc.m_Consume = isDeposit;

    if (bIsMultisig)
        fc.m_Amount = bCoSigner ? amountCoSigner : (arg.m_Amount - amountCoSigner);

    if (isDeposit)
        Env::GenerateKernel(&cid, Vault::Deposit::s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "deposit to Vault", 0);
    else
    {
        if (bIsMultisig)
        {
            Comm::Channel cc(kid, pkForeign);
            cc.m_Context
                << 2U // proto version 
                << cid
                << Vault::Deposit::s_iMethod
                << amountCoSigner;
            cc.m_Context.Write(&arg, sizeof(arg));

            MultiSigProto msp;
            msp.m_pCid = &cid;
            msp.m_pArg = &arg;
            msp.m_pFc = &fc;

            Height h = Env::get_Height();

            Secp::Point p0, p1;
            p0.FromSlot(msp.s_iSlotNonceKey);
            p1.FromSlot(msp.s_iSlotKrnNonce); // total nonce
            p0 += p1;
            p1.FromSlot(msp.s_iSlotKrnBlind); // blinding factor

            if (bCoSigner)
            {
                cc.m_Context << pkForeign;

                Env::WriteStr("Waiting for signer", Stream::Out);
                cc.Rcv_T(msp.m_Msg1);
                if ((msp.m_Msg1.m_hMin + 10 < h) || (msp.m_Msg1.m_hMin >= h + 20))
                    OnError("height insane");

                Secp::Point p2;
                p2.Import(msp.m_Msg1.m_pkSignerNonce);
                p2 += p0;
                p2.Export(msp.m_Msg2.m_pkFullNonce);

                p1.Export(msp.m_Msg2.m_pkKrnBlind);
                msp.SetCommitment(p1, msp.m_Msg1.m_pkKrnBlind);

                Secp_scalar_data e;
                msp.InvokeKrn(e, &e, true);

                Secp::Scalar s;
                s.Import(e);
                kid.get_Blind(s, s, msp.s_iSlotNonceKey);

                s.Export(msp.m_Msg2.m_kSig);

                // send result back
                cc.Send_T(msp.m_Msg2);

                _POD_(e).SetZero();
                msp.InvokeKrn(e, nullptr, true);
                Env::WriteStr("Negotiation is over", Stream::Out);
            }
            else
            {
                cc.m_Context << pkMy;

                msp.m_Msg1.m_hMin = h + 1;

                p0.Export(msp.m_Msg1.m_pkSignerNonce);
                p1.Export(msp.m_Msg1.m_pkKrnBlind);

                cc.Send_T(msp.m_Msg1);
                Env::WriteStr("Waiting for co-signer", Stream::Out);
                cc.Rcv_T(msp.m_Msg2);

                msp.SetCommitment(p1, msp.m_Msg2.m_pkKrnBlind);

                Secp_scalar_data e;
                msp.InvokeKrn(e, &e, false);

                Secp::Scalar s0, s1;
                s0.Import(msp.m_Msg2.m_kSig);

                s1.Import(e);
                kid.get_Blind(s1, s1, msp.s_iSlotNonceKey);

                s0 += s1; // full key
                s0.Export(e);

                msp.InvokeKrn(e, nullptr, false);
            }

        }
        else
        {
            Env::GenerateKernel(&cid, Vault::Withdraw::s_iMethod, &arg, sizeof(arg), &fc, 1, &kid, 1, "withdraw from Vault", 0);
        }
    }
}

ON_METHOD(my_account, deposit)
{
    On_my_account_move(1, cid, pkForeign, bCoSigner, amount, aid, 0);
}

ON_METHOD(my_account, withdraw)
{
    On_my_account_move(0, cid, pkForeign, bCoSigner, amount, aid, amountCoSigner);
}

ON_METHOD(my_account, view)
{
    PubKey pubKey;
    DeriveMyPk(pubKey, cid);
    DumpAccount(pubKey, cid);
}

ON_METHOD(my_account, get_key)
{
    PubKey pubKey;
    DeriveMyPk(pubKey, cid);
    Env::DocAddBlob_T("key", pubKey);
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

BEAM_EXPORT void Method_1()
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

