#include "../common.h"
#include "../app_common_impl.h"
#include "contract.h"
#include "../upgradable3/app_common_impl.h"
#include "../dao-vault/contract.h"
#include "../oracle2/contract.h"

#define Nephrite_manager_view(macro)
#define Nephrite_manager_view_params(macro) macro(ContractID, cid)
#define Nephrite_manager_view_all(macro) macro(ContractID, cid)
#define Nephrite_manager_my_admin_key(macro)
#define Nephrite_manager_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define Nephrite_manager_replace_admin(macro) Upgradable3_replace_admin(macro)
#define Nephrite_manager_set_min_approvers(macro) Upgradable3_set_min_approvers(macro)
#define Nephrite_manager_explicit_upgrade(macro) macro(ContractID, cid)
#define Nephrite_manager_add_stab_reward(macro) macro(ContractID, cid) macro(Amount, amount)

#define Nephrite_manager_deploy(macro) \
    Upgradable3_deploy(macro) \
    macro(ContractID, cidDaoVault) \
    macro(ContractID, cidOracle1) \
    macro(ContractID, cidOracle2) \
    macro(Amount, troveLiquidationReserve) \
    macro(AssetID, aidGov) \
    macro(uint32_t, hInitialPeriod)


#define NephriteRole_manager(macro) \
    macro(manager, view) \
    macro(manager, deploy) \
    macro(manager, schedule_upgrade) \
    macro(manager, explicit_upgrade) \
    macro(manager, replace_admin) \
    macro(manager, set_min_approvers) \
    macro(manager, view_params) \
    macro(manager, view_all) \
    macro(manager, my_admin_key) \
    macro(manager, add_stab_reward) \

#define Nephrite_user_view(macro) macro(ContractID, cid)
#define Nephrite_user_withdraw_surplus(macro) macro(ContractID, cid)

#define Nephrite_user_upd_stab(macro) \
    macro(ContractID, cid) \
    macro(Amount, newVal) \

#define Nephrite_user_trove_modify(macro) \
    macro(ContractID, cid) \
    macro(Amount, tok) \
    macro(Amount, col) \
    macro(uint32_t, opTok) \
    macro(uint32_t, opCol) \
    macro(uint32_t, tcr_mperc) \
    macro(uint32_t, bAutoTok) \
    macro(uint32_t, bPredictOnly)

#define Nephrite_user_liquidate(macro) \
    macro(ContractID, cid) \
    macro(uint32_t, nMaxTroves) \
    macro(uint32_t, bPredictOnly)

#define Nephrite_user_redeem(macro) \
    macro(ContractID, cid) \
    macro(Amount, val) \
    macro(uint32_t, bPredictOnly)

#define NephriteRole_user(macro) \
    macro(user, view) \
    macro(user, withdraw_surplus) \
    macro(user, upd_stab) \
    macro(user, trove_modify) \
    macro(user, liquidate) \
    macro(user, redeem)

#define NephriteRoles_All(macro) \
    macro(manager) \
    macro(user)

namespace Nephrite {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("roles");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_METHOD(role, name) { Env::DocGroup grMethod(#name);  Nephrite_##role##_##name(THE_FIELD) }
#define THE_ROLE(name) { Env::DocGroup grRole(#name); NephriteRole_##name(THE_METHOD) }
        
        NephriteRoles_All(THE_ROLE)
#undef THE_ROLE
#undef THE_METHOD
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(role, name) void On_##role##_##name(Nephrite_##role##_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr3-nephrite";

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

};

void DocAddPairInternal(const Pair& p)
{
    Env::DocAddNum("tok", p.Tok);
    Env::DocAddNum("col", p.Col);
}

void DocAddPair(const char* sz, const Pair& p)
{
    Env::DocGroup gr(sz);
    DocAddPairInternal(p);
}

void DocAddFloat(const char* sz, Float x, uint32_t nDigsAfterDot)
{
    uint64_t norm = 1;
    for (uint32_t i = 0; i < nDigsAfterDot; i++)
        norm *= 10;

    uint64_t val = x * Float(norm * 2);
    val = (val + 1) / 2;

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

const Upgradable3::Manager::VerInfo g_VerInfo = { s_pSID, _countof(s_pSID) };

ON_METHOD(manager, view)
{
    AdminKeyID kid;
    g_VerInfo.DumpAll(&kid);
}

static const Amount g_DepositCA = 3000 * g_Beam2Groth; // 3K beams

ON_METHOD(manager, deploy)
{
    FundsChange fc;
    fc.m_Aid = 0; // asset id
    fc.m_Amount = g_DepositCA; // amount of the input or output
    fc.m_Consume = 1; // contract consumes funds (i.e input, in this case)

    AdminKeyID kid;
    PubKey pk;
    kid.get_Pk(pk);

    Nephrite::Method::Create arg;

    if (!g_VerInfo.FillDeployArgs(arg.m_Upgradable, &pk))
        return;

    if (!troveLiquidationReserve)
        return OnError("trove Liquidation Reserve should not be zero");

    auto& s = arg.m_Settings; // alias
    s.m_AidGov = aidGov;
    s.m_TroveLiquidationReserve = troveLiquidationReserve;
    _POD_(s.m_cidDaoVault) = cidDaoVault;
    _POD_(s.m_cidOracle1) = cidOracle1;
    _POD_(s.m_cidOracle2) = _POD_(cidOracle2).IsZero() ? cidOracle1 : cidOracle2;
    s.m_hMinRedemptionHeight = Env::get_Height() + hInitialPeriod;

    const uint32_t nCharge =
        Upgradable3::Manager::get_ChargeDeploy() +
        Env::Cost::AssetManage +
        Env::Cost::Refs * 3 +
        Env::Cost::SaveVar_For(sizeof(Nephrite::Global)) +
        Env::Cost::MemOpPerByte * sizeof(Nephrite::Global) +
        Env::Cost::Cycle * 1000;

    Env::GenerateKernel(nullptr, 0, &arg, sizeof(arg), &fc, 1, nullptr, 0, "Deploy Nephrite contract", nCharge);
}

ON_METHOD(manager, schedule_upgrade)
{
    AdminKeyID kid;
    g_VerInfo.ScheduleUpgrade(cid, kid, hTarget);
}

ON_METHOD(manager, explicit_upgrade)
{
    Upgradable3::Manager::MultiSigRitual::Perform_ExplicitUpgrade(cid);
}

ON_METHOD(manager, replace_admin)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_ReplaceAdmin(cid, kid, iAdmin, pk);
}

ON_METHOD(manager, set_min_approvers)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_SetApprovers(cid, kid, newVal);
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

        m_StabPool.AddReward(Env::get_Height());

        return true;

    }

    struct MyPrice
        :public Price
    {
        bool LoadInternal(const Env::Key_T<uint8_t>& key)
        {
            Oracle2::Median med;

            if (!Env::VarReader::Read_T(key, med) ||
                (med.m_hEnd < Env::get_Height()))
                return false;

            if (!Price::IsSane(med.m_Res))
                return false;

            m_Value = med.m_Res;
            return true;
        }

        uint32_t Load(const AppGlobal& g)
        {
            Env::Key_T<uint8_t> key;
            _POD_(key.m_Prefix.m_Cid) = g.m_Settings.m_cidOracle1;
            key.m_KeyInContract = Oracle2::Tags::s_Median;

            if (LoadInternal(key))
                return 1;

            if (_POD_(g.m_Settings.m_cidOracle1) != g.m_Settings.m_cidOracle2)
            {
                _POD_(key.m_Prefix.m_Cid) = g.m_Settings.m_cidOracle2;
                if (LoadInternal(key))
                    return 2;
            }

            return 0;
        }
    };
};

namespace Charge
{
    uint32_t StdCall_RO()
    {
        return
            Env::Cost::CallFar +
            Env::Cost::LoadVar_For(sizeof(Global));
    }

    static uint32_t StdCall()
    {
        return
            StdCall_RO() +
            Env::Cost::SaveVar_For(sizeof(Global));
    }

    static uint32_t BankDeposit()
    {
        return
            Env::Cost::LoadVar_For(sizeof(Balance)) +
            Env::Cost::SaveVar_For(sizeof(Balance)) +
            Env::Cost::Cycle * 50;
    }

    static uint32_t BankWithdraw()
    {
        return
            Env::Cost::LoadVar_For(sizeof(Balance)) +
            Env::Cost::SaveVar + // assume no balance after withdraw
            Env::Cost::Cycle * 50;
    }

    static const uint32_t RedistPoolOp =
        Env::Cost::Cycle * 1500;

    static uint32_t TrovePull0()
    {
        return
            Env::Cost::LoadVar_For(sizeof(Trove)) +
            RedistPoolOp;
    }

    static uint32_t TrovePull(Trove::ID iPrev)
    {
        uint32_t nRet = TrovePull0();

        if (iPrev)
        {
            nRet += 
                Env::Cost::LoadVar_For(sizeof(Trove)) +
                Env::Cost::SaveVar_For(sizeof(Trove));
        }

        return nRet;
    }

    static const uint32_t TrovePushCheck1 =
        Env::Cost::Cycle * 200;


    static const uint32_t get_SendProfit()
    {
        return
            Env::Cost::Cycle * 100 +
            Env::Cost::CallFar +
            Env::Cost::FundsLock;
    }

    static const uint32_t StabPoolOp0 =
        Env::Cost::Cycle * 1500;

    static const uint32_t Price()
    {
        return
            Env::Cost::CallFar +
            Env::Cost::LoadVar_For(sizeof(Oracle2::Median)) +
            Env::Cost::Cycle * 50;
    }

    static const uint32_t TroveTest =
        Env::Cost::Cycle * 1000;
}

struct AppGlobalPlus
    :public AppGlobal
{
    MyKeyID m_Kid;

    MyPrice m_Price;
    uint32_t m_PriceOracle = 0;


    template <uint8_t nTag>
    struct EpochStorage
    {
        uint32_t m_Charge = 0;
        const ContractID& m_Cid;
        EpochStorage(const ContractID& cid) :m_Cid(cid) {}

        template <uint32_t nDims>
        void Load(uint32_t iEpoch, HomogenousPool::Epoch<nDims>& e)
        {
            m_Charge += Env::Cost::LoadVar_For(sizeof(HomogenousPool::Epoch<nDims>));

            Env::Key_T<EpochKey> k;
            _POD_(k.m_Prefix.m_Cid) = m_Cid;
            k.m_KeyInContract.m_Tag = nTag;
            k.m_KeyInContract.m_iEpoch = iEpoch;

            Env::Halt_if(!Env::VarReader::Read_T(k, e));
        }

        template <uint32_t nDims>
        void Save(uint32_t iEpoch, const HomogenousPool::Epoch<nDims>& e) {
            m_Charge += Env::Cost::SaveVar_For(sizeof(HomogenousPool::Epoch<nDims>));
        }
        void Del(uint32_t iEpoch) {
            m_Charge += Env::Cost::SaveVar;
        }
    };

    EpochStorage<Tags::s_Epoch_Stable> m_EpochStorageStab;
    EpochStorage<Tags::s_Epoch_Redist> m_EpochStorageRedist;

    AppGlobalPlus(const ContractID& cid)
        :m_Kid(cid)
        ,m_EpochStorageStab(cid)
        ,m_EpochStorageRedist(cid)
    {
    }


    Trove* m_pT = nullptr;
    uint32_t m_ActiveTroves = 0;

    struct MyTrove
    {
        Trove* m_pT = nullptr;
        Trove::ID m_iT;
        Trove::ID m_iPrev0;
        uint32_t m_Index;
    } m_MyTrove;

    Balance m_Balance;
    PubKey m_Pk;

    struct MyStab
    {
        Pair m_Amounts;
        Amount m_Gov;
    } m_MyStab;

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

    void LoadPrice()
    {
        m_PriceOracle = m_Price.Load(*this);
    }

    bool LoadPlus()
    {
        if (!Load(m_Kid.m_Blob.m_Cid))
            return false;

        LoadPrice();

        if (m_Troves.m_iLastCreated)
            LoadAllTroves();

        return true;
    }

    bool TestHaveValidPrice()
    {
        if (m_PriceOracle)
            return true;

        OnError("no valid price");
        return false;
    }

    void DocAddTrove(const Trove& t)
    {
        DocAddPair("amounts", t.m_Amounts);
        if (m_PriceOracle)
            DocAddPerc("cr", m_Price.ToCR(t.m_Amounts.get_Rcr()));
    }

    bool ReadBalance()
    {
        m_Kid.get_Pk(m_Pk);

        Env::Key_T<Balance::Key> k;
        k.m_Prefix.m_Cid = m_Kid.m_Blob.m_Cid;
        _POD_(k.m_KeyInContract.m_Pk) = m_Pk;

        bool bFound = Env::VarReader::Read_T(k, m_Balance);
        if (!bFound)
            _POD_(m_Balance).SetZero();

        return bFound;
    }

    bool ReadBalanceAny()
    {
        m_Kid.set_Trove();
        if (ReadBalance())
            return true;

        m_Kid.set_Stab();
        return ReadBalance();
    }

    void PopTrove(Trove::ID iPrev, Trove& t)
    {
        if (iPrev)
            get_T(iPrev).m_iNext = t.m_iNext;
        else
            m_Troves.m_iHead = t.m_iNext;

        m_RedistPool.Remove(t, m_EpochStorageRedist);

        m_Troves.m_Totals.Tok -= t.m_Amounts.Tok;
        m_Troves.m_Totals.Col -= t.m_Amounts.Col;
    }

    bool PopMyTrove()
    {
        assert(!m_MyTrove.m_pT);

        m_Kid.set_Trove();
        ReadBalance();

        m_MyTrove.m_iPrev0 = 0;
        for (m_MyTrove.m_iT = m_Troves.m_iHead; m_MyTrove.m_iT; m_MyTrove.m_Index++)
        {
            Trove& t = get_T(m_MyTrove.m_iT);
            if (_POD_(m_Pk) == t.m_pkOwner)
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

    Trove::ID PushTrove(Trove& t, Trove::ID tid, uint32_t& nCharge)
    {
        nCharge +=
            Env::Cost::SaveVar_For(sizeof(Trove)) +
            Charge::RedistPoolOp;

        Trove::ID iPrev1 = 0;
        for (Trove::ID iT = m_Troves.m_iHead; iT; )
        {
            const Trove& t1 = get_T(iT);

            auto vals = m_RedistPool.get_UpdatedAmounts(t1, m_EpochStorageRedist);
            if (vals.CmpRcr(t.m_Amounts) <= 0)
                iPrev1 = iT; // suitable candidate, but maybe there'll be a better one

            iT = t1.m_iNext;
        }

        m_Troves.m_Totals.Tok += t.m_Amounts.Tok;
        m_Troves.m_Totals.Col += t.m_Amounts.Col;

        if (iPrev1)
        {
            uint32_t nCharge0 = m_EpochStorageRedist.m_Charge;

            Trove& tPrev = get_T(iPrev1);
            auto vals = m_RedistPool.get_UpdatedAmounts(tPrev, m_EpochStorageRedist);

            nCharge +=
                Charge::TrovePull0() +
                (m_EpochStorageRedist.m_Charge - nCharge0) +
                Charge::TrovePushCheck1 +
                Env::Cost::SaveVar_For(sizeof(Trove));

            t.m_iNext = tPrev.m_iNext;
            tPrev.m_iNext = tid;
        }
        else
        {
            t.m_iNext = m_Troves.m_iHead;
            m_Troves.m_iHead = tid;
        }

        return iPrev1;
    }

    Trove::ID PushMyTrove(uint32_t& nCharge)
    {
        assert(m_MyTrove.m_pT);
        return PushTrove(*m_MyTrove.m_pT, m_MyTrove.m_iT, nCharge);
    }

    bool PopMyStab()
    {
        m_Kid.set_Stab();
        ReadBalance();

        Env::Key_T<StabPoolEntry::Key> k;
        k.m_Prefix.m_Cid = m_Kid.m_Blob.m_Cid;
        _POD_(k.m_KeyInContract.m_pkUser) = m_Pk;

        StabPoolEntry e;
        if (!Env::VarReader::Read_T(k, e))
        {
            m_MyStab.m_Gov = 0;
            return false;
        }

        StabilityPool::User::Out out;
        m_StabPool.UserDel<false, false>(e.m_User, out, 0, m_EpochStorageStab);

        m_MyStab.m_Amounts.Tok = out.m_Sell;
        m_MyStab.m_Amounts.Col = out.m_pBuy[0];
        m_MyStab.m_Gov = out.m_pBuy[1];

        return true;
    }

    static void AdjustChargeFunds(const FundsChange& fc, uint32_t& nCharge)
    {
        if (fc.m_Amount)
            nCharge += Env::Cost::FundsLock;
    }

    static void Flow2Fc(FundsChange& fc, const Flow& f, AssetID aid, uint32_t& nCharge)
    {
        fc.m_Amount = f.m_Val;
        fc.m_Consume = f.m_Spend;
        fc.m_Aid = aid;

        AdjustChargeFunds(fc, nCharge);
    }

    void Flow2Fc(FundsChange* pFc, const FlowPair& fp, uint32_t& nCharge)
    {
        Flow2Fc(pFc[0], fp.Tok, m_Aid, nCharge);
        Flow2Fc(pFc[1], fp.Col, 0, nCharge);
    }

    bool PrepareBalanceTx(Method::BaseTx& tx, FundsChange* pFc, uint32_t& nCharge)
    {
        bool bUseBank = m_Balance.m_Amounts.Tok || m_Balance.m_Amounts.Col;
        if (bUseBank)
        {
            tx.m_Flow.Tok.Add(m_Balance.m_Amounts.Tok, 0);
            tx.m_Flow.Col.Add(m_Balance.m_Amounts.Col, 0);

            nCharge += Charge::BankWithdraw();
        }

        Flow2Fc(pFc, tx.m_Flow, nCharge);

        nCharge += Env::Cost::AddSig;

        return bUseBank;
    }

    bool PrepareTroveTx(Method::BaseTxUser& tx, FundsChange* pFc, uint32_t& nCharge)
    {
        _POD_(tx.m_pkUser) = m_Pk;
        return PrepareBalanceTx(tx, pFc, nCharge);
    }

    bool PrepareTroveTx(Method::BaseTxTrove& tx, FundsChange* pFc, uint32_t& nCharge)
    {
        tx.m_iPrev0 = m_MyTrove.m_iPrev0;
        return PrepareBalanceTx(tx, pFc, nCharge);
    }

    bool PrepareTroveTxGov(Method::BaseTxUserGov& tx, FundsChange* pFc, uint32_t& nCharge)
    {
        bool bUseBank = PrepareTroveTx(tx, pFc, nCharge);

        tx.m_GovPull += m_Balance.m_Gov;
        pFc[2].m_Amount = tx.m_GovPull;
        pFc[2].m_Aid = m_Settings.m_AidGov;
        pFc[2].m_Consume = 0;

        if (m_Balance.m_Gov && !bUseBank)
        {
            bUseBank = true;
            nCharge += Charge::BankWithdraw();
        }

        return bUseBank;
    }

    void OnTroveMove(Method::BaseTx& tx, uint8_t bPop)
    {
        const Pair& tVals = m_MyTrove.m_pT->m_Amounts;
        tx.m_Flow.Tok.Add(m_Settings.m_TroveLiquidationReserve, !bPop);
        tx.m_Flow.Tok.Add(tVals.Tok, bPop);
        tx.m_Flow.Col.Add(tVals.Col, !bPop);
    }
};

ON_METHOD(manager, view_params)
{
    AppGlobal g;
    if (!g.Load(cid))
        return;

    Env::DocGroup gr("params");

    Env::DocAddBlob_T("oracle1", g.m_Settings.m_cidOracle1);
    Env::DocAddBlob_T("oracle2", g.m_Settings.m_cidOracle2);
    Env::DocAddNum("aidTok", g.m_Aid);
    Env::DocAddNum("aidGov", g.m_Settings.m_AidGov);
    Env::DocAddNum("redemptionHeight", g.m_Settings.m_hMinRedemptionHeight);
    Env::DocAddNum("liq_reserve", g.m_Settings.m_TroveLiquidationReserve);
    Env::DocAddNum("troves_created", g.m_Troves.m_iLastCreated);
    DocAddPair("totals", g.m_Troves.m_Totals);
    DocAddPerc("baserate", g.m_BaseRate.m_k);
    {
        Env::DocGroup gr1("stab_pool");
        Env::DocAddNum("tok", g.m_StabPool.get_TotalSell());

        Env::DocGroup gr2("reward");
        Env::DocAddNum("gov", g.m_StabPool.m_Reward.m_Remaining);
        Env::DocAddNum("hEnd", g.m_StabPool.m_Reward.m_hEnd);
    }

    AppGlobal::MyPrice price;
    uint32_t iOracle = price.Load(g);
    Env::DocAddNum("oracle_state", iOracle);

    if (iOracle)
    {
        DocAddFloat("price", price.m_Value, 4);

        if (g.m_Troves.m_Totals.Tok)
            DocAddPerc("tcr", price.ToCR(g.m_Troves.m_Totals.get_Rcr()));

        Float kHalf = Float(1u) / Float(200u);
        Float k = kHalf + g.m_BaseRate.m_k;

        {
            Float kMax(1u); // 100%
            DocAddPerc("redeem_rate", (k < kMax) ? k : kMax);
        }

        const char* szIss = "issue_rate";
        if (g.IsRecovery(price))
            Env::DocAddNum(szIss, 0u);
        else
        {
            Float kMax = kHalf * Float(10u); // 5%
            DocAddPerc(szIss, (k < kMax) ? k : kMax);
        }
    }
}

ON_METHOD(manager, view_all)
{
    AppGlobalPlus g(cid);
    if (!g.LoadPlus())
        return;

    Env::DocGroup gr("res");

    Env::DocAddNum("troves_active", g.m_ActiveTroves);

    {
        Env::DocArray gr1("troves");

        for (Trove::ID iT = g.m_Troves.m_iHead; iT; )
        {
            Env::DocGroup gr2("");

            Trove& t = g.get_T(iT);
            t.m_Amounts = g.m_RedistPool.get_UpdatedAmounts(t, g.m_EpochStorageRedist);
            g.DocAddTrove(t);

            iT = t.m_iNext;
        }
    }
}

ON_METHOD(manager, my_admin_key)
{
    PubKey pk;
    AdminKeyID kid;
    kid.get_Pk(pk);
    Env::DocAddBlob_T("admin_key", pk);
}

ON_METHOD(manager, add_stab_reward)
{
    if (!amount)
        return OnError("amount not specified");

    AppGlobalPlus g(cid);
    if (!g.LoadPlus())
        return;

    Method::AddStabPoolReward args;
    args.m_Amount = amount;

    FundsChange fc;
    fc.m_Aid = g.m_Settings.m_AidGov;
    fc.m_Amount = amount;
    fc.m_Consume = 1;

    Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), &fc, 1, nullptr, 0, "Nephrite  add stab reward", 0);
}

void PrintSurplus(const char* szName, const AppGlobalPlus& g)
{
    if (g.m_Balance.m_Amounts.Tok || g.m_Balance.m_Amounts.Col || g.m_Balance.m_Gov)
    {
        Env::DocGroup gr(szName);
        DocAddPairInternal(g.m_Balance.m_Amounts);
        Env::DocAddNum("gov", g.m_Balance.m_Gov);
    }
}

ON_METHOD(user, view)
{
    AppGlobalPlus g(cid);
    if (!g.LoadPlus())
        return;

    Env::DocGroup gr("res");
    Env::DocAddNum("troves", g.m_ActiveTroves);

    if (g.PopMyTrove())
    {
        Env::DocGroup gr2("my_trove");
        g.DocAddTrove(*g.m_MyTrove.m_pT);
    }
    PrintSurplus("surplus-t", g);

    if (g.PopMyStab())
    {
        Env::DocGroup gr1("stab");
        DocAddPairInternal(g.m_MyStab.m_Amounts);
        Env::DocAddNum("gov", g.m_MyStab.m_Gov);
    }
    PrintSurplus("surplus_s", g);
}


ON_METHOD(user, withdraw_surplus)
{
    AppGlobalPlus g(cid);
    if (!g.Load(cid)) // skip loading all troves
        return;

    if (!g.ReadBalanceAny())
        return OnError("no surplus");

    const auto& v = g.m_Balance.m_Amounts;
    assert(v.Col || v.Tok || g.m_Balance.m_Gov);

    Method::FundsAccess args;
    _POD_(args.m_Flow).SetZero();
    args.m_GovPull = 0;

    uint32_t nCharge =
        Charge::StdCall_RO() + // load global, but no modify/save
        Env::Cost::Cycle * 300;

    FundsChange pFc[3];
    g.PrepareTroveTxGov(args, pFc, nCharge);

    Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "surplus withdraw", nCharge);
}

ON_METHOD(user, upd_stab)
{
    AppGlobalPlus g(cid);
    if (!g.Load(cid)) // skip loading all troves
        return;

    Method::UpdStabPool args;
    _POD_(args.m_Flow).SetZero();

    if (g.PopMyStab())
    {
        args.m_Flow.Col.Add(g.m_MyStab.m_Amounts.Col, 0);
        args.m_Flow.Tok.Add(g.m_MyStab.m_Amounts.Tok, 0);
    }

    args.m_Flow.Tok.Add(newVal, 1);

    if (!args.m_Flow.Tok.m_Val && !args.m_Flow.Col.m_Val && !g.m_MyStab.m_Gov)
        return OnError("no change");

    args.m_NewAmount = newVal;
    args.m_GovPull = g.m_MyStab.m_Gov;

    uint32_t nCharge =
        Charge::StdCall() +
        Env::Cost::LoadVar_For(sizeof(StabPoolEntry)) +
        Env::Cost::SaveVar_For(sizeof(StabPoolEntry)) +
        Charge::StabPoolOp0 * 2 + // account for reward calculation
        g.m_EpochStorageStab.m_Charge +
        Env::Cost::Cycle * 100;

    FundsChange pFc[3];
    g.PrepareTroveTxGov(args, pFc, nCharge);

    if (newVal < g.m_MyStab.m_Amounts.Tok && g.m_Troves.m_iHead)
    {
        g.LoadPrice();
        if (!g.TestHaveValidPrice())
            return;

        // verification that 1st trove can't be liquidated.
        // Note: we didn't load all the troves (that'd be unnecessary), hence let's load it ourselves

        Env::Key_T<Trove::Key> kt;
        _POD_(kt.m_Prefix.m_Cid) = cid;
        kt.m_KeyInContract.m_iTrove = g.m_Troves.m_iHead;

        Trove tHead;
        Env::Halt_if(!Env::VarReader::Read_T(kt, tHead));

        g.m_RedistPool.Remove(tHead, g.m_EpochStorageRedist);

        nCharge +=
            Charge::Price() +
            Charge::TrovePull0() +
            g.m_EpochStorageRedist.m_Charge +
            Charge::TroveTest;
    }

    Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "update stab pool", nCharge);
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

    uint32_t nCharge =
        Charge::StdCall() +
        Env::Cost::Cycle * 200;

    bool bPopped = g.PopMyTrove();
    if (bPopped)
    {
        nCharge +=
            Charge::TrovePull(g.m_MyTrove.m_iPrev0) +
            g.m_EpochStorageRedist.m_Charge;

        g.OnTroveMove(txb, 1);
    }
    else
    {
        g.m_MyTrove.m_pT = &tNew;
        tNew.m_Amounts.Tok = tNew.m_Amounts.Col = 0;
    }

    auto& t = *g.m_MyTrove.m_pT; // alias
    Pair vals0 = t.m_Amounts;

    if (!AdjustVal(t.m_Amounts.Tok, tok, opTok) ||
        !AdjustVal(t.m_Amounts.Col, col, opCol))
        return;

    if (vals0.Tok != t.m_Amounts.Tok)
        nCharge += Env::Cost::AssetEmit;

    FundsChange pFc[2];

    if (t.m_Amounts.Tok || t.m_Amounts.Col)
    {

        if (!g.TestHaveValidPrice())
            return;

        if (tcr_mperc)
        {
            Float fTrg(tcr_mperc);
            fTrg = fTrg / Float(100000); // to fraction

            if (bAutoTok)
            {
                Float fTok = g.m_Price.C2T(t.m_Amounts.Col) / fTrg;
                t.m_Amounts.Tok = fTok; // round to a smaller side
            }
            else
            {
                Float fCol = g.m_Price.T2C(t.m_Amounts.Tok) * fTrg;
                t.m_Amounts.Col = fCol; // round to a smaller side

                if (Float(t.m_Amounts.Col) < fCol)
                    t.m_Amounts.Col++;
            }
        }

        if (t.m_Amounts.Tok < g.m_Settings.m_TroveLiquidationReserve)
            return OnError("min tok required");

        auto totals0 = g.m_Troves.m_Totals;
        auto iPrev1 = g.PushMyTrove(nCharge);

        bool bRecovery = g.IsRecovery(g.m_Price);
        if (g.IsTroveUpdInvalid(t, totals0, g.m_Price, bRecovery))
            return OnError("insufficient collateral");

        if ((vals0.Col == t.m_Amounts.Col) && (vals0.Tok == t.m_Amounts.Tok))
            return OnError("no change");

        g.OnTroveMove(txb, 0);

        Amount fee = g.get_BorrowFee(t.m_Amounts.Tok, vals0.Tok, bRecovery);
        if (fee)
        {
            nCharge += Charge::get_SendProfit();
            txb.m_Flow.Tok.Add(fee, 1);
        }

        nCharge +=
            Charge::Price() +
            Charge::TroveTest;

        if (bPopped)
        {
            Method::TroveModify args;
            Cast::Down<Method::BaseTx>(args) = txb;

            args.m_Amounts = t.m_Amounts;
            args.m_iPrev1 = iPrev1;
            g.PrepareTroveTx(args, pFc, nCharge);

            if (!bPredictOnly)
                Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "trove modify", nCharge);
        }
        else
        {
            Method::TroveOpen args;
            Cast::Down<Method::BaseTx>(args) = txb;

            args.m_Amounts = t.m_Amounts;
            args.m_iPrev1 = iPrev1;
            g.PrepareTroveTx(args, pFc, nCharge);

            if (!bPredictOnly)
                Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "trove open", nCharge);
        }

        {
            Env::DocGroup gr("prediction");
            g.DocAddTrove(t);

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
        
        g.PrepareTroveTx(args, pFc, nCharge);

        nCharge += Env::Cost::SaveVar;

        if (!bPredictOnly)
            Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &g.m_Kid, 1, "trove close", nCharge);
    }
}

ON_METHOD(user, liquidate)
{
    AppGlobalPlus g(cid);
    if (!g.LoadPlus())
        return;

    if (!g.TestHaveValidPrice())
        return;

    // Currently don't try to access balance. Prefer anonymous tx, without pk
    g.m_Kid.set_Trove();
    PubKey pkMy;
    g.m_Kid.get_Pk(pkMy);

    Global::Liquidator ctx;
    ctx.m_Price = g.m_Price;
    _POD_(ctx.m_fpLogic).SetZero();

    uint32_t nCharge =
        Charge::StdCall() +
        Charge::Price();


    uint32_t nCount = 0;
    bool bSelf = false, bRedist = false, bStab = false;

    while (g.m_Troves.m_iHead)
    {
        Pair totals0 = g.m_Troves.m_Totals;
        auto& t = g.get_T(g.m_Troves.m_iHead);

        uint32_t nCharge0 = g.m_EpochStorageRedist.m_Charge;
        g.PopTrove(0, t);

        ctx.m_Redist = false;
        ctx.m_Stab = false;

        Amount valSurplus = 0;
        if (!g.LiquidateTrove(t, totals0, ctx, valSurplus))
        {
            g.m_EpochStorageRedist.m_Charge = nCharge0;
            break;
        }

        nCharge +=
            Charge::TrovePull0() +
            Charge::TroveTest +
            Env::Cost::SaveVar; // trove del

        if (ctx.m_Stab)
        {
            nCharge += Charge::StabPoolOp0;
            bStab = true;
        }

        if (ctx.m_Redist)
        {
            nCharge += Charge::RedistPoolOp;
            bRedist = true;
        }

        if (valSurplus)
        {
            nCharge +=
                Charge::BankDeposit() +
                Env::Cost::Cycle * 500; // surplus calculation
        }

        if (_POD_(pkMy) == t.m_pkOwner)
            bSelf = true;

        if (nMaxTroves == ++nCount) // if nMaxTroves is 0 then unlimited
            break;
    }

    {
        Env::DocGroup gr("prediction");
        Env::DocAddNum("count", nCount);
        Env::DocAddNum("tok", ctx.m_fpLogic.Tok.m_Val);
        if (bSelf)
            Env::DocAddNum("self_kill", 1u);
    }

    if (!bPredictOnly)
    {
        if (!nCount)
            return OnError("no liquidations possible");

        if (bRedist)
        {
            g.m_RedistPool.MaybeSwitchEpoch(g.m_EpochStorageRedist);
            nCharge += Env::Cost::Cycle * 100;
        }

        nCharge += g.m_EpochStorageRedist.m_Charge; // both epoch switch and troves pulling

        if (bStab)
        {
            g.m_StabPool.MaybeSwitchEpoch(g.m_EpochStorageStab);

            nCharge +=
                g.m_EpochStorageStab.m_Charge +
                Env::Cost::AssetEmit +
                Env::Cost::Cycle * 100;
        }

        Method::Liquidate args;
        _POD_(args.m_pkUser).SetZero();
        args.m_Flow = ctx.m_fpLogic;
        args.m_Count = nCount;

        FundsChange pFc[2];
        g.Flow2Fc(pFc, args.m_Flow, nCharge);

        Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), nullptr, 0, "troves liquidate", nCharge);
    }
}

ON_METHOD(user, redeem)
{
    if (!val)
        return OnError("value must be nnz");

    AppGlobalPlus g(cid);
    if (!g.LoadPlus())
        return;

    if (!g.TestHaveValidPrice())
        return;

    // Currently don't try to access balance. Prefer anonymous tx, without pk

    uint32_t nCharge =
        Charge::StdCall() +
        Charge::Price();

    Global::Redeemer ctx;
    ctx.m_Price = g.m_Price;
    _POD_(ctx.m_fpLogic).SetZero();
    ctx.m_TokRemaining = val;

    Trove::ID iPrev1 = 0;

    while (g.m_Troves.m_iHead && ctx.m_TokRemaining)
    {
        Trove::ID tid = g.m_Troves.m_iHead;
        auto& t = g.get_T(tid);

        uint32_t nCharge0 = g.m_EpochStorageRedist.m_Charge;
        g.PopTrove(0, t);

        nCharge +=
            Charge::TrovePull0() +
            Charge::TroveTest +
            Env::Cost::Cycle * 800;

        if (!g.RedeemTrove(t, ctx))
        {
            g.m_EpochStorageRedist.m_Charge = nCharge0;
            break;
        }

        if (t.m_Amounts.Tok)
        {
            assert(!ctx.m_TokRemaining);
            iPrev1 = g.PushTrove(t, tid, nCharge);
        }
        else
        {
            nCharge +=
                Env::Cost::SaveVar + // del trove
                Charge::BankDeposit(); // surplus
        }
    }

    Amount fee = g.AddRedeemFee(ctx);

    {
        Env::DocGroup gr("prediction");
        Env::DocAddNum("tok", ctx.m_fpLogic.Tok.m_Val);
        Env::DocAddNum("col", ctx.m_fpLogic.Col.m_Val);
        Env::DocAddNum("fee", fee);
    }

    if (fee)
    {
        ctx.m_fpLogic.Col.Add(fee, 1);
        nCharge += Charge::get_SendProfit();
    }

    if (!bPredictOnly)
    {
        if (ctx.m_TokRemaining)
            return OnError("insufficient redeemable troves");

        Method::Redeem args;
        _POD_(args.m_pkUser).SetZero();
        args.m_Flow = ctx.m_fpLogic;
        args.m_Amount = val;
        args.m_iPrev1 = iPrev1;

        FundsChange pFc[2];
        g.Flow2Fc(pFc, args.m_Flow, nCharge);

        nCharge +=
            g.m_EpochStorageRedist.m_Charge +
            Env::Cost::AssetEmit;

        Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), nullptr, 0, "redeem", nCharge);
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
            Nephrite_##role##_##name(PAR_READ) \
            On_##role##_##name(Nephrite_##role##_##name(PAR_PASS) 0); \
            return; \
        }

#define THE_ROLE(name) \
    if (!Env::Strcmp(szRole, #name)) { \
        NephriteRole_##name(THE_METHOD) \
        return OnError("invalid Action"); \
    }

    NephriteRoles_All(THE_ROLE)

#undef THE_ROLE
#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown Role");
}

} // namespace Nephrite
