#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../vault_anon/app_impl.h"

#define NameService_manager_view(macro)

#define NameService_manager_deploy(macro) \
    macro(ContractID, cidDaoVault) \
    macro(ContractID, cidVault)

#define NameService_manager_pay(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define NameService_manager_view_domain(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pk)

#define NameService_manager_view_name(macro) macro(ContractID, cid)
#define NameService_manager_view_params(macro) macro(ContractID, cid)

#define NameServiceRole_manager(macro) \
    macro(manager, view) \
    macro(manager, view_params) \
    macro(manager, deploy) \
    macro(manager, view_domain) \
    macro(manager, view_name) \
    macro(manager, pay)

#define NameService_user_my_key(macro) macro(ContractID, cid)
#define NameService_user_view(macro) macro(ContractID, cid)
#define NameService_user_domain_register(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, nPeriods)

#define NameService_user_domain_extend(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, nPeriods)

#define NameService_user_domain_set_owner(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkOwner)

#define NameService_user_domain_set_price(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define NameService_user_domain_buy(macro) macro(ContractID, cid) \

#define NameService_user_receive(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkOwner) \
    macro(AssetID, aid) \
    macro(Amount, amount)

#define NameService_user_receive_list(macro) macro(ContractID, cid)
#define NameService_user_receive_all(macro) macro(ContractID, cid)

#define NameServiceRole_user(macro) \
    macro(user, my_key) \
    macro(user, view) \
    macro(user, domain_register) \
    macro(user, domain_extend) \
    macro(user, domain_set_owner) \
    macro(user, domain_set_price) \
    macro(user, domain_buy) \
    macro(user, receive) \
    macro(user, receive_list) \
    macro(user, receive_all)

#define NameServiceRoles_All(macro) \
    macro(manager) \
    macro(user)


void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

namespace NameService {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  NameService_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); NameServiceRole_##name(THE_METHOD) }
        
        NameServiceRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(NameService_##role##_##name(THE_FIELD) int unused = 0)

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(s_SID);
}

ON_METHOD(manager, deploy)
{
    Method::Create arg;
    _POD_(arg.m_Settings.m_cidDaoVault) = cidDaoVault;
    _POD_(arg.m_Settings.m_cidVault) = cidVault;

    const uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::SaveVar_For(sizeof(Settings)) +
        Env::Cost::Refs * 2 +
        Env::Cost::Cycle * 200;

    Env::GenerateKernel(nullptr, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Deploy NameService contract", nCharge);
}

void DumpName(const Domain& d)
{
    Env::DocAddBlob_T("key", d.m_pkOwner);
    Env::DocAddNum("hExpire", d.m_hExpire);

    if (d.m_Price.m_Amount)
    {
        Env::DocGroup gr("price");
        Env::DocAddNum("aid", d.m_Price.m_Aid);
        Env::DocAddNum("amount", d.m_Price.m_Amount);
    }
}

void DumpDomains(const ContractID& cid, const PubKey* pPk)
{
    Env::Key_T<Domain::Key0> k0;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_sz).SetZero();

#pragma pack (push, 1)
    struct KeyMaxPlus :public Domain::KeyMax {
        char m_chExtra;
    };
#pragma pack (pop)

    Env::Key_T<KeyMaxPlus> k1;
    _POD_(k1.m_Prefix.m_Cid) = cid;
    Env::Memset(k1.m_KeyInContract.m_sz, 0xff, Domain::s_MaxLen);

    Env::DocArray gr0("domains");

    for (Env::VarReader r(k0, k1); ; )
    {
        Domain d;
        uint32_t nKey = sizeof(k1) - 1, nVal = sizeof(d);

        if (!r.MoveNext(&k1, nKey, &d, nVal, 0))
            break;

        if (pPk && (_POD_(*pPk) != d.m_pkOwner))
            continue;

        Env::DocGroup gr("");

        uint32_t nNameLen = nKey - (sizeof(Env::KeyPrefix) + 1);
        k1.m_KeyInContract.m_sz[nNameLen] = 0;

        Env::DocAddText("name", k1.m_KeyInContract.m_sz);
        DumpName(d);
    }
}

ON_METHOD(manager, view_domain)
{
    DumpDomains(cid, _POD_(pk).IsZero() ? nullptr : &pk);
}

struct MyKeyID :public Env::KeyID
{
    MyKeyID(const ContractID& cid) {
        m_pID = &cid;
        m_nID = sizeof(cid);
    }
};

struct DomainName
{
    char m_sz[Domain::s_MaxLen + 1];
    uint32_t m_Len;

    void ReadName()
    {
        m_Len = 0;

        uint32_t nSize = Env::DocGetText("name", m_sz, Domain::s_MaxLen + 1);
        if (!nSize)
            return OnError("name not specified");

        uint32_t nLen = nSize - 1;
        if (nLen < Domain::s_MinLen)
            return OnError("name too short");

        if (nLen > Domain::s_MaxLen)
            return OnError("name too long");

        for (uint32_t i = 0; i < nLen; i++)
            if (!Domain::IsValidChar(m_sz[i]))
                return OnError("name is invalid");

        m_Len = nLen;
    }

    bool Read(const ContractID& cid, Domain& d)
    {
        Env::Key_T<Domain::KeyMax> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        Env::Memcpy(key.m_KeyInContract.m_sz, m_sz, m_Len);

        uint32_t nSize = sizeof(Env::KeyPrefix) + 1 + m_Len;

        Env::VarReader r(&key, nSize, &key, nSize);
        uint32_t nKey = 0, nVal = sizeof(d);
        return r.MoveNext(nullptr, nKey, &d, nVal, 0);
    }

    bool ReadFromName(const ContractID& cid, Domain& d)
    {
        ReadName();
        if (!m_Len)
            return false;

        if (Read(cid, d))
            return true;

        OnError("not registered");
        return false;
    }

    bool ReadOwned(const ContractID& cid, Domain& d)
    {
        if (!ReadFromName(cid, d))
            return false;

        PubKey pk;
        MyKeyID(cid).get_Pk(pk);
        if (_POD_(pk) == d.m_pkOwner)
            return true;

        OnError("owned by other");
        return false;
    }

#pragma pack (push, 1)
    template <typename TMethod>
    struct Method
        :public TMethod
    {
        char m_sz[Domain::s_MaxLen];

        void From(const DomainName& dn)
        {
            TMethod::m_NameLen = dn.m_Len;
            Env::Memcpy(m_sz, dn.m_sz, dn.m_Len);
        }

        uint32_t get_Size() const
        {
            return sizeof(TMethod) + TMethod::m_NameLen;
        }
    };
#pragma pack (pop)

};

struct MySettings
    :public Settings
{
    bool Read(const ContractID& cid)
    {
        Env::Key_T<uint8_t> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        key.m_KeyInContract = Tags::s_Settings;

        if (Env::VarReader::Read_T(key, *this))
            return true;

        OnError("state not found");
        return false;
    }
};

ON_METHOD(manager, view_params)
{
    MySettings stg;
    if (!stg.Read(cid))
        return;

    Env::DocGroup gr("res");

    Env::DocAddBlob_T("vault", stg.m_cidVault);
    Env::DocAddBlob_T("dao-vault", stg.m_cidDaoVault);
}

ON_METHOD(manager, view_name)
{
    DomainName dn;
    dn.ReadName();
    if (!dn.m_Len)
        return;

    Domain d;
    if (dn.Read(cid, d))
    {
        Env::DocGroup gr("res");
        DumpName(d);
    }
}

ON_METHOD(manager, pay)
{
    MySettings stg;
    if (!stg.Read(cid))
        return;

    Domain d;
    DomainName dn;
    if (!dn.ReadFromName(cid, d))
        return;

    if (d.IsExpired(Env::get_Height() + 1))
        return OnError("domain expired");

    Env::Memset(dn.m_sz + dn.m_Len, 0, Domain::s_MaxLen - dn.m_Len);
    VaultAnon::OnUser_send_anon(stg.m_cidVault, d.m_pkOwner, aid, amount, dn.m_sz, Domain::s_MaxLen);
}

ON_METHOD(user, view)
{
    MySettings stg;
    if (!stg.Read(cid))
        return;

    MyKeyID kid(cid);
    PubKey pk;
    kid.get_Pk(pk);

    Env::DocGroup gr("res");

    DumpDomains(cid, &pk);

    struct MyAccountsPrinter
        :public VaultAnon::WalkerAccounts_Print
    {
        void PrintMsg(bool bIsAnon, const uint8_t* pMsg, uint32_t nMsg) override
        {
            if (bIsAnon && nMsg)
            {
                nMsg = std::min(nMsg, Domain::s_MaxLen);

                char szBuf[Domain::s_MaxLen + 1];
                Env::Memcpy(szBuf, pMsg, nMsg);
                szBuf[nMsg] = 0;

                Env::DocAddText("domain", szBuf);
            }
        }
    } prnt;

    VaultAnon::OnUser_view_raw(stg.m_cidVault, kid, prnt);
    VaultAnon::OnUser_view_anon(stg.m_cidVault, kid, prnt);
}

ON_METHOD(user, my_key)
{
    PubKey pk;
    MyKeyID(cid).get_Pk(pk);

    Env::DocGroup gr("res");
    Env::DocAddBlob_T("key", pk);
}

namespace Cost
{
    uint32_t get_InvokeDomainChange()
    {
        return
            Env::Cost::CallFar +
            Env::Cost::LoadVar_For(sizeof(Domain)) +
            Env::Cost::SaveVar_For(sizeof(Domain));
    }

    uint32_t get_InvokeDomainReg()
    {
        return
            get_InvokeDomainChange() +
            Env::Cost::LoadVar_For(sizeof(Settings)) +
            Env::Cost::CallFar +
            Env::Cost::FundsLock +
            Env::Cost::Cycle * 500;
    }
}

ON_METHOD(user, domain_register)
{
    DomainName dn;
    dn.ReadName();
    if (!dn.m_Len)
        return;

    DomainName::Method<Method::Register> arg;
    arg.From(dn);

    MyKeyID kid(cid);
    kid.get_Pk(arg.m_pkOwner);

    Domain d;
    if (dn.Read(cid, d))
    {
        if (_POD_(arg.m_pkOwner) == d.m_pkOwner)
            return OnError("already owned by me");

        if (!d.IsExpired(Env::get_Height() + 1))
            return OnError("owned by other");
    }

    auto num = std::max<uint32_t>(nPeriods, 1);
    if (Domain::s_PeriodValidity * num > Domain::s_PeriodValidityMax)
        return OnError("validity period too long");

    arg.m_Periods = num;

    FundsChange fc;
    fc.m_Aid = 0;
    fc.m_Consume = 1;
    fc.m_Amount = Domain::get_Price(dn.m_Len) * num;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, arg.get_Size(), &fc, 1, nullptr, 0, "bans register domain", Cost::get_InvokeDomainReg());
}

ON_METHOD(user, domain_extend)
{
    Domain d;
    DomainName dn;
    if (!dn.ReadOwned(cid, d))
        return;

    // predict expiration height, and max number of allowed extend periods
    Height h = Env::get_Height() + 1;
    d.m_hExpire = std::max(h, d.m_hExpire);

    Height hMax = h + Domain::s_PeriodValidityMax;
    uint32_t nNumMax = (d.m_hExpire < hMax) ? (hMax - d.m_hExpire) / Domain::s_PeriodValidity : 0;

    auto num = std::max<uint32_t>(nPeriods, 1);
    if (num > nNumMax)
        return OnError("validity period too long");

    DomainName::Method<Method::Extend> arg;
    arg.From(dn);
    arg.m_Periods = num;

    FundsChange fc;
    fc.m_Aid = 0;
    fc.m_Consume = 1;
    fc.m_Amount = Domain::get_Price(dn.m_Len) * num;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, arg.get_Size(), &fc, 1, nullptr, 0, "bans extend domain", Cost::get_InvokeDomainReg());
}

ON_METHOD(user, domain_set_owner)
{
    Domain d;
    DomainName dn;
    if (!dn.ReadOwned(cid, d))
        return;

    DomainName::Method<Method::SetOwner> arg;
    arg.From(dn);

    if (_POD_(pkOwner).IsZero())
        return OnError("new owner not set");

    _POD_(arg.m_pkNewOwner) = pkOwner;

    const uint32_t nCharge =
        Cost::get_InvokeDomainChange() +
        Env::Cost::AddSig +
        Env::Cost::Cycle * 300;

    MyKeyID kid(cid);
    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, arg.get_Size(), nullptr, 0, &kid, 1, "bans domain set owner", nCharge);
}

ON_METHOD(user, domain_set_price)
{
    Domain d;
    DomainName dn;
    if (!dn.ReadOwned(cid, d))
        return;

    if ((d.m_Price.m_Aid == aid) && (d.m_Price.m_Amount == amount))
        return OnError("no change");

    DomainName::Method<Method::SetPrice> arg;
    arg.From(dn);

    arg.m_Price.m_Aid = aid;
    arg.m_Price.m_Amount = amount;

    const uint32_t nCharge =
        Cost::get_InvokeDomainChange() +
        Env::Cost::AddSig +
        Env::Cost::Cycle * 300;

    MyKeyID kid(cid);
    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, arg.get_Size(), nullptr, 0, &kid, 1, "bans domain set price", nCharge);
}

ON_METHOD(user, domain_buy)
{
    Domain d;
    DomainName dn;
    if (!dn.ReadFromName(cid, d))
        return;

    if (!d.m_Price.m_Amount)
        return OnError("not for sale");

    DomainName::Method<Method::Buy> arg;
    arg.From(dn);
    MyKeyID(cid).get_Pk(arg.m_pkNewOwner);

    FundsChange fc;
    fc.m_Aid = d.m_Price.m_Aid;
    fc.m_Amount = d.m_Price.m_Amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, arg.get_Size(), &fc, 1, nullptr, 0, "bans domain buy", Cost::get_InvokeDomainReg());
}

ON_METHOD(user, receive)
{
    MySettings stg;
    if (!stg.Read(cid))
        return;

    if (_POD_(pkOwner).IsZero())
        VaultAnon::OnUser_receive_raw(stg.m_cidVault, MyKeyID(cid), aid, amount);
    else
        VaultAnon::OnUser_receive_anon(stg.m_cidVault, MyKeyID(cid), pkOwner, aid, amount);
}

template <uint32_t nStrSize, uint32_t nMaxIndex>
struct FieldWithIndex
{
    char m_sz[nStrSize + Utils::String::Decimal::Digits<nMaxIndex>::N];

    FieldWithIndex(const char(&sz)[nStrSize])
    {
        Env::Memcpy(m_sz, sz, nStrSize);
    }

    void Set(uint32_t iIdx)
    {
        assert(iIdx <= nMaxIndex);
        Utils::String::Decimal::Print(m_sz + nStrSize - 1, iIdx);
    }
};

template <uint32_t nMaxIndex, uint32_t nStrSize>
FieldWithIndex<nStrSize, nMaxIndex> MakeFieldIndex(const char(&sz)[nStrSize])
{
    return FieldWithIndex<nStrSize, nMaxIndex>(sz);
}

ON_METHOD(user, receive_list)
{
    MySettings stg;
    if (!stg.Read(cid))
        return;

    MyKeyID key(cid);

    const uint32_t nMaxOps = 999;
    auto fKey = MakeFieldIndex<nMaxOps>("key_");
    auto fAid = MakeFieldIndex<nMaxOps>("aid_");

    for (uint32_t i = 1; i < nMaxOps; i++)
    {
        fKey.Set(i);
        fAid.Set(i);

        PubKey pkOwner;
        if (!Env::DocGet(fKey.m_sz, pkOwner))
            break;

        AssetID aid = 0;
        Env::DocGet(fAid.m_sz, aid);

        if (_POD_(pkOwner).IsZero())
            VaultAnon::OnUser_receive_raw(stg.m_cidVault, key, aid, 0);
        else
            VaultAnon::OnUser_receive_anon(stg.m_cidVault, key, pkOwner, aid, 0);
    }
#undef KEY_PREFIX
}

ON_METHOD(user, receive_all)
{
    MySettings stg;
    if (!stg.Read(cid))
        return;

    const uint32_t nMaxOps = 30;
    if (!VaultAnon::OnUser_receive_All(stg.m_cidVault, MyKeyID(cid), nMaxOps))
        OnError("nothing to withdraw");
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
            NameService_##role##_##name(PAR_READ) \
            On_##role##_##name(NameService_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        NameServiceRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    NameServiceRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace NameService
