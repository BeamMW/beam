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

#define Liquity_user_view(macro) macro(ContractID, cid)
#define Liquity_user_withdraw_surplus(macro) macro(ContractID, cid)

#define Liquity_user_upd_stab(macro) \
    macro(ContractID, cid) \
    macro(Amount, newVal) \

#define Liquity_user_upd_profit(macro) \
    macro(ContractID, cid) \
    macro(Amount, newVal) \

#define Liquity_user_trove_modify(macro) \
    macro(ContractID, cid) \
    macro(Amount, tok) \
    macro(Amount, col) \
    macro(uint32_t, opTok) \
    macro(uint32_t, opCol) \
    macro(uint32_t, bPredictOnly)

#define Liquity_user_liquidate(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, nMaxTroves) \
    macro(uint32_t, bPredictOnly)

#define Liquity_user_redeem(macro) \
    macro(ContractID, cid) \
    macro(Amount, val) \
    macro(uint32_t, bPredictOnly)

#define LiquityRole_user(macro) \
    macro(user, view) \
    macro(user, withdraw_surplus) \
    macro(user, upd_stab) \
    macro(user, upd_profit) \
    macro(user, trove_modify) \
    macro(user, liquidate) \
    macro(user, redeem)

#define LiquityRoles_All(macro) \
    macro(manager) \
    macro(user)

namespace Liquity {

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

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr2-liquity";

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

template <uint8_t a, uint8_t b, uint8_t c, uint8_t d>
struct FourCC {
    static const uint32_t V = (((((a << 8) | b) << 8) | c) << 8) | d;
};

struct MyKeyID :public Env::KeyID
{
#pragma pack (push, 1)
    struct Blob {
        ContractID m_Cid;
        uint32_t m_Magic;
    } m_Blob;
#pragma pack (pop)

    MyKeyID(const ContractID& cid)
    {
        m_pID = &m_Blob;
        m_nID = sizeof(m_Blob);
        _POD_(m_Blob.m_Cid) = cid;
    }

    void set_Trove() {
        m_Blob.m_Magic = FourCC<'t', 'r', 'o', 'v'>::V;
    }

    void set_Stab() {
        m_Blob.m_Magic = FourCC<'s', 't', 'a', 'b'>::V;
    }

    void set_Profit() {
        m_Blob.m_Magic = FourCC<'p', 'f', 't', '$'>::V;
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

struct AppGlobal
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

    struct MyPrice
        :public Price
    {
        bool Load(const AppGlobal& g)
        {
            Env::Key_T<uint8_t> key;
            _POD_(key.m_Prefix.m_Cid) = g.m_Settings.m_cidOracle;
            key.m_KeyInContract = Oracle2::Tags::s_Median;

            Oracle2::Median med;

            if (!Env::VarReader::Read_T(key, med))
                return false;

            m_Value = med.m_Res;
            return true;
        }
    };
};

struct AppGlobalPlus
    :public AppGlobal
{
    MyKeyID m_Kid;

    AppGlobalPlus(const ContractID& cid) :m_Kid(cid) {}

    MyPrice m_Price;

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

    struct MyTrove
    {
        Trove* m_pT = nullptr;
        Trove::ID m_iT;
        Trove::ID m_iPrev0;
        uint32_t m_Index;
        Pair m_Vault;
        PubKey m_Pk;
    } m_MyTrove;

    struct MyStab
    {
        PubKey m_Pk;
        Pair m_Amounts;
    } m_MyStab;

    struct MyProfit
    {
        PubKey m_Pk;
        Amount m_Beam;
        Amount m_Gov;
    } m_MyProfit;

    Trove& get_T(Trove::ID iTrove)
    {
        assert(iTrove && (iTrove <= m_Troves.m_iLastCreated));
        return m_pT[iTrove - 1];
    }

    ~AppGlobalPlus()
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
        _POD_(k0.m_Prefix.m_Cid) = m_Kid.m_Blob.m_Cid;
        _POD_(k1.m_Prefix.m_Cid) = m_Kid.m_Blob.m_Cid;
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

    bool LoadPlus()
    {
        if (!Load(m_Kid.m_Blob.m_Cid))
            return false;

        if (!m_Price.Load(*this))
            return false;

        if (m_Troves.m_iLastCreated)
            LoadAllTroves();

        return true;
    }

    bool ReadVault()
    {
        m_Kid.set_Trove();
        m_Kid.get_Pk(m_MyTrove.m_Pk);

        Env::Key_T<Balance::Key> k;
        k.m_Prefix.m_Cid = m_Kid.m_Blob.m_Cid;
        _POD_(k.m_KeyInContract.m_Pk) = m_MyTrove.m_Pk;

        Balance x;
        bool bFound = Env::VarReader::Read_T(k, x);

        if (bFound)
            m_MyTrove.m_Vault = x.m_Amounts;
        else
            _POD_(m_MyTrove.m_Vault).SetZero();
        return bFound;
    }

    void PopTrove(Trove::ID iPrev, Trove& t)
    {
        if (iPrev)
            get_T(iPrev).m_iNext = t.m_iNext;
        else
            m_Troves.m_iHead = t.m_iNext;

        EpochStorage<Tags::s_Epoch_Redist> stor(m_Kid.m_Blob.m_Cid);
        m_RedistPool.Remove(t, stor);

        m_Troves.m_Totals.Tok -= t.m_Amounts.Tok;
        m_Troves.m_Totals.Col -= t.m_Amounts.Col;
    }

    bool PopMyTrove()
    {
        assert(!m_MyTrove.m_pT);

        ReadVault();

        m_MyTrove.m_iPrev0 = 0;
        for (m_MyTrove.m_iT = m_Troves.m_iHead; m_MyTrove.m_iT; m_MyTrove.m_Index++)
        {
            Trove& t = get_T(m_MyTrove.m_iT);
            if (_POD_(m_MyTrove.m_Pk) == t.m_pkOwner)
            {
                PopTrove(m_MyTrove.m_iPrev0, t);

                m_MyTrove.m_pT = &t;
                return true;
            }

            m_MyTrove.m_iPrev0 = m_MyTrove.m_iT;
            m_MyTrove.m_iT = t.m_iNext;
        }
        return false;
    }

    Trove::ID PushTrove(const Pair& tVals)
    {
        EpochStorage<Tags::s_Epoch_Redist> stor(m_Kid.m_Blob.m_Cid);

        Trove::ID iPrev1 = 0;
        for (Trove::ID iT = m_Troves.m_iHead; iT; )
        {
            const Trove& t1 = get_T(iT);

            auto vals = m_RedistPool.get_UpdatedAmounts(t1, stor);
            if (vals.CmpRcr(tVals) >= 0)
                break;

            iPrev1 = iT;
            iT = t1.m_iNext;
        }

        // no need to modify list, we're not doing anything with it further. Just update totals
        m_Troves.m_Totals.Tok += tVals.Tok;
        m_Troves.m_Totals.Col += tVals.Col;

        return iPrev1;
    }

    Trove::ID PushMyTrove()
    {
        assert(m_MyTrove.m_pT);
        return PushTrove(m_MyTrove.m_pT->m_Amounts);
    }

    bool PopMyStab()
    {
        m_Kid.set_Stab();
        m_Kid.get_Pk(m_MyStab.m_Pk);

        Env::Key_T<StabPoolEntry::Key> k;
        k.m_Prefix.m_Cid = m_Kid.m_Blob.m_Cid;
        _POD_(k.m_KeyInContract.m_pkUser) = m_MyStab.m_Pk;

        StabPoolEntry e;
        if (!Env::VarReader::Read_T(k, e))
        {
            m_MyStab.m_Amounts.Tok = m_MyStab.m_Amounts.Col = 0;
            return false;
        }

        EpochStorage<Tags::s_Epoch_Stable> stor(m_Kid.m_Blob.m_Cid);

        HomogenousPool::Pair out;
        m_StabPool.UserDel(e.m_User, out, stor);

        m_MyStab.m_Amounts.Tok = out.s;
        m_MyStab.m_Amounts.Col = out.b;

        return true;
    }

    bool PopMyProfit()
    {
        m_Kid.set_Profit();
        m_Kid.get_Pk(m_MyProfit.m_Pk);

        Env::Key_T<ProfitPoolEntry::Key> k;
        k.m_Prefix.m_Cid = m_Kid.m_Blob.m_Cid;
        _POD_(k.m_KeyInContract.m_pkUser) = m_MyProfit.m_Pk;

        ProfitPoolEntry e;
        if (!Env::VarReader::Read_T(k, e))
            return false;

        m_MyProfit.m_Gov = e.m_User.m_Weight;
        m_ProfitPool.Remove(&m_MyProfit.m_Beam, e.m_User);

        return true;
    }

    static void PrepareComp(Flow& dst, Amount vault, FundsChange& fc)
    {
        dst.Add(vault, 0);
        fc.m_Amount = dst.m_Val;
        fc.m_Consume = dst.m_Spend;
    }

    void PrepareTxBase(Method::BaseTx& tx, FundsChange* pFc)
    {
        // account for surplus, and update fc
        PrepareComp(tx.m_Flow.Tok, m_MyTrove.m_Vault.Tok, pFc[0]);
        PrepareComp(tx.m_Flow.Col, m_MyTrove.m_Vault.Col, pFc[1]);

        pFc[0].m_Aid = m_Aid;
        pFc[1].m_Aid = 0;
    }

    void PrepareTx(Method::BaseTxUser& tx, FundsChange* pFc)
    {
        PrepareTxBase(tx, pFc);
        _POD_(tx.m_pkUser) = m_MyTrove.m_Pk;
    }

    void PrepareTx(Method::BaseTxTrove& tx, FundsChange* pFc)
    {
        PrepareTxBase(tx, pFc);
        tx.m_iPrev0 = m_MyTrove.m_iPrev0;
    }

    void OnTroveMove(Method::BaseTx& tx, uint8_t bPop)
    {
        const Pair& tVals = m_MyTrove.m_pT->m_Amounts;
        tx.m_Flow.Tok.Add(!m_Settings.m_TroveLiquidationReserve, bPop);
        tx.m_Flow.Tok.Add(tVals.Tok, bPop);
        tx.m_Flow.Col.Add(tVals.Col, !bPop);
    }
};


void DocAddPair(const char* sz, const Pair& p)
{
    Env::DocGroup gr(sz);

    Env::DocAddNum("tok", p.Tok);
    Env::DocAddNum("col", p.Col);
}

void DocAddFloat(const char* sz, Float x, uint32_t nDigsAfterDot)
{
    uint64_t norm = 1;
    for (uint32_t i = 0; i < nDigsAfterDot; i++)
        norm *= 10;

    uint64_t val = x * Float(norm);

    char szBuf[Utils::String::Decimal::DigitsMax<uint64_t>::N + 2]; // dot + 0-term
    uint32_t nPos = Utils::String::Decimal::Print(szBuf, val / norm);
    szBuf[nPos++] = '.';
    Utils::String::Decimal::Print(szBuf + nPos, val % norm, nDigsAfterDot);

    Env::DocAddText(sz, szBuf);
}


void DocAddPerc(const char* sz, Float x)
{
    DocAddFloat(sz, x * Float(100), 3);
}

ON_METHOD(manager, view_params)
{
    AppGlobal g;
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

    AppGlobal::MyPrice price;
    if (price.Load(g))
    {
        DocAddFloat("price", price.m_Value, 4);

        if (g.m_Troves.m_Totals.Tok)
            DocAddPerc("tcr", price.ToCR(g.m_Troves.m_Totals.get_Rcr()));
    }
}

ON_METHOD(manager, my_admin_key)
{
    PubKey pk;
    AdminKeyID kid;
    kid.get_Pk(pk);
    Env::DocAddBlob_T("admin_key", pk);
}

ON_METHOD(user, view)
{
    AppGlobalPlus g(cid);
    if (!g.LoadPlus())
        return;

    Env::DocGroup gr("res");
    Env::DocAddNum("nTotal", g.m_ActiveTroves);

    if (g.PopMyTrove())
    {
        auto& t = *g.m_MyTrove.m_pT;

        Env::DocAddNum("iPos", g.m_MyTrove.m_Index);
        DocAddPair("amounts", t.m_Amounts);
        DocAddPerc("cr", g.m_Price.ToCR(t.m_Amounts.get_Rcr()));
    }

    if (g.m_MyTrove.m_Vault.Tok || g.m_MyTrove.m_Vault.Col)
        DocAddPair("surplus", g.m_MyTrove.m_Vault);

    if (g.PopMyStab())
        DocAddPair("stab", g.m_MyStab.m_Amounts);

    if (g.PopMyProfit())
    {
        Env::DocGroup gr1("profit");
        Env::DocAddNum("gov", g.m_MyProfit.m_Gov);
        Env::DocAddNum("beam", g.m_MyProfit.m_Beam);
    }
}

ON_METHOD(user, withdraw_surplus)
{
    AppGlobalPlus g(cid);
    if (!g.Load(cid)) // skip loading all troves
        return;

    if (!g.ReadVault())
        OnError("no surplus");

    const auto& v = g.m_MyTrove.m_Vault;
    assert(v.Col || v.Tok);

    Method::FundsAccess args;
    FundsChange pFc[2];

    _POD_(args.m_Flow).SetZero();
    g.PrepareTx(args, pFc);

    Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "surplus withdraw", 0);
}

ON_METHOD(user, upd_stab)
{
    AppGlobalPlus g(cid);
    if (!g.Load(cid)) // skip loading all troves
        return;

    g.PopMyStab();

    if ((newVal == g.m_MyStab.m_Amounts.Tok) && !g.m_MyStab.m_Amounts.Col)
        OnError("no change");

    g.m_MyTrove.m_Vault = g.m_MyStab.m_Amounts;

    Method::UpdStabPool args;
    FundsChange pFc[2];

    _POD_(args.m_Flow).SetZero();
    args.m_Flow.Tok.Add(newVal, 1);

    g.PrepareTx(args, pFc);

    Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "update stab pool", 0);
}

ON_METHOD(user, upd_profit)
{
    AppGlobalPlus g(cid);
    if (!g.Load(cid)) // skip loading all troves
        return;

    g.PopMyProfit();

    if ((newVal == g.m_MyProfit.m_Gov) && !g.m_MyProfit.m_Beam)
        OnError("no change");

    g.m_MyTrove.m_Vault.Col = g.m_MyProfit.m_Beam;
    g.m_MyTrove.m_Vault.Tok = 0;

    Method::UpdProfitPool args;
    FundsChange pFc[3];

    _POD_(args.m_Flow).SetZero();
    args.m_Flow.Tok.Add(newVal, 1);

    g.PrepareTx(args, pFc);

    auto& fc = pFc[2];
    fc.m_Aid = g.m_Settings.m_AidProfit;
    fc.m_Consume = (newVal > g.m_MyProfit.m_Gov);
    fc.m_Amount = fc.m_Consume ? (newVal - g.m_MyProfit.m_Gov) : (g.m_MyProfit.m_Gov - newVal);
    

    Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "update profit pool", 0);
}

bool AdjustVal(Amount& dst, Amount src, uint32_t op)
{
    switch (op)
    {
    case 0:
        dst = src;
        return true;

    case 1:
        dst += src;
        if (dst >= src)
            return true;

        OnError("val overflow");
        return false;

    case 2:
        if (dst < src)
        {
            OnError("val overflow");
            return false;
        }
        dst -= src;
        return true;
    }

    OnError("invalid val op");
    return false;
}

ON_METHOD(user, trove_modify)
{
    AppGlobalPlus g(cid);
    if (!g.LoadPlus())
        return;

    Trove tNew;
    Method::BaseTx txb;
    _POD_(txb.m_Flow).SetZero();

    bool bPopped = g.PopMyTrove();
    if (bPopped)
        g.OnTroveMove(txb, 1);
    else
    {
        g.m_MyTrove.m_pT = &tNew;
        tNew.m_Amounts.Tok = tNew.m_Amounts.Col = 0;
    }

    auto& t = *g.m_MyTrove.m_pT; // alias
    Amount tok0 = t.m_Amounts.Tok;

    if (!AdjustVal(t.m_Amounts.Tok, tok, opTok) ||
        !AdjustVal(t.m_Amounts.Col, col, opCol))
        return;

    FundsChange pFc[2];

    if (t.m_Amounts.Tok || t.m_Amounts.Col)
    {
        if (t.m_Amounts.Tok < g.m_Settings.m_TroveLiquidationReserve)
        {
            OnError("min tok required");
            return;
        }

       auto iPrev1 = g.PushMyTrove();

        bool bRecovery = g.IsRecovery(g.m_Price);
        if (g.IsTroveUpdInvalid(t, g.m_Price, bRecovery))
            return OnError("insufficient collateral");

        g.OnTroveMove(txb, 0);

        Amount fee = g.get_BorrowFee(t.m_Amounts.Tok, tok0, bRecovery, g.m_Price);
        txb.m_Flow.Col.Add(fee, 1);

        if (bPopped)
        {
            Method::TroveModify args;
            Cast::Down<Method::BaseTx>(args) = txb;

            args.m_Amounts = t.m_Amounts;
            args.m_iPrev1 = iPrev1;
            g.PrepareTx(args, pFc);

            if (!bPredictOnly)
                Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "trove modify", 0);
        }
        else
        {
            Method::TroveOpen args;
            Cast::Down<Method::BaseTx>(args) = txb;

            args.m_Amounts = t.m_Amounts;
            args.m_iPrev1 = iPrev1;
            g.PrepareTx(args, pFc);

            if (!bPredictOnly)
                Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "trove open", 0);
        }

        if (bPredictOnly)
        {
            Env::DocGroup gr("prediction");
            DocAddPair("newVals", t.m_Amounts);
            DocAddPerc("cr", g.m_Price.ToCR(t.m_Amounts.get_Rcr()));

            if (fee)
                Env::DocAddNum("fee", fee);
        }

    }
    else
    {
        // closing
        if (!bPopped)
            return OnError("trove already closed");

        Method::TroveClose args;
        Cast::Down<Method::BaseTx>(args) = txb;
        
        g.PrepareTx(args, pFc);

        if (!bPredictOnly)
            Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "trove close", 0);
    }
}

ON_METHOD(user, liquidate)
{
    AppGlobalPlus g(cid);
    if (!g.LoadPlus())
        return;

    g.ReadVault();

    Global::Liquidator ctx;
    ctx.m_Price = g.m_Price;
    _POD_(ctx.m_fpLogic).SetZero();

    uint32_t nCount = 0;
    bool bSelf = false;

    while (g.m_Troves.m_iHead)
    {
        auto& t = g.get_T(g.m_Troves.m_iHead);
        g.PopTrove(0, t);

        Amount valSurplus = 0;
        Env::Halt_if(!g.LiquidateTrove(t, ctx, valSurplus));

        if (_POD_(g.m_MyTrove.m_Pk) == t.m_pkOwner)
            bSelf = true;

        if (nMaxTroves == ++nCount) // if nMaxTroves is 0 then unlimited
            break;
    }

    if (bPredictOnly)
    {
        Env::DocGroup gr("prediction");
        Env::DocAddNum("count", nCount);
        Env::DocAddNum("tok", ctx.m_fpLogic.Tok.m_Val);
        if (bSelf)
            Env::DocAddNum("self_kill", 1u);
    }
    else
    {

        Method::Liquidate args;
        args.m_Flow = ctx.m_fpLogic;
        args.m_Count = nCount;

        FundsChange pFc[2];
        g.PrepareTx(args, pFc);

        Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "troves liquidate", 0);
    }
}

ON_METHOD(user, redeem)
{
    AppGlobalPlus g(cid);
    if (!g.LoadPlus())
        return;

    g.ReadVault();

    Global::Redeemer ctx;
    ctx.m_Price = g.m_Price;
    _POD_(ctx.m_fpLogic).SetZero();
    ctx.m_TokRemaining = val;

    Trove::ID iPrev1 = 0;

    while (g.m_Troves.m_iHead && ctx.m_TokRemaining)
    {
        auto& t = g.get_T(g.m_Troves.m_iHead);
        g.PopTrove(0, t);

        if (!g.RedeemTrove(t, ctx))
            break;

        if (t.m_Amounts.Tok)
        {
            assert(!ctx.m_TokRemaining);
            iPrev1 = g.PushTrove(t.m_Amounts);
        }
    }

    Amount fee = g.AddRedeemFee(ctx);

    if (bPredictOnly)
    {
        Env::DocGroup gr("prediction");
        Env::DocAddNum("tok", ctx.m_fpLogic.Tok.m_Val);
        Env::DocAddNum("col", ctx.m_fpLogic.Col.m_Val);
        Env::DocAddNum("fee", fee);
    }
    else
    {
        if (ctx.m_TokRemaining)
            OnError("insufficient redeemable troves");

        Method::Redeem args;
        args.m_Flow = ctx.m_fpLogic;
        args.m_Amount = val;
        args.m_iPrev1 = iPrev1;

        FundsChange pFc[2];
        g.PrepareTx(args, pFc);

        Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "redeem", 0);
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
