#pragma once
#include "../common.h"
#include "contract.h"

void OnError(const char* sz);

namespace VaultAnon {

static const uint32_t s_MaxMsgSize = Account::s_CustomMaxSize - sizeof(PubKey);

void ImportScalar(Secp::Scalar& res, HashValue& hv)
{
    while (true)
    {
        if (res.Import(Cast::Reinterpret<Secp_scalar_data>(hv)))
            break;

        HashProcessor::Sha256() << hv >> hv;
    }
}

void get_DH_Key(Secp::Scalar& res, PubKey& pkShared, const Secp::Point& ptShared)
{
    ptShared.Export(pkShared);
    ImportScalar(res, pkShared.m_X);
}

void XcodeMsg(const PubKey& pkShared, uint8_t* pMsg, uint32_t nMsg)
{
    if (!nMsg)
        return;

    HashProcessor::Sha256 hp;
    hp << pkShared.m_X;

    while (true)
    {
        HashValue hv;
        hp >> hv;

        for (auto i = std::min<uint32_t>(sizeof(hv), nMsg); i--; )
            pMsg[i] ^= hv.m_p[i];

        if (nMsg <= sizeof(hv))
            break;

        pMsg += sizeof(hv);
        nMsg -= sizeof(hv);

        hp << hv;
    }
}

struct AnonScanner
{
    Secp::Point m_ptMy;
    Secp::Scalar m_sk;
    PubKey m_pkShared;
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

        get_DH_Key(m_sk, m_pkShared, pt);
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
        m_Key.m_KeyInContract.m_Tag = Tags::s_Account;
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


struct WalkerAccounts
{
    virtual bool OnAccount(const PubKey& pkOwner, AssetID, Amount, bool bIsAnon, const uint8_t* pMsg, uint32_t nMsg)
    {
        return true;
    }

    virtual bool OnAccount(AccountReader& ar, const AnonScanner* pAnon)
    {
        uint8_t* pMsg = ar.m_Key.m_KeyInContract.m_pCustom;
        uint32_t nMsg = ar.m_SizeCustom;

        if (pAnon)
        {
            pMsg += sizeof(PubKey);
            nMsg -= sizeof(PubKey);
            XcodeMsg(pAnon->m_pkShared, pMsg, nMsg);
        }

        return OnAccount(
            ar.m_Key.m_KeyInContract.m_pkOwner,
            ar.m_Key.m_KeyInContract.m_Aid,
            ar.m_Amount,
            !!pAnon,
            pMsg,
            nMsg);
    }

    bool Proceed(const ContractID& cid, const PubKey* pOwner, AnonScanner* pAnon)
    {
        Env::Key_T<Account::Key0> k0;
        _POD_(k0.m_Prefix.m_Cid) = cid;
        _POD_(k0.m_KeyInContract).SetZero();
        k0.m_KeyInContract.m_Tag = Tags::s_Account;

        AccountReader ar(cid);

        if (pOwner)
        {
            _POD_(k0.m_KeyInContract.m_pkOwner) = *pOwner;
            _POD_(ar.m_Key.m_KeyInContract.m_pkOwner) = *pOwner;
        }

        for (Env::VarReader r(k0, ar.m_Key); ar.MoveNext(r); )
        {
            if (pAnon && !ar.Recognize(*pAnon))
                continue;

            if (!OnAccount(ar, pAnon))
                return false;
        }

        return true;
    }
};

struct WalkerAccounts_Print
    :public WalkerAccounts
{
    bool OnAccount(const PubKey& pkOwner, AssetID aid, Amount amount, bool bIsAnon, const uint8_t* pMsg, uint32_t nMsg) override
    {
        Env::DocGroup gr("");

        if (bIsAnon)
            Env::DocAddBlob_T("pk", pkOwner);

        Env::DocAddNum("aid", aid);
        Env::DocAddNum("amount", amount);

        PrintMsg(bIsAnon, pMsg, nMsg);

        return true;
    }

    virtual void PrintMsg(bool bIsAnon, const uint8_t* pMsg, uint32_t nMsg) {}
};

void OnUser_view_raw(const ContractID& cid, const Env::KeyID& kid, WalkerAccounts_Print& wlk)
{
    PubKey pk;
    kid.get_Pk(pk);
    Env::DocArray gr("raw");
    wlk.Proceed(cid, &pk, nullptr);
}

void OnUser_view_anon(const ContractID& cid, const Env::KeyID& kid, WalkerAccounts_Print& wlk)
{
    AnonScanner as(kid);
    Env::DocArray gr("anon");
    wlk.Proceed(cid, nullptr, &as);
}

#pragma pack (push, 1)
struct AnyTx :public Method::BaseTx
{
    uint32_t get_Size() const
    {
        return sizeof(Method::BaseTx) + m_SizeCustom;
    }
};

struct RawTx :public AnyTx
{
    uint8_t m_pMsg[Account::s_CustomMaxSize];
};

struct AnonTx :public AnyTx
{
    PubKey m_pkSender;
    uint8_t m_pMsg[s_MaxMsgSize];
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

void OnUser_send_anon(const ContractID& cid, const PubKey& pkOwner, AssetID aid, Amount amount, const void* pMsg, uint32_t nMsg)
{
    if (_POD_(pkOwner).IsZero())
        return OnError("account not specified");
    if (!amount)
        return OnError("amount not specified");
    if (nMsg > s_MaxMsgSize)
    {
        Env::DocAddNum("nMsg", nMsg);
        return OnError("message too long");
    }

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
    PubKey pkShared;
    get_DH_Key(sk, pkShared, ptDh);
    Env::Secp_Point_mul_G(ptDh, sk);

    // spend key
    pt += ptDh;
    pt.Export(arg.m_Key.m_pkOwner);

    arg.m_Key.m_Aid = aid;
    arg.m_Amount = amount;

    Env::Memcpy(arg.m_pMsg, pMsg, nMsg);
    XcodeMsg(pkShared, arg.m_pMsg, nMsg);
    arg.m_SizeCustom = sizeof(arg.m_pkSender) + nMsg;

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, Method::Deposit::s_iMethod, &arg, arg.get_Size(), &fc, 1, nullptr, 0, "vault_anon send anon", 0);
}

void OnUser_receive_internal_raw(const AccountReader& ar, const Env::KeyID& kid)
{
    assert(ar.m_Amount);

    RawTx arg;
    arg.m_SizeCustom = ar.m_SizeCustom;
    arg.m_Amount = ar.m_Amount;
    arg.m_Key.m_Aid = ar.m_Key.m_KeyInContract.m_Aid;
    _POD_(arg.m_Key.m_pkOwner) = ar.m_Key.m_KeyInContract.m_pkOwner;
    Env::Memcpy(arg.m_pMsg, ar.m_Key.m_KeyInContract.m_pCustom, ar.m_SizeCustom);

    FundsChange fc;
    fc.m_Aid = arg.m_Key.m_Aid;
    fc.m_Amount = arg.m_Amount;
    fc.m_Consume = 0;

    Env::GenerateKernel(&ar.m_Key.m_Prefix.m_Cid, Method::Withdraw::s_iMethod, &arg, arg.get_Size(), &fc, 1, &kid, 1, "vault_anon receive raw", 0);
}

void OnUser_receive_raw(const ContractID& cid, const Env::KeyID& kid, AssetID aid, Amount amount)
{
    PubKey pkOwner;
    kid.get_Pk(pkOwner);

    AccountReader ar(cid);
    if (!ar.ReadMy(aid, pkOwner, nullptr))
        return OnError("no funds");

    assert(ar.m_Amount);
    if (amount)
    {
        if (amount > ar.m_Amount)
            return OnError("insufficient funds");
    }
    else
        amount = ar.m_Amount; // withdraw all

    OnUser_receive_internal_raw(ar, kid);
}

void OnUser_receive_internal_anon(const AccountReader& ar, const AnonScanner& as, const Env::KeyID& kid)
{
    assert(ar.m_Amount);

    AnonTx arg;
    arg.m_Key.m_Aid = ar.m_Key.m_KeyInContract.m_Aid;
    arg.m_Amount = ar.m_Amount;
    _POD_(arg.m_Key.m_pkOwner) = ar.m_Key.m_KeyInContract.m_pkOwner;
    Env::Memcpy(&arg.m_pkSender, ar.m_Key.m_KeyInContract.m_pCustom, ar.m_SizeCustom);
    arg.m_SizeCustom = ar.m_SizeCustom;

    FundsChange fc;
    fc.m_Aid = arg.m_Key.m_Aid;
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
    Env::GenerateKernelAdvanced(&ar.m_Key.m_Prefix.m_Cid, Method::Withdraw::s_iMethod, &arg, arg.get_Size(), &fc, 1, &arg.m_Key.m_pkOwner, 1, "", 0, h0, h1, pkKrnBlind, pkFullNonce, skSig, s_KrnBlind, s_KrnNonce, &skSig);

    Secp::Scalar e, skRes;
    e.Import(skSig);
    kid.get_Blind(skRes, e, s_MyNonce);

    Env::Secp_Scalar_mul(as.m_sk, as.m_sk, e);
    skRes += as.m_sk;
    skRes.Export(skSig);

    Env::GenerateKernelAdvanced(&ar.m_Key.m_Prefix.m_Cid, Method::Withdraw::s_iMethod, &arg, arg.get_Size(), &fc, 1, &arg.m_Key.m_pkOwner, 1, "vault_anon receive", 0, h0, h1, pkKrnBlind, pkFullNonce, skSig, s_KrnBlind, s_KrnNonce, nullptr);
}

void OnUser_receive_anon(const ContractID& cid, const Env::KeyID& kid, const PubKey& pkOwner, AssetID aid, Amount amount)
{
    AnonScanner as(kid);

    AccountReader ar(cid);
    if (!ar.ReadMy(aid, pkOwner, &as))
        return OnError("not detected");

    assert(ar.m_Amount);
    if (amount)
    {
        if (amount > ar.m_Amount)
            return OnError("insufficient funds");
    }
    else
        amount = ar.m_Amount; // withdraw all

    OnUser_receive_internal_anon(ar, as, kid);
}

uint32_t OnUser_receive_All(const ContractID& cid, const Env::KeyID& kid, uint32_t nMaxOps)
{
    struct MyWalker
        :public WalkerAccounts
    {
        Env::KeyID m_Kid;
        uint32_t m_Remaining;

        bool OnAccount(AccountReader& ar, const AnonScanner* pAnon) override
        {
            if (pAnon)
                OnUser_receive_internal_anon(ar, *pAnon, m_Kid);
            else
                OnUser_receive_internal_raw(ar, m_Kid);

            return (--m_Remaining) > 0;
        }
    };

    MyWalker wlk;
    wlk.m_Kid = kid;
    wlk.m_Remaining = nMaxOps;

    if (wlk.m_Remaining)
    {
        PubKey pkOwner;
        kid.get_Pk(pkOwner);
        wlk.Proceed(cid, &pkOwner, nullptr);
    }

    if (wlk.m_Remaining)
    {
        AnonScanner as(kid);
        wlk.Proceed(cid, nullptr, &as);
    }

    return nMaxOps - wlk.m_Remaining;
}

} // namespace VaultAnon
