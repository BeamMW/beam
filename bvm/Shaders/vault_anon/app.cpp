#include "app_impl.h"
#include "../app_common_impl.h"

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

#define VaultAnon_user_send_anon_many(macro) macro(ContractID, cid)

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
    macro(user, send_anon_many) \
    macro(user, receive_raw) \
    macro(user, receive_anon)



#define VaultAnon_code_view(macro) macro(ContractID, cid)
#define VaultAnon_code_my_key(macro) macro(ContractID, cid)

#define VaultAnon_code_send(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define VaultAnon_code_receive(macro) macro(ContractID, cid)

#define VaultAnonRole_code(macro) \
    macro(code, view) \
    macro(code, my_key) \
    macro(code, send) \
    macro(code, receive)

#define VaultAnonRoles_All(macro) \
    macro(manager) \
    macro(user) \
    macro(code)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

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

struct MyKeyID :public Env::KeyID
{
    MyKeyID(const ContractID& cid) {
        m_pID = &cid;
        m_nID = sizeof(cid);
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

struct MyAccountsPrinter
    :public WalkerAccounts_Print
{
    static void PrintStat(char* szBuf, const uint8_t* pMsg, uint32_t nMsg)
    {
        Env::DocAddBlob("custom", pMsg, nMsg);

        char szTxt[s_MaxMsgSize + 1];
        Env::Memcpy(szTxt, pMsg, nMsg);
        szTxt[nMsg] = 0;
        Env::DocAddText("custom_txt", szTxt);
    }

    void PrintMsg(bool bIsAnon, const uint8_t* pMsg, uint32_t nMsg) override
    {
        if (nMsg)
        {
            char szTxt[s_MaxMsgSize + 1];
            PrintStat(szTxt, pMsg, nMsg);
        }
    }
};

ON_METHOD(manager, view_account)
{
    Env::DocArray gr("accounts");
    MyAccountsPrinter wlk;
    wlk.Proceed(cid, _POD_(pkOwner).IsZero() ? nullptr : &pkOwner, nullptr);
}

ON_METHOD(user, view_raw)
{
    MyAccountsPrinter prnt;
    OnUser_view_raw(cid, MyKeyID(cid), prnt);
}

ON_METHOD(user, view_anon)
{
    MyAccountsPrinter prnt;
    OnUser_view_anon(cid, MyKeyID(cid), prnt);
}

ON_METHOD(user, my_key)
{
    PubKey pk;
    MyKeyID(cid).get_Pk(pk);

    Env::DocGroup gr("res");
    Env::DocAddBlob_T("key", pk);
}

ON_METHOD(user, send_raw)
{
    OnUser_send_raw(cid, pkOwner, aid, amount);
}

ON_METHOD(user, send_anon)
{
    uint8_t pMsg[s_MaxMsgSize];
    auto nMsg = Env::DocGetBlob("msg", pMsg, sizeof(pMsg));
    if (nMsg > sizeof(pMsg))
        return OnError("msg too long");

    OnUser_send_anon(cid, pkOwner, aid, amount, pMsg, nMsg);
}

ON_METHOD(user, send_anon_many)
{
    const uint32_t nMaxEntries = 1000;
    auto fPk = Utils::MakeFieldIndex<nMaxEntries>("pk_");
    auto fAmount = Utils::MakeFieldIndex<nMaxEntries>("amount_");
    auto fAid = Utils::MakeFieldIndex<nMaxEntries>("aid_");
    auto fMsg = Utils::MakeFieldIndex<nMaxEntries>("msg_");

    for (uint32_t i = 0; i < nMaxEntries; i++)
    {
        PubKey pk;
        fPk.Set(i);
        if (!Env::DocGet(fPk.m_sz, pk))
            break;

        Amount amount;
        fAmount.Set(i);
        if (!Env::DocGet(fAmount.m_sz, amount))
            break;

        char szMsg[s_MaxMsgSize + 1];
        fMsg.Set(i);
        auto nMsg = Env::DocGetText(fMsg.m_sz, szMsg, sizeof(szMsg));
        if (nMsg > sizeof(szMsg))
            break;
        if (!nMsg)
            break;

        AssetID aid;
        fAid.Set(i);
        if (!Env::DocGet(fAid.m_sz, aid))
            aid = 0;

        OnUser_send_anon(cid, pk, aid, amount, szMsg, nMsg - 1);
    }

}

ON_METHOD(user, receive_raw)
{
    OnUser_receive_raw(cid, MyKeyID(cid), aid, amount);

}

ON_METHOD(user, receive_anon)
{
    OnUser_receive_anon(cid, MyKeyID(cid), pkOwner, aid, amount);
}

struct CodeKey
{
    HashValue m_hv;
    Secp::Scalar m_sk;
    PubKey m_pk;

    bool Read()
    {
        char szKey[0x100];

        uint32_t n = Env::DocGetText("code", szKey, sizeof(szKey));
        if (!n)
        {
            OnError("code missing");
            return false;
        }
        if (n > _countof(szKey))
        {
            OnError("code too long");
            return false;
        }

        {
            HashProcessor::Sha256 hp;
            hp.Write(szKey, n); // including 0-term
            hp >> m_hv;
        }

        ImportScalar(m_sk, m_hv);

        Secp::Point pt;
        Env::Secp_Point_mul_G(pt, m_sk);
        pt.Export(m_pk);

        return true;
    }

    void XCode(uint8_t* pMsg, uint32_t nMsg) const
    {
        HashProcessor::Sha256 hp;
        hp << m_hv << nMsg;
        XcodeMsg(hp, pMsg, nMsg);
    }
};

ON_METHOD(code, my_key)
{
    CodeKey ck;
    if (!ck.Read())
        return;

    Env::DocGroup gr("res");
    Env::DocAddBlob_T("key", ck.m_pk);
}

struct CodeAccountWalker
{
    Env::Key_T<Account::KeyMax> m_Key;
    uint32_t m_nMsg;
    Account m_Acc;

    Env::VarReaderEx<true> m_R;

    void Enum(const ContractID& cid, const CodeKey& ck)
    {
        Env::Key_T<Account::Key0> k0;
        _POD_(k0.m_Prefix.m_Cid) = cid;
        _POD_(k0.m_KeyInContract.m_pkOwner) = ck.m_pk;
        k0.m_KeyInContract.m_Aid = 0;
        k0.m_KeyInContract.m_Tag = Tags::s_Account;

        _POD_(m_Key.m_Prefix.m_Cid) = cid;
        _POD_(m_Key.m_KeyInContract).SetObject(0xff);
        _POD_(m_Key.m_KeyInContract.m_pkOwner) = ck.m_pk;
        m_Key.m_KeyInContract.m_Tag = Tags::s_Account;

        m_R.Enum_T(k0, m_Key);
    }

    bool MoveNext()
    {
        while (true)
        {
            uint32_t nKey = sizeof(m_Key), nValue = sizeof(m_Acc);
            if (!m_R.MoveNext(&m_Key, nKey, &m_Acc, nValue, 0))
                break;

            if (nValue != sizeof(m_Acc))
                continue;

            m_nMsg = nKey - sizeof(Env::Key_T<Account::Key0>);
            if (m_nMsg > Account::s_CustomMaxSize)
                continue;

            return true;
        }

        return false;
    }
};

ON_METHOD(code, view)
{
    CodeKey ck;
    if (!ck.Read())
        return;

    Env::DocArray gr("res");

    CodeAccountWalker wlk;
    for (wlk.Enum(cid, ck); wlk.MoveNext(); )
    {
        Env::DocGroup gr1("");

        Env::DocAddNum("aid", wlk.m_Key.m_KeyInContract.m_Aid);
        Env::DocAddNum("amount", wlk.m_Acc.m_Amount);

        if (wlk.m_nMsg)
        {
            ck.XCode(wlk.m_Key.m_KeyInContract.m_pCustom, wlk.m_nMsg);

            char szTxt[Account::s_CustomMaxSize + 1];
            MyAccountsPrinter::PrintStat(szTxt, wlk.m_Key.m_KeyInContract.m_pCustom, wlk.m_nMsg);
        }
    }
}

ON_METHOD(code, send)
{
    CodeKey ck;
    if (!ck.Read())
        return;

    if (!amount)
        return OnError("amount not specified");

#pragma pack (push, 1)
    struct MyDeposit
        :public Method::Deposit
    {
        uint8_t m_pMsg[Account::s_CustomMaxSize];
    };
#pragma pack (pop)

    MyDeposit arg;
    _POD_(arg.m_Key.m_pkOwner) = ck.m_pk;
    arg.m_Key.m_Aid = aid;
    arg.m_Amount = amount;
    arg.m_SizeCustom = 0;

    char szMsg[Account::s_CustomMaxSize + 1];
    arg.m_SizeCustom = Env::DocGetText("msg", szMsg, sizeof(szMsg));
    if (arg.m_SizeCustom)
    {
        if (arg.m_SizeCustom > sizeof(szMsg))
        {
            OnError("msg too long");
            return;
        }

        if (--arg.m_SizeCustom)
        {
            Env::Memcpy(arg.m_pMsg, szMsg, arg.m_SizeCustom);
            ck.XCode(arg.m_pMsg, arg.m_SizeCustom);
        }
    }

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(Method::Deposit) + arg.m_SizeCustom, &fc, 1, nullptr, 0, "vault_anon send to code", 0);
}

ON_METHOD(code, receive)
{
    CodeKey ck;
    if (!ck.Read())
        return;

#pragma pack (push, 1)
    struct MyWithdraw
        :public Method::Withdraw
    {
        uint8_t m_pMsg[Account::s_CustomMaxSize];
    };
#pragma pack (pop)

    MyWithdraw arg;
    _POD_(arg.m_Key.m_pkOwner) = ck.m_pk;

    uint32_t nTxs = 0;
    Height h = Env::get_Height();

    CodeAccountWalker wlk;
    for (wlk.Enum(cid, ck); wlk.MoveNext(); nTxs++)
    {
        arg.m_Key.m_Aid = wlk.m_Key.m_KeyInContract.m_Aid;
        arg.m_Amount = wlk.m_Acc.m_Amount;

        arg.m_SizeCustom = wlk.m_nMsg;
        Env::Memcpy(arg.m_pMsg, wlk.m_Key.m_KeyInContract.m_pCustom, arg.m_SizeCustom);

        FundsChange fc;
        fc.m_Aid = arg.m_Key.m_Aid;
        fc.m_Amount = arg.m_Amount;
        fc.m_Consume = 0;

        static const uint32_t s_KrnBlind = 0, s_KrnNonce = 1;
        Secp::Point ptKrnBlind, ptFullNonce;

        ptKrnBlind.FromSlot(s_KrnBlind);
        ptFullNonce.FromSlot(s_KrnNonce);

        PubKey pkKrnBlind, pkFullNonce;
        ptKrnBlind.Export(pkKrnBlind);
        ptFullNonce.Export(pkFullNonce);

        Height h0 = Env::get_Height();
        Height h1 = h0 + 15;

        // obtain challenge
        Secp_scalar_data skSig;
        Env::GenerateKernelAdvanced(&cid, arg.s_iMethod, &arg, sizeof(Method::Deposit) + arg.m_SizeCustom, &fc, 1, &arg.m_Key.m_pkOwner, 1, "", 0, h0, h1, pkKrnBlind, pkFullNonce, skSig, s_KrnBlind, s_KrnNonce, &skSig);

        Secp::Scalar e;
        e.Import(skSig);
        Env::Secp_Scalar_mul(e, e, ck.m_sk);
        e.Export(skSig);

        Env::GenerateKernelAdvanced(&cid, arg.s_iMethod, &arg, sizeof(Method::Deposit) + arg.m_SizeCustom, &fc, 1, &arg.m_Key.m_pkOwner, 1, "vault_anon withdraw by code", 0, h0, h1, pkKrnBlind, pkFullNonce, skSig, s_KrnBlind, s_KrnNonce, nullptr);
    }
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
