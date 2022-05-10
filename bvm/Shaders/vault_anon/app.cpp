#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define VaultAnon_manager_view(macro)
#define VaultAnon_manager_deploy(macro)
#define VaultAnon_manager_view_account(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pk)

#define VaultAnon_manager_view_deposit(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkSpend)

#define VaultAnonRole_manager(macro) \
    macro(manager, view) \
    macro(manager, deploy) \
    macro(manager, view_account) \
    macro(manager, view_deposit)

#define VaultAnon_user_view(macro) macro(ContractID, cid)
#define VaultAnon_user_my_key(macro) macro(ContractID, cid)
#define VaultAnon_user_set_account(macro) macro(ContractID, cid)
#define VaultAnon_user_send(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkAccount) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define VaultAnon_user_receive(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkSpend)

#define VaultAnonRole_user(macro) \
    macro(user, view) \
    macro(user, my_key) \
    macro(user, set_account) \
    macro(user, send) \
    macro(user, receive)

#define VaultAnonRoles_All(macro) \
    macro(manager) \
    macro(user)

namespace VaultAnon {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  VaultAnon_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); VaultAnonRole_##name(THE_METHOD) }
        
        VaultAnonRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(VaultAnon_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

struct MyKeyID :public Env::KeyID
{
    MyKeyID(const ContractID& cid) {
        m_pID = &cid;
        m_nID = sizeof(cid);
    }
};

void ImportScalar(Secp::Scalar& res, HashValue& hv)
{
    while (true)
    {
        if (res.Import(Cast::Reinterpret<Secp_scalar_data>(hv)))
            break;

        HashProcessor::Sha256() << hv >> hv;
    }
}

void get_DH_Key(Secp::Scalar& res, const Secp::Point& ptShared)
{
    PubKey pk;
    ptShared.Export(pk);
    ImportScalar(res, pk.m_X);
}

bool get_DH_Key_My(Secp::Scalar& res, const PubKey& pkSender, const PubKey& pkSpend, const Secp::Point& ptMy, const ContractID& cid)
{
    Secp::Point pt;
    if (!pt.Import(pkSender))
        return false;

    MyKeyID kid(cid);
    Env::get_PkEx(pt, pt, kid.m_pID, kid.m_nID);

    get_DH_Key(res, pt);
    Env::Secp_Point_mul_G(pt, res);

    pt += ptMy; // should be spend key

    PubKey pk;
    pt.Export(pk);
    return _POD_(pk) == pkSpend;
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(s_SID);
}

ON_METHOD(manager, deploy)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy VaultAnon contract", 0);
}

void DumpAccounts(Env::Key_T<Account::Key>& k0, const Env::Key_T<Account::Key>& k1, const char* szGr)
{
    for (Env::VarReader r(k0, k1); ; )
    {
        char szTitle[Account::s_TitleLenMax + 1];
        uint32_t nKey = sizeof(nKey), nVal = Account::s_TitleLenMax;

        if (!r.MoveNext(&k0, nKey, szTitle, nVal, 0))
            break;

        Env::DocGroup gr(szGr);

        Env::DocAddBlob_T("key", k0.m_KeyInContract.m_Pk);
        
        szTitle[nVal] = 0;
        Env::DocAddText("title", szTitle);

    }
}

ON_METHOD(manager, view_account)
{
    Env::Key_T<Account::Key> k0;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_Pk) = pk;

    if (_POD_(pk).IsZero())
    {
        Env::Key_T<Account::Key> k1;
        _POD_(k1.m_Prefix.m_Cid) = cid;
        _POD_(k1.m_KeyInContract.m_Pk).SetObject(0xff);

        Env::DocArray gr("accounts");

        DumpAccounts(k0, k1, "");
    }
    else
        DumpAccounts(k0, k0, "account");
}

void DumpDeposit(const PubKey& pkSpend, const Deposit& d, const char* szGr)
{
    Env::DocGroup gr(szGr);

    Env::DocAddBlob_T("key", pkSpend);
    Env::DocAddNum("aid", d.m_Aid);
    Env::DocAddNum("amount", d.m_Amount);
}

void DumpDeposits(Env::Key_T<Deposit::Key>& k0, const Env::Key_T<Deposit::Key>& k1, const char* szGr, bool bOwned)
{
    Secp::Point ptMy;
    Secp::Scalar sk;
    if (bOwned)
        MyKeyID(k0.m_Prefix.m_Cid).get_Pk(ptMy);

    Deposit d;
    for (Env::VarReader r(k0, k1); r.MoveNext_T(k0, d); )
    {
        if (bOwned && !get_DH_Key_My(sk, d.m_SenderKey, k0.m_KeyInContract.m_SpendKey, ptMy, k0.m_Prefix.m_Cid))
            continue;

        DumpDeposit(k0.m_KeyInContract.m_SpendKey, d, szGr);
    }
}

void DumpDeposits(const ContractID& cid, const PubKey& pkSpend, bool bOwned)
{
    Env::Key_T<Deposit::Key> k0;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_SpendKey) = pkSpend;

    if (_POD_(pkSpend).IsZero())
    {
        Env::Key_T<Deposit::Key> k1;
        _POD_(k1.m_Prefix.m_Cid) = cid;
        _POD_(k1.m_KeyInContract.m_SpendKey).SetObject(0xff);

        Env::DocArray gr0("deposits");

        DumpDeposits(k0, k1, "", bOwned);
    }
    else
        DumpDeposits(k0, k0, "deposit", bOwned);
}

ON_METHOD(manager, view_deposit)
{
    DumpDeposits(cid, pkSpend, false);
}

ON_METHOD(user, view)
{
    PubKey pk;
    MyKeyID(cid).get_Pk(pk);

    Env::DocGroup gr("user");

    On_manager_view_account(cid, pk);

    _POD_(pk).SetZero();
    DumpDeposits(cid, pk, true);
}

ON_METHOD(user, my_key)
{
    PubKey pk;
    MyKeyID(cid).get_Pk(pk);

    Env::DocGroup gr("res");
    Env::DocAddBlob_T("key", pk);
}

ON_METHOD(user, set_account)
{
#pragma pack (push, 1)
    struct Arg
        :public Method::SetAccount
    {
        char m_szTitle[Account::s_TitleLenMax];
    } arg;
#pragma pack (pop)

    const char* szComment;
    uint32_t nRes = Env::DocGetText("title", arg.m_szTitle, Account::s_TitleLenMax);

    if (nRes > 1)
    {
        szComment = "vault_anon set account";
        arg.m_TitleLen = nRes - 1;
    }
    else
    {
        arg.m_TitleLen = 0;
        szComment = "vault_anon delete account";
    }

    MyKeyID kid(cid);
    kid.get_Pk(arg.m_Pk);

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(Method::SetAccount) + arg.m_TitleLen, nullptr, 0, &kid, 1, szComment, 0);
}


ON_METHOD(user, send)
{
    if (_POD_(pkAccount).IsZero())
        return OnError("account not specified");
    if (!amount)
        return OnError("amount not specified");

    // derive random key
    Secp::Scalar sk;
    {
        HashValue hv;
        Env::GenerateRandom(&hv, sizeof(hv));
        ImportScalar(sk, hv);
    }

    // to senderKey
    Secp::Point pt;
    Env::Secp_Point_mul_G(pt, sk);

    Method::Send arg;
    pt.Export(arg.m_Deposit.m_SenderKey);

    // obtain DH shared secret
    if (!pt.Import(pkAccount))
        return OnError("invalid account key");

    Secp::Point ptDh;
    Env::Secp_Point_mul(ptDh, pt, sk);

    // derive DH key
    get_DH_Key(sk, ptDh);
    Env::Secp_Point_mul_G(ptDh, sk);

    // spend key
    pt += ptDh;
    pt.Export(arg.m_SpendKey);

    arg.m_Deposit.m_Aid = aid;
    arg.m_Deposit.m_Amount = amount;

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "vault_anon send", 0);
}

ON_METHOD(user, receive)
{
    Env::Key_T<Deposit::Key> key;
    _POD_(key.m_Prefix.m_Cid) = cid;
    _POD_(key.m_KeyInContract.m_SpendKey) = pkSpend;

    Deposit d;
    if (!Env::VarReader::Read_T(key, d))
        return OnError("deposit not found");

    MyKeyID kid(cid);
    Secp::Point ptMy;
    kid.get_Pk(ptMy);

    Secp::Scalar sk;
    if (!get_DH_Key_My(sk, d.m_SenderKey, pkSpend, ptMy, cid))
        return OnError("deposit is not owned");

    Method::Receive arg;
    _POD_(arg.m_SpendKey) = pkSpend;

    FundsChange fc;
    fc.m_Aid = d.m_Aid;
    fc.m_Amount = d.m_Amount;
    fc.m_Consume = 0;

    static const uint32_t s_KrnBlind = 0, s_KrnNonce = 1, s_MyNonce = 2;
    Secp::Point ptKrnBlind, ptFullNonce;

    ptKrnBlind.FromSlot(s_MyNonce);
    ptFullNonce.FromSlot(s_KrnNonce);
    ptFullNonce += ptKrnBlind;

    ptKrnBlind.FromSlot(s_KrnBlind);

    PubKey pkKrnBlind, pkFullNonce;
    ptKrnBlind.Export(pkKrnBlind);
    ptFullNonce.Export(pkFullNonce);

    Height h0 = Env::get_Height();
    Height h1 = h0 + 15;

    // obtain challenge
    Secp_scalar_data skSig;
    Env::GenerateKernelAdvanced(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &pkSpend, 1, "", 0, h0, h1, pkKrnBlind, pkFullNonce, skSig, s_KrnBlind, s_KrnNonce, &skSig);

    Secp::Scalar e, skRes;
    e.Import(skSig);
    kid.get_Blind(skRes, e, s_MyNonce);

    Env::Secp_Scalar_mul(sk, sk, e);
    skRes += sk;
    skRes.Export(skSig);

    Env::GenerateKernelAdvanced(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &pkSpend, 1, "vault_anon receive", 0, h0, h1, pkKrnBlind, pkFullNonce, skSig, s_KrnBlind, s_KrnNonce, nullptr);
}

#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
{
    Env::DocGroup root("");

    char szRole[0x20], szAction[0x20];

    if (!Env::DocGetText("role", szRole, sizeof(szRole)))
        return OnError("Role not specified");

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            VaultAnon_##role##_##name(PAR_READ) \
            On_##role##_##name(VaultAnon_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        VaultAnonRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    VaultAnonRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace VaultAnon
