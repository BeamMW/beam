#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define VaultAnon_manager_view(macro)
#define VaultAnon_manager_deploy(macro)
#define VaultAnon_manager_view_account(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkOwner)

#define VaultAnonRole_manager(macro) \
    macro(manager, view) \
    macro(manager, deploy) \
    macro(manager, view_account)

#define VaultAnon_user_view_raw(macro) macro(ContractID, cid)
#define VaultAnon_user_view_anon(macro) macro(ContractID, cid)
#define VaultAnon_user_my_key(macro) macro(ContractID, cid)

#define VaultAnon_user_send_raw(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkOwner) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define VaultAnon_user_send_anon(macro) VaultAnon_user_send_raw(macro)

#define VaultAnon_user_receive_raw(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define VaultAnon_user_receive_anon(macro) \
    VaultAnon_user_receive_raw(macro) \
    macro(PubKey, pkOwner)

#define VaultAnonRole_user(macro) \
    macro(user, view_raw) \
    macro(user, view_anon) \
    macro(user, my_key) \
    macro(user, send_raw) \
    macro(user, send_anon) \
    macro(user, receive_raw) \
    macro(user, receive_anon)

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

struct AnonScanner
{
    Secp::Point m_ptMy;
    Secp::Scalar m_sk;

    void Init(const ContractID& cid)
    {
        MyKeyID(cid).get_Pk(m_ptMy);
    }

    bool Recognize(const PubKey& pkSender, const PubKey& pkSpend, const ContractID& cid)
    {
        Secp::Point pt;
        if (!pt.Import(pkSender))
            return false;

        MyKeyID kid(cid);
        Env::get_PkEx(pt, pt, kid.m_pID, kid.m_nID);

        get_DH_Key(m_sk, pt);
        Env::Secp_Point_mul_G(pt, m_sk);

        pt += m_ptMy; // should be spend key

        PubKey pk;
        pt.Export(pk);
        return _POD_(pk) == pkSpend;
    }
};

struct AccountReader
{
    Amount m_Amount;
    Env::Key_T<Account::KeyMax> m_Key;
    uint32_t m_SizeCustom;

    AccountReader(const ContractID& cid)
    {
        _POD_(m_Key.m_Prefix.m_Cid) = cid;
        _POD_(m_Key.m_KeyInContract).SetObject(0xff);
    }

    bool MoveNext(Env::VarReader& r)
    {
        while (true)
        {
            uint32_t nKey = sizeof(m_Key), nVal = sizeof(m_Amount);

            if (!r.MoveNext(&m_Key, nKey, &m_Amount, nVal, 0))
                return false;

            if ((sizeof(m_Amount) == nVal) && (nKey >= sizeof(Env::Key_T<Account::Key0>)))
            {
                m_SizeCustom = nKey - sizeof(Env::Key_T<Account::Key0>);
                break;
            }
        }
        return true;
    }

    bool Recognize(AnonScanner& as) const
    {
        if (m_SizeCustom < sizeof(PubKey))
            return false;

        return as.Recognize(get_PkSender(), m_Key.m_KeyInContract.m_pkOwner, m_Key.m_Prefix.m_Cid);
    }

    const PubKey& get_PkSender() const
    {
        static_assert(sizeof(PubKey) <= sizeof(m_Key.m_KeyInContract.m_pCustom));
        return *(PubKey*) m_Key.m_KeyInContract.m_pCustom;
    }

    bool ReadMy(AssetID aid, const PubKey& pkOwner, AnonScanner* pAs)
    {
        Env::Key_T<Account::Key0> k0;
        _POD_(k0.m_Prefix.m_Cid) = m_Key.m_Prefix.m_Cid;
        _POD_(k0.m_KeyInContract.m_pkOwner) = pkOwner;
        k0.m_KeyInContract.m_Aid = aid;

        _POD_(m_Key.m_KeyInContract.m_pkOwner) = pkOwner;
        m_Key.m_KeyInContract.m_Aid = aid;

        for (Env::VarReader r(k0, m_Key); MoveNext(r); )
        {
            if (!pAs)
                return true;

            if (Recognize(*pAs))
                return true;
        }

        return false;
    }
};

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(s_SID);
}

ON_METHOD(manager, deploy)
{
    Env::GenerateKernel(nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, "Deploy VaultAnon contract", 0);
}

void ViewAccounts(const ContractID& cid, const PubKey* pOwner, bool bAnonOwned)
{
    AnonScanner as;
    if (bAnonOwned)
        as.Init(cid);

    Env::Key_T<Account::Key0> k0;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract).SetZero();

    AccountReader ar(cid);

    if (pOwner)
    {
        _POD_(k0.m_KeyInContract.m_pkOwner) = *pOwner;
        _POD_(ar.m_Key.m_KeyInContract.m_pkOwner) = *pOwner;
    }

    Env::DocArray gr("accounts");

    for (Env::VarReader r(k0, ar.m_Key); ar.MoveNext(r); )
    {
        if (bAnonOwned && !ar.Recognize(as))
            continue;

        Env::DocGroup gr("");

        if (!pOwner)
            Env::DocAddBlob_T("pk", ar.m_Key.m_KeyInContract.m_pkOwner);

        Env::DocAddNum("aid", ar.m_Key.m_KeyInContract.m_Aid);
        Env::DocAddNum("amount", ar.m_Amount);
        Env::DocAddBlob("custom", ar.m_Key.m_KeyInContract.m_pCustom, ar.m_SizeCustom);
    }
}

ON_METHOD(manager, view_account)
{
    ViewAccounts(cid, _POD_(pkOwner).IsZero() ? nullptr : &pkOwner, false);
}

ON_METHOD(user, view_raw)
{
    PubKey pk;
    MyKeyID(cid).get_Pk(pk);
    ViewAccounts(cid, &pk, false);
}

ON_METHOD(user, view_anon)
{
    ViewAccounts(cid, nullptr, true);
}

ON_METHOD(user, my_key)
{
    PubKey pk;
    MyKeyID(cid).get_Pk(pk);

    Env::DocGroup gr("res");
    Env::DocAddBlob_T("key", pk);
}

#pragma pack (push, 1)
struct AnonTx :public Method::BaseTx
{
    PubKey m_pkSender;
    AnonTx()
    {
        m_SizeCustom = sizeof(m_pkSender);
    }
};
#pragma pack (pop)

ON_METHOD(user, send_raw)
{
    if (_POD_(pkOwner).IsZero())
        return OnError("account not specified");
    if (!amount)
        return OnError("amount not specified");

    Method::Deposit arg;
    _POD_(arg.m_Key.m_pkOwner) = pkOwner;
    arg.m_Key.m_Aid = aid;
    arg.m_Amount = amount;
    arg.m_SizeCustom = 0;

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "vault_anon send raw", 0);
}

ON_METHOD(user, send_anon)
{
    if (_POD_(pkOwner).IsZero())
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


    AnonTx arg;
    pt.Export(arg.m_pkSender);

    // obtain DH shared secret
    if (!pt.Import(pkOwner))
        return OnError("invalid account key");

    Secp::Point ptDh;
    Env::Secp_Point_mul(ptDh, pt, sk);

    // derive DH key
    get_DH_Key(sk, ptDh);
    Env::Secp_Point_mul_G(ptDh, sk);

    // spend key
    pt += ptDh;
    pt.Export(arg.m_Key.m_pkOwner);

    arg.m_Key.m_Aid = aid;
    arg.m_Amount = amount;

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, Method::Deposit::s_iMethod, &arg, sizeof(arg), &fc, 1, nullptr, 0, "vault_anon send anon", 0);
}

ON_METHOD(user, receive_raw)
{
    Method::Withdraw arg;
    MyKeyID kid(cid);
    kid.get_Pk(arg.m_Key.m_pkOwner);

    AccountReader ar(cid);
    if (!ar.ReadMy(aid, arg.m_Key.m_pkOwner, nullptr))
        return OnError("no funds");

    arg.m_Key.m_Aid = aid;
    arg.m_Amount = amount;
    arg.m_SizeCustom = 0;

    assert(ar.m_Amount);
    if (arg.m_Amount)
    {
        if (arg.m_Amount > ar.m_Amount)
            return OnError("insufficient funds");
    }
    else
        arg.m_Amount = ar.m_Amount; // withdraw all

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = arg.m_Amount;
    fc.m_Consume = 0;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &kid, 1, "vault_anon receive raw", 0);
}

ON_METHOD(user, receive_anon)
{
    AnonScanner as;
    as.Init(cid);

    AccountReader ar(cid);
    if (!ar.ReadMy(aid, pkOwner, &as))
        return OnError("not detected");

    AnonTx arg;
    arg.m_Amount = amount;
    _POD_(arg.m_pkSender) = ar.get_PkSender(); // TODO - there can be more custom data, should just be appended

    assert(ar.m_Amount);
    if (arg.m_Amount)
    {
        if (arg.m_Amount > ar.m_Amount)
            return OnError("insufficient funds");
    }
    else
        arg.m_Amount = ar.m_Amount; // withdraw all

    _POD_(arg.m_Key.m_pkOwner) = pkOwner;
    arg.m_Key.m_Aid = aid;

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = arg.m_Amount;
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
    Env::GenerateKernelAdvanced(&cid, Method::Withdraw::s_iMethod, &arg, sizeof(arg), &fc, 1, &pkOwner, 1, "", 0, h0, h1, pkKrnBlind, pkFullNonce, skSig, s_KrnBlind, s_KrnNonce, &skSig);

    MyKeyID kid(cid);
    Secp::Scalar e, skRes;
    e.Import(skSig);
    kid.get_Blind(skRes, e, s_MyNonce);

    Env::Secp_Scalar_mul(as.m_sk, as.m_sk, e);
    skRes += as.m_sk;
    skRes.Export(skSig);

    Env::GenerateKernelAdvanced(&cid, Method::Withdraw::s_iMethod, &arg, sizeof(arg), &fc, 1, &pkOwner, 1, "vault_anon receive", 0, h0, h1, pkKrnBlind, pkFullNonce, skSig, s_KrnBlind, s_KrnNonce, nullptr);
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
