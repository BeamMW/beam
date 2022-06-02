#pragma once
#include "../common.h"
#include "contract.h"

void OnError(const char* sz);

namespace VaultAnon {


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
    Env::KeyID m_Kid;

    AnonScanner(const Env::KeyID& kid)
        :m_Kid(kid)
    {
        kid.get_Pk(m_ptMy);
    }

    bool Recognize(const PubKey& pkSender, const PubKey& pkSpend, const ContractID& cid)
    {
        Secp::Point pt;
        if (!pt.Import(pkSender))
            return false;

        Env::get_PkEx(pt, pt, m_Kid.m_pID, m_Kid.m_nID);

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

void ViewAccounts(const ContractID& cid, const PubKey* pOwner, AnonScanner* pAnon, const char* szArr)
{
    Env::Key_T<Account::Key0> k0;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract).SetZero();

    AccountReader ar(cid);

    if (pOwner)
    {
        _POD_(k0.m_KeyInContract.m_pkOwner) = *pOwner;
        _POD_(ar.m_Key.m_KeyInContract.m_pkOwner) = *pOwner;
    }

    Env::DocArray gr(szArr);

    for (Env::VarReader r(k0, ar.m_Key); ar.MoveNext(r); )
    {
        if (pAnon && !ar.Recognize(*pAnon))
            continue;

        Env::DocGroup gr("");

        if (!pOwner)
            Env::DocAddBlob_T("pk", ar.m_Key.m_KeyInContract.m_pkOwner);

        Env::DocAddNum("aid", ar.m_Key.m_KeyInContract.m_Aid);
        Env::DocAddNum("amount", ar.m_Amount);
        Env::DocAddBlob("custom", ar.m_Key.m_KeyInContract.m_pCustom, ar.m_SizeCustom);
    }
}

void OnUser_view_raw(const ContractID& cid, const Env::KeyID& kid)
{
    PubKey pk;
    kid.get_Pk(pk);
    ViewAccounts(cid, &pk, nullptr, "raw");
}

void OnUser_view_anon(const ContractID& cid, const Env::KeyID& kid)
{
    AnonScanner as(kid);
    ViewAccounts(cid, nullptr, &as, "anon");
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

void OnUser_send_raw(const ContractID& cid, const PubKey& pkOwner, AssetID aid, Amount amount)
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

void OnUser_send_anon(const ContractID& cid, const PubKey& pkOwner, AssetID aid, Amount amount)
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

void OnUser_receive_raw(const ContractID& cid, const Env::KeyID& kid, AssetID aid, Amount amount)
{
    Method::Withdraw arg;
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

void OnUser_receive_anon(const ContractID& cid, const Env::KeyID& kid, const PubKey& pkOwner, AssetID aid, Amount amount)
{
    AnonScanner as(kid);

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

    Secp::Scalar e, skRes;
    e.Import(skSig);
    kid.get_Blind(skRes, e, s_MyNonce);

    Env::Secp_Scalar_mul(as.m_sk, as.m_sk, e);
    skRes += as.m_sk;
    skRes.Export(skSig);

    Env::GenerateKernelAdvanced(&cid, Method::Withdraw::s_iMethod, &arg, sizeof(arg), &fc, 1, &pkOwner, 1, "vault_anon receive", 0, h0, h1, pkKrnBlind, pkFullNonce, skSig, s_KrnBlind, s_KrnNonce, nullptr);
}


} // namespace VaultAnon
