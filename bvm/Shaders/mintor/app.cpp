#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"

#define Mintor_manager_view(macro)
#define Mintor_manager_view_params(macro) macro(ContractID, cid)
#define Mintor_manager_view_token(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid)

#define Mintor_manager_deploy(macro) \
    macro(ContractID, cidDaoVault) \
    macro(Amount, tokenIssueFee)

#define MintorRole_manager(macro) \
    macro(manager, view) \
    macro(manager, deploy) \
    macro(manager, view_params) \
    macro(manager, view_token) \

#define Mintor_user_view(macro) macro(ContractID, cid)

#define Mintor_user_create(macro) \
    macro(ContractID, cid) \
    macro(Amount, limit) \
    macro(Amount, limitHi)

#define Mintor_user_withdraw(macro) \
    macro(ContractID, cid) \
    macro(AssetID, aid) \
    macro(Amount, value)

#define MintorRole_user(macro) \
    macro(user, view) \
    macro(user, create) \
    macro(user, withdraw)

#define MintorRoles_All(macro) \
    macro(manager) \
    macro(user)

namespace Mintor {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Mintor_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); MintorRole_##name(THE_METHOD) }
        
        MintorRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Mintor_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

ON_METHOD(manager, view)
{
    EnumAndDumpContracts(s_SID);
}

ON_METHOD(manager, deploy)
{
    Mintor::Method::Init arg;
    _POD_(arg.m_Settings.m_cidDaoVault) = cidDaoVault;
    arg.m_Settings.m_IssueFee = tokenIssueFee;

    Env::GenerateKernel(nullptr, 0, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Deploy Mintor contract", 0);
}

bool LoadSettings(Settings& s, const ContractID& cid)
{
    Env::Key_T<uint8_t> k;
    _POD_(k.m_Prefix.m_Cid) = cid;
    k.m_KeyInContract = Tags::s_Settings;

    if (Env::VarReader::Read_T(k, s))
        return true;

    OnError("no such a contract");
    return false;
}

ON_METHOD(manager, view_params)
{
    Settings s;
    if (LoadSettings(s, cid))
    {
        Env::DocGroup gr("res");
        Env::DocAddNum("tokenIssueFee", s.m_IssueFee);
        Env::DocAddBlob_T("cidDaoVault", s.m_cidDaoVault);
    }
}

struct MyKeyID
    :public Env::KeyID
{
    HashValue m_hv;

    MyKeyID()
    {
        m_pID = &m_hv;
        m_nID = sizeof(m_hv);
    }

    void Set(const ContractID& cid, const void* pMetadata, uint32_t nMetadata, const AmountBig& limit)
    {
        HashProcessor::Sha256 hp;
        hp
            << cid
            << limit.m_Lo
            << limit.m_Hi;
        hp.Write(pMetadata, nMetadata);
        hp >> m_hv;
    }
};

struct TokenWalker
{
    Env::VarReaderEx<true> m_R;

    AssetID m_Aid;
    Token m_Token;

    MyKeyID m_Kid;
    AssetInfo m_AssetInfo;

    bool m_IsOwned;
    bool m_HaveAssetInfo;

    uint32_t m_nMetadata;
    char m_szMetadata[1024 * 16 + 1]; // max metadata size is 16K

    void Enum(const ContractID& cid, AssetID aid)
    {
        Env::Key_T<Token::Key> k0;
        _POD_(k0.m_Prefix.m_Cid) = cid;
        k0.m_KeyInContract.m_Aid = aid;

        if (aid)
        {
            k0.m_KeyInContract.m_Aid = aid;
            m_R.Enum_T(k0, k0);
        }
        else
        {
            Env::Key_T<Token::Key> k1;
            _POD_(k1.m_Prefix.m_Cid) = cid;
            k1.m_KeyInContract.m_Aid = static_cast<AssetID>(-1);
            m_R.Enum_T(k0, k1);
        }
    }

    bool MoveNext()
    {
        Env::Key_T<Token::Key> tk;
        if (!m_R.MoveNext_T(tk, m_Token))
            return false;

        m_Aid = tk.m_KeyInContract.m_Aid;

        m_nMetadata = Env::get_AssetInfo(m_Aid, m_AssetInfo, m_szMetadata, sizeof(m_szMetadata) - 1);
        m_HaveAssetInfo = (m_nMetadata || !_POD_(m_AssetInfo).IsZero());

        m_nMetadata = std::min<uint32_t>(m_nMetadata, sizeof(m_szMetadata) - 1);
        m_szMetadata[m_nMetadata] = 0;

        m_IsOwned = false;
        if (m_HaveAssetInfo && (PubKeyFlag::s_Cid != m_Token.m_pkOwner.m_Y))
        {
            m_Kid.Set(tk.m_Prefix.m_Cid, m_szMetadata, m_nMetadata, m_Token.m_Limit);

            PubKey pk;
            m_Kid.get_Pk(pk);
            if (_POD_(pk) == m_Token.m_pkOwner)
                m_IsOwned = true;
        }

        return true;
    }

    void PrintAid() const
    {
        Env::DocAddNum("aid", m_Aid);
    }

    void PrintToken() const
    {
        Env::DocAddNum("mintedLo", m_Token.m_Minted.m_Lo);
        Env::DocAddNum("mintedHi", m_Token.m_Minted.m_Hi);
        Env::DocAddNum("limitLo", m_Token.m_Limit.m_Lo);
        Env::DocAddNum("limitHi", m_Token.m_Limit.m_Hi);

        if (PubKeyFlag::s_Cid == m_Token.m_pkOwner.m_Y)
            Env::DocAddBlob_T("owner_cid", m_Token.m_pkOwner.m_X);
        else
            Env::DocAddBlob_T("owner_pk", m_Token.m_pkOwner);

        if (m_HaveAssetInfo)
            Env::DocAddText("metadata", m_szMetadata);
    }

    void PrintIsOwned() const
    {
        if (m_IsOwned)
            Env::DocAddNum32("owned", 1);
    }
};



void PrintTokens(const ContractID& cid, bool bOwnedOnly)
{
    Env::DocArray gr("res");

    TokenWalker wlk;
    for (wlk.Enum(cid, 0); wlk.MoveNext(); )
    {
        if (bOwnedOnly && !wlk.m_IsOwned)
            continue;

        Env::DocGroup gr2("");

        wlk.PrintAid();
        wlk.PrintToken();

        if (!bOwnedOnly)
            wlk.PrintIsOwned();
    }
}

ON_METHOD(manager, view_token)
{
    TokenWalker wlk;
    wlk.Enum(cid, aid);

    if (aid)
    {
        if (wlk.MoveNext())
        {
            Env::DocGroup gr("res");

            wlk.PrintToken();
            wlk.PrintIsOwned();
        }
        else
            OnError("no such a token");
    }
    else
        PrintTokens(cid, false);
}


ON_METHOD(user, view)
{
    PrintTokens(cid, true);
}

ON_METHOD(user, create)
{
    uint32_t nSize = Env::DocGetText("metadata", nullptr, 0);
    if (!nSize)
        return OnError("no metadata");

    uint32_t nSizeArg = sizeof(Method::CreateToken) + nSize;
    auto pArg = (Method::CreateToken*) Env::Heap_Alloc(nSizeArg);

    char* szMetadata = (char*) (pArg + 1);
    Env::DocGetText("metadata", szMetadata, nSize);
    nSize--; // don't count 0-term

    _POD_(*pArg).SetZero();
    pArg->m_Limit.m_Lo = limit;
    pArg->m_Limit.m_Hi = limitHi;
    pArg->m_MetadataSize = nSize - 1;

    MyKeyID kid;
    kid.Set(cid, szMetadata, pArg->m_MetadataSize, pArg->m_Limit);
    kid.get_Pk(pArg->m_pkOwner);

    FundsChange fc;
    fc.m_Aid = 0;
    fc.m_Amount = g_Beam2Groth * 10;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, pArg->s_iMethod, pArg, nSizeArg, &fc, 1, nullptr, 0, "Mintor create token", 0);

    Env::Heap_Free(pArg);
}

ON_METHOD(user, withdraw)
{
    if (!(aid && value))
        return OnError("aid and amount must be nnz");

    TokenWalker wlk;
    wlk.Enum(cid, aid);
    if (!wlk.MoveNext())
        return OnError("no such a token");

    if (!wlk.m_IsOwned)
        return OnError("not owner");

    Method::Withdraw arg;
    arg.m_Aid = aid;
    arg.m_Value = value;

    FundsChange fc;
    fc.m_Aid = aid;
    fc.m_Amount = value;
    fc.m_Consume = 0;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), &fc, 1, &wlk.m_Kid, 1, "Mintor withdraw", 0);

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
            Mintor_##role##_##name(PAR_READ) \
            On_##role##_##name(Mintor_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        MintorRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    MintorRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace Mintor
