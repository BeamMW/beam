#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable2/app_common_impl.h"

#define Liquity_manager_view(macro)
#define Liquity_manager_view_params(macro) macro(ContractID, cid)
#define Liquity_manager_my_admin_key(macro)
#define Liquity_manager_explicit_upgrade(macro) macro(ContractID, cid)

#define LiquityRole_manager(macro) \
    macro(manager, view) \
    macro(manager, explicit_upgrade) \
    macro(manager, view_params) \
    macro(manager, my_admin_key) \

#define Liquity_user_my_key(macro) macro(ContractID, cid)
#define Liquity_user_my_trove(macro) macro(ContractID, cid)

#define LiquityRole_user(macro) \
    macro(user, my_key) \
    macro(user, my_trove)

#define LiquityRoles_All(macro) \
    macro(manager) \
    macro(user)

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Liquity_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); LiquityRole_##name(THE_METHOD) }
        
        LiquityRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Liquity_##role##_##name(THE_FIELD) int unused = 0)

namespace Liquity {

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr2-liquity";

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

struct TroveKeyID :public Env::KeyID
{
#pragma pack (push, 1)
    struct Blob {
        ContractID m_Cid;
        uint32_t m_Magic;
    } m_Blob;
#pragma pack (pop)

    TroveKeyID(const ContractID& cid)
    {
        m_pID = &m_Blob;
        m_nID = sizeof(m_Blob);

        _POD_(m_Blob.m_Cid) = cid;
        m_Blob.m_Magic = 0x21260ac2;

    }
};

ON_METHOD(manager, view)
{
    static const ShaderID s_pSid[] = {
        Liquity::s_SID,
    };

    ContractID pVerCid[_countof(s_pSid)];
    Height pVerDeploy[_countof(s_pSid)];

    ManagerUpgadable2::Walker wlk;
    wlk.m_VerInfo.m_Count = _countof(s_pSid);
    wlk.m_VerInfo.s_pSid = s_pSid;
    wlk.m_VerInfo.m_pCid = pVerCid;
    wlk.m_VerInfo.m_pHeight = pVerDeploy;

    AdminKeyID kid;
    wlk.ViewAll(&kid);
}

ON_METHOD(manager, explicit_upgrade)
{
    ManagerUpgadable2::MultiSigRitual::Perform_ExplicitUpgrade(cid);
}

struct MyGlobal
    :public Global
{
    bool Load(const ContractID& cid)
    {
        Env::SelectContext(1, 0); // dependent ctx

        Env::Key_T<uint8_t> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        key.m_KeyInContract = Tags::s_State;

        if (!Env::VarReader::Read_T(key, Cast::Down<Global>(*this)))
        {
            OnError("no such a contract");
            return false;
        }

        m_BaseRate.Decay();

        return true;

    }
};

struct MyGlobalPlusTroves
    :public MyGlobal
{
    const ContractID& m_Cid;

    MyGlobalPlusTroves(const ContractID& cid) :m_Cid(cid) {}

    template <uint8_t nTag>
    struct EpochStorage
    {
        const ContractID& m_Cid;
        EpochStorage(const ContractID& cid) :m_Cid(cid) {}

        void Load(uint32_t iEpoch, ExchangePool::Epoch& e)
        {
            Env::Key_T<EpochKey> k;
            _POD_(k.m_Prefix.m_Cid) = m_Cid;
            k.m_KeyInContract.m_Tag = nTag;
            k.m_KeyInContract.m_iEpoch = iEpoch;

            Env::Halt_if(!Env::VarReader::Read_T(k, e));
        }

        static void Save(uint32_t iEpoch, const ExchangePool::Epoch& e) {}
        static void Del(uint32_t iEpoch) {}
    };


    Trove* m_pT = nullptr;
    uint32_t m_ActiveTroves = 0;

    struct My 
    {
        Trove* m_pT = nullptr;
        Trove::ID m_iT;
        uint32_t m_Index;
        PubKey m_Pk;
    } m_My;

    Trove& get_T(Trove::ID iTrove)
    {
        assert(iTrove && (iTrove <= m_Troves.m_iLastCreated));
        return m_pT[iTrove - 1];
    }

    ~MyGlobalPlusTroves()
    {
        if (m_pT)
            Env::Heap_Free(m_pT);
    }

    void LoadAllTroves()
    {
        assert(!m_pT);
        uint32_t nSize = sizeof(Trove) * m_Troves.m_iLastCreated;
        m_pT = (Trove*)Env::Heap_Alloc(nSize);
        Env::Memset(m_pT, 0, nSize);

        Env::Key_T<Trove::Key> k0, k1;
        _POD_(k0.m_Prefix.m_Cid) = m_Cid;
        _POD_(k1.m_Prefix.m_Cid) = m_Cid;
        k0.m_KeyInContract.m_iTrove = 0;
        k1.m_KeyInContract.m_iTrove = static_cast<Trove::ID>(-1);

        for (Env::VarReader r(k0, k1); ; m_ActiveTroves++)
        {
            Trove t;
            if (!r.MoveNext_T(k0, t))
                break;

            _POD_(get_T(k0.m_KeyInContract.m_iTrove)) = t;
        }
    }

    bool Load()
    {
        if (!MyGlobal::Load(m_Cid))
            return false;

        if (m_Troves.m_iLastCreated)
            LoadAllTroves();

        return true;
    }

    bool PopMyTrove(const TroveKeyID& kid)
    {
        assert(!m_My.m_pT);
        kid.get_Pk(m_My.m_Pk);

        Trove* pP = nullptr;
        for (m_My.m_iT = m_Troves.m_iHead; m_My.m_iT; m_My.m_Index++)
        {
            Trove& t = get_T(m_My.m_iT);
            if (_POD_(m_My.m_Pk) == t.m_pkOwner)
            {
                if (pP)
                    pP->m_iNext = t.m_iNext;
                else
                    m_Troves.m_iHead = t.m_iNext;

                EpochStorage<Tags::s_Epoch_Redist> stor(m_Cid);
                m_RedistPool.Remove(t, stor);

                m_Troves.m_Totals.Tok -= t.m_Amounts.Tok;
                m_Troves.m_Totals.Col -= t.m_Amounts.Col;

                m_My.m_pT = &t;
                return true;
            }

            pP = &t;
            m_My.m_iT = t.m_iNext;
        }
        return false;
    }
};


void DocAddPair(const char* sz, const Pair& p)
{
    Env::DocGroup gr(sz);

    Env::DocAddNum("tok", p.Tok);
    Env::DocAddNum("col", p.Col);
}



void DocAddPerc(const char* sz, Float x)
{
    uint64_t val = x * Float(10000); // convert to percents, with 2 digits after point

    char szBuf[Utils::String::Decimal::DigitsMax<uint64_t>::N + 2]; // dot + 0-term
    uint32_t nPos = Utils::String::Decimal::Print(szBuf, val / 100u);
    szBuf[nPos++] = '.';
    nPos += Utils::String::Decimal::Print(szBuf + nPos, val % 100u);
    szBuf[nPos] = 0;


    Env::DocAddText(sz, szBuf);
}

ON_METHOD(manager, view_params)
{
    MyGlobal g;
    if (!g.Load(cid))
        return;

    Env::DocGroup gr("params");

    Env::DocAddBlob_T("oracle", g.m_Settings.m_cidOracle);
    Env::DocAddNum("aidTok", g.m_Aid);
    Env::DocAddNum("aidGov", g.m_Settings.m_AidProfit);
    Env::DocAddNum("liq_reserve", g.m_Settings.m_TroveLiquidationReserve);
    Env::DocAddNum("troves_created", g.m_Troves.m_iLastCreated);
    DocAddPair("totals", g.m_Troves.m_Totals);
    DocAddPerc("baserate", g.m_BaseRate.m_k);
    Env::DocAddNum("stab_pool", g.m_StabPool.get_TotalSell());
    Env::DocAddNum("gov_pool", g.m_ProfitPool.m_Weight);
}

ON_METHOD(manager, my_admin_key)
{
    PubKey pk;
    AdminKeyID kid;
    kid.get_Pk(pk);
    Env::DocAddBlob_T("admin_key", pk);
}

ON_METHOD(user, my_key)
{
    PubKey pk;
    TroveKeyID(cid).get_Pk(pk);
    Env::DocAddBlob_T("key", pk);
}

ON_METHOD(user, my_trove)
{
    MyGlobalPlusTroves g(cid);
    if (!g.Load())
        return;

    Env::DocGroup gr("res");
    Env::DocAddNum("nTotal", g.m_ActiveTroves);

    TroveKeyID kid(cid);
    if (g.PopMyTrove(kid))
    {
        auto& t = *g.m_My.m_pT;

        Env::DocAddNum("iPos", g.m_My.m_Index);
        DocAddPair("amounts", t.m_Amounts);
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

    const char* szErr = nullptr;

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(role, name) \
        if (!Env::Strcmp(szAction, #name)) { \
            Liquity_##role##_##name(PAR_READ) \
            On_##role##_##name(Liquity_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        LiquityRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    LiquityRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace Liquity
