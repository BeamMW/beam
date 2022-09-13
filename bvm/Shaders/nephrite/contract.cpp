////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"
#include "../upgradable3/contract_impl.h"
#include "../dao-vault/contract.h"
#include "../oracle2/contract.h"


namespace Nephrite {

struct EpochStorage
{
    static EpochKey get_Key(uint32_t iEpoch) {
        EpochKey k;
        k.m_Tag = Tags::s_Epoch_Stable;
        k.m_iEpoch = iEpoch;
        return k;
    }

    template <uint32_t nDims>
    static void Load(uint32_t iEpoch, HomogenousPool::Epoch<nDims>& e) {
        Env::LoadVar_T(get_Key(iEpoch), e);
    }

    template <uint32_t nDims>
    static void Save(uint32_t iEpoch, const HomogenousPool::Epoch<nDims>& e) {
        Env::SaveVar_T(get_Key(iEpoch), e);
    }

    static void Del(uint32_t iEpoch) {
        Env::DelVar_T(get_Key(iEpoch));
    }
};

struct MyGlobal
    :public Global
{
    void Load()
    {
        auto key = Tags::s_State;
        Env::LoadVar_T(key, *this);
    }

    void Save()
    {
        auto key = Tags::s_State;
        Env::SaveVar_T(key, *this);
    }

    static void AdjustStrict(Amount& x, const Flow& f)
    {
        if (f.m_Spend)
            Strict::Add(x, f.m_Val);
        else
            Strict::Sub(x, f.m_Val);
    }

    static void AdjustBank(const FlowPair& fp, const PubKey& pk, const Flow* pGov = nullptr)
    {
        Env::AddSig(pk);
        AdjustBankNoSig(fp, pk, pGov);
    }

    static void AdjustBankNoSig(const FlowPair& fp, const PubKey& pk, const Flow* pGov = nullptr)
    {
        if (pGov && !pGov->m_Val)
            pGov = nullptr;

        if (fp.Tok.m_Val || fp.Col.m_Val || pGov)
        {
            Balance::Key kub;
            _POD_(kub.m_Pk) = pk;

            Balance ub;
            if (!Env::LoadVar_T(kub, ub))
                _POD_(ub).SetZero();

            AdjustStrict(ub.m_Amounts.Tok, fp.Tok);
            AdjustStrict(ub.m_Amounts.Col, fp.Col);

            if (pGov)
                AdjustStrict(ub.m_Gov, *pGov);

            if (ub.m_Amounts.Tok || ub.m_Amounts.Col || ub.m_Gov)
                Env::SaveVar_T(kub, ub);
            else
                Env::DelVar_T(kub);
        }
    }

    static void ExtractSurplusCol(Amount val, const Trove& t)
    {
        FlowPair fp;
        _POD_(fp.Tok).SetZero();
        fp.Col.m_Spend = 1;
        fp.Col.m_Val = val;
        AdjustBankNoSig(fp, t.m_pkOwner);
    }


    static void AdjustTxFunds(const Flow& f, AssetID aid)
    {
        if (f.m_Val)
        {
            if (f.m_Spend)
                Env::FundsLock(aid, f.m_Val);
            else
                Env::FundsUnlock(aid, f.m_Val);
        }
    }

    void SendProfit(Amount val, AssetID aid, Flow& fFlow)
    {
        if (val)
        {
            DaoVault::Method::Deposit args;
            args.m_Amount = val;
            args.m_Aid = aid;

            Env::CallFar_T(m_Settings.m_cidDaoVault, args);

            fFlow.Add(val, 0);
        }
    }

    void AdjustTxFunds(const Method::BaseTx& r) const
    {
        AdjustTxFunds(r.m_Flow.Tok, m_Aid);
        AdjustTxFunds(r.m_Flow.Col, 0);
    }

    void AdjustTxBank(const FlowPair& fpLogic, const Method::BaseTx& r, const PubKey& pk, const Flow* pGov = nullptr)
    {
        FlowPair fpDelta = r.m_Flow;
        fpDelta.Tok -= fpLogic.Tok;
        fpDelta.Col -= fpLogic.Col;

        AdjustBank(fpDelta, pk, pGov);
    }

    void AdjustAll(const Method::BaseTx& r, const Pair& totals0, const FlowPair& fpLogic, const PubKey& pk)
    {
        if (m_Troves.m_Totals.Tok > totals0.Tok)
            Env::Halt_if(!Env::AssetEmit(m_Aid, m_Troves.m_Totals.Tok - totals0.Tok, 1));

        AdjustTxFunds(r);

        if (totals0.Tok > m_Troves.m_Totals.Tok)
            Env::Halt_if(!Env::AssetEmit(m_Aid, totals0.Tok - m_Troves.m_Totals.Tok, 0));

        AdjustTxBank(fpLogic, r, pk);
    }

    Trove::ID TrovePop(Trove::ID iPrev, Trove& t)
    {
        Trove tPrev;
        Trove::Key tk;
        Trove::ID iTrove;

        if (iPrev)
        {
            tk.m_iTrove = iPrev;
            Env::Halt_if(!Env::LoadVar_T(tk, tPrev));
            iTrove = tPrev.m_iNext;
        }
        else
            iTrove = m_Troves.m_iHead;

        tk.m_iTrove = iTrove;
        Env::Halt_if(!Env::LoadVar_T(tk, t));

        m_RedistPool.Remove(t);

        // just for more safety. Theoretically strict isn't necessary
        Strict::Sub(m_Troves.m_Totals.Tok, t.m_Amounts.Tok);
        Strict::Sub(m_Troves.m_Totals.Col, t.m_Amounts.Col);

        if (iPrev)
        {
            tPrev.m_iNext = t.m_iNext;
            tk.m_iTrove = iPrev;
            Env::SaveVar_T(tk, tPrev);
        }
        else
            m_Troves.m_iHead = t.m_iNext;

        return iTrove;
    }

    int TroveLoadCmp(const Trove::Key& tk, Trove& t, const Trove& tRef)
    {
        Env::Halt_if(!Env::LoadVar_T(tk, t));

        auto vals = m_RedistPool.get_UpdatedAmounts(t);

        return vals.CmpRcr(tRef.m_Amounts);
    }

    void TrovePush(Trove::ID iTrove, Trove& t, Trove::ID iPrev)
    {
        Strict::Add(m_Troves.m_Totals.Tok, t.m_Amounts.Tok);
        Strict::Add(m_Troves.m_Totals.Col, t.m_Amounts.Col);

        m_RedistPool.Add(t);

        Trove::Key tk;

        if (iPrev)
        {
            Trove tPrev;
            tk.m_iTrove = iPrev;
            int iCmp = TroveLoadCmp(tk, tPrev, t);

            Env::Halt_if(iCmp > 0);

            t.m_iNext = tPrev.m_iNext;
            tPrev.m_iNext = iTrove;

            Env::SaveVar_T(tk, tPrev);

        }
        else
        {
            t.m_iNext = m_Troves.m_iHead;
            m_Troves.m_iHead = iTrove;
        }

        if (t.m_iNext)
        {
            Trove tNext;
            tk.m_iTrove = t.m_iNext;
            int iCmp = TroveLoadCmp(tk, tNext, t);

            Env::Halt_if(iCmp < 0);
        }

        tk.m_iTrove = iTrove;
        Env::SaveVar_T(tk, t);
    }

    void TroveModify(Trove::ID iPrev0, Trove::ID iPrev1, const Pair* pNewVals, const PubKey* pPk, Method::BaseTx& r)
    {
        bool bOpen = !!pPk;
        bool bClose = !pNewVals;

        Pair totals0 = m_Troves.m_Totals;
        FlowPair fpLogic;
        _POD_(fpLogic).SetZero();

        Trove t;
        Trove::ID iTrove;

        if (bOpen)
        {
            _POD_(t).SetZero();
            _POD_(t.m_pkOwner) = *pPk;
            iTrove = ++m_Troves.m_iLastCreated;

            fpLogic.Tok.Add(m_Settings.m_TroveLiquidationReserve, 1);
        }
        else
        {
            iTrove = TrovePop(iPrev0, t);

            fpLogic.Tok.Add(t.m_Amounts.Tok, 1);
            fpLogic.Col.Add(t.m_Amounts.Col, 0);
        }


        if (bClose)
        {
            fpLogic.Tok.Add(m_Settings.m_TroveLiquidationReserve, 0);
            Trove::Key tk;
            tk.m_iTrove = iTrove;
            Env::DelVar_T(tk);
        }
        else
        {
            fpLogic.Tok.Add(pNewVals->Tok, 0);
            fpLogic.Col.Add(pNewVals->Col, 1);

            t.m_Amounts = *pNewVals;
            Env::Halt_if(t.m_Amounts.Tok <= m_Settings.m_TroveLiquidationReserve);

            TrovePush(iTrove, t, iPrev1);

            // check cr
            Price price = get_Price();

            bool bRecovery = IsRecovery(price);
            Env::Halt_if(IsTroveUpdInvalid(t, totals0, price, bRecovery));

            Amount feeTok = get_BorrowFee(m_Troves.m_Totals.Tok, totals0.Tok, bRecovery);
            SendProfit(feeTok, m_Aid, r.m_Flow.Tok);
        }


        AdjustAll(r, totals0, fpLogic, t.m_pkOwner); // will invoke AddSig
    }

    static bool get_PriceInternal(Oracle2::Method::Get& args, const ContractID& cidOracle)
    {
        Env::CallFar_T(cidOracle, args);
        // ban zero price. Our floating point division-by-zero may be exploited
        return args.m_IsValid && Price::IsSane(args.m_Value);
    }

    Global::Price get_Price()
    {
        Oracle2::Method::Get args;
        Env::Halt_if(
            !get_PriceInternal(args, m_Settings.m_cidOracle1) &&
            !get_PriceInternal(args, m_Settings.m_cidOracle2)
        );

        Global::Price ret;
        ret.m_Value = args.m_Value;
        return ret;
    }
};

struct MyGlobal_Load
    :public MyGlobal
{
    MyGlobal_Load() {
        Load();
    }
};

struct MyGlobal_LoadSave
    :public MyGlobal_Load
{
    ~MyGlobal_LoadSave()
    {
#ifdef HOST_BUILD
        if (std::uncaught_exceptions())
            return;
#endif // HOST_BUILD
        Save();
    }
};

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    r.m_Upgradable.TestNumApprovers();
    r.m_Upgradable.Save();

    MyGlobal g;
    _POD_(g).SetZero();

    _POD_(g.m_Settings) = r.m_Settings;
    g.m_StabPool.Init();
    g.m_RedistPool.Reset();

    static const char szMeta[] = "STD:SCH_VER=1;N=Nephrite Token;SN=Nph;UN=NPH;NTHUN=GROTHN";
    g.m_Aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
    Env::Halt_if(!g.m_Aid);

    Env::Halt_if(!Env::RefAdd(g.m_Settings.m_cidDaoVault));
    Env::Halt_if(!Env::RefAdd(g.m_Settings.m_cidOracle1));
    Env::Halt_if(!Env::RefAdd(g.m_Settings.m_cidOracle2));

    g.Save();
}

BEAM_EXPORT void Dtor(void*)
{
}

BEAM_EXPORT void Method_3(Method::TroveOpen& r)
{
    MyGlobal_LoadSave g;
    g.TroveModify(0, r.m_iPrev1, &r.m_Amounts, &r.m_pkUser, r);
}

BEAM_EXPORT void Method_4(Method::TroveClose& r)
{
    MyGlobal_LoadSave g;
    g.TroveModify(r.m_iPrev0, 0, nullptr, nullptr, r);
}

BEAM_EXPORT void Method_5(Method::TroveModify& r)
{
    MyGlobal_LoadSave g;
    g.TroveModify(r.m_iPrev0, r.m_iPrev1, &r.m_Amounts, nullptr, r);
}

BEAM_EXPORT void Method_6(const Method::FundsAccess& r)
{
    MyGlobal_Load g;

    Flow fGov;
    fGov.m_Spend = 0;
    fGov.m_Val = r.m_GovPull;
    g.AdjustTxFunds(fGov, g.m_Settings.m_AidGov);

    g.AdjustTxFunds(r);
    g.AdjustBank(r.m_Flow, r.m_pkUser, &fGov); // will invoke AddSig
}

BEAM_EXPORT void Method_7(Method::UpdStabPool& r)
{
    MyGlobal_LoadSave g;

    StabPoolEntry::Key spk;
    _POD_(spk.m_pkUser) = r.m_pkUser;

    FlowPair fpLogic;
    _POD_(fpLogic).SetZero();

    Flow fGov;
    fGov.m_Spend = 0;
    fGov.m_Val = r.m_GovPull;
    g.AdjustTxFunds(fGov, g.m_Settings.m_AidGov);

    Height h = Env::get_Height();
    g.m_StabPool.AddReward(h);

    StabPoolEntry spe;
    if (!Env::LoadVar_T(spk, spe))
        _POD_(spe).SetZero();
    else
    {
        Env::Halt_if(spe.m_hLastModify == h);

        EpochStorage stor;

        Global::StabilityPool::User::Out out;
        g.m_StabPool.UserDel(spe.m_User, out, stor);

        fpLogic.Tok.m_Val = out.m_Sell;
        fpLogic.Col.m_Val = out.m_pBuy[0];

        if ((r.m_NewAmount < out.m_Sell) && g.m_Troves.m_iHead)
        {
            // ensure no pending liquidations
            Global::Price price = g.get_Price();
            Env::Halt_if(price.IsRecovery(g.m_Troves.m_Totals));

            Trove::Key tk;
            tk.m_iTrove = g.m_Troves.m_iHead;
            Trove t;
            Env::Halt_if(!Env::LoadVar_T(tk, t));

            auto vals = g.m_RedistPool.get_UpdatedAmounts(t);
            auto cr = price.ToCR(vals.get_Rcr());
            Env::Halt_if((cr < Global::Price::get_k110()));
        }

        fGov.Add(out.m_pBuy[1], 1);
    }

    if (r.m_NewAmount)
    {
        spe.m_hLastModify = h;
        g.m_StabPool.UserAdd(spe.m_User, r.m_NewAmount);
        Env::SaveVar_T(spk, spe);

        fpLogic.Tok.Add(r.m_NewAmount, 1);
    }
    else
        Env::DelVar_T(spk);

    g.AdjustTxFunds(r);
    g.AdjustTxBank(fpLogic, r, r.m_pkUser, &fGov);
}

BEAM_EXPORT void Method_8(Method::Liquidate& r)
{
    MyGlobal_LoadSave g;

    g.m_StabPool.AddReward(Env::get_Height());

    Global::Liquidator ctx;
    ctx.m_Price = g.get_Price();
    _POD_(ctx.m_fpLogic).SetZero();
    Pair totals0 = g.m_Troves.m_Totals;

    Trove::Key tk;

    for (uint32_t i = 0; i < r.m_Count; i++)
    {
        Pair totals1 = g.m_Troves.m_Totals;
        Trove t;
        tk.m_iTrove = g.TrovePop(0, t);
        Env::DelVar_T(tk);

        Amount valSurplus = 0;
        Env::Halt_if(!g.LiquidateTrove(t, totals1, ctx, valSurplus));

        if (valSurplus)
            g.ExtractSurplusCol(valSurplus, t);
    }

    if (ctx.m_Stab)
    {
        EpochStorage stor;
        g.m_StabPool.OnPostTrade(stor);
    }

    g.AdjustAll(r, totals0, ctx.m_fpLogic, r.m_pkUser);
}

BEAM_EXPORT void Method_9(Method::Redeem& r)
{
    MyGlobal_LoadSave g;

    Env::Halt_if(Env::get_Height() < g.m_Settings.m_hMinRedemptionHeight);

    Pair totals0 = g.m_Troves.m_Totals;

    Global::Redeemer ctx;
    ctx.m_Price = g.get_Price();
    _POD_(ctx.m_fpLogic).SetZero();

    Trove::Key tk;

    for (ctx.m_TokRemaining = r.m_Amount; ctx.m_TokRemaining; )
    {
        Trove t;
        tk.m_iTrove = g.TrovePop(0, t);

        Env::Halt_if(!g.RedeemTrove(t, ctx));

        if (t.m_Amounts.Tok)
            g.TrovePush(tk.m_iTrove, t, r.m_iPrev1);
        else
        {
            // close trove
            Env::DelVar_T(tk);
            g.ExtractSurplusCol(t.m_Amounts.Col, t);
        }
    }

    Amount fee = g.AddRedeemFee(ctx);
    g.SendProfit(fee, 0, r.m_Flow.Col);

    g.AdjustAll(r, totals0, ctx.m_fpLogic, r.m_pkUser);
}

BEAM_EXPORT void Method_10(Method::AddStabPoolReward& r)
{
    MyGlobal_LoadSave g;
    auto& x = g.m_StabPool.m_Reward; // alias

    Height h = Env::get_Height();
    if (h >= x.m_hEnd)
    {
        x.m_hLast = h;
        x.m_hEnd = h + 1440 * 365 * 2; // 2 years
    }

    Strict::Add(x.m_Remaining, r.m_Amount);
    Env::FundsLock(g.m_Settings.m_AidGov, r.m_Amount);
}

} // namespace Nephrite

namespace Upgradable3 {

    const uint32_t g_CurrentVersion = _countof(Nephrite::s_pSID) - 1;

    uint32_t get_CurrentVersion()
    {
        return g_CurrentVersion;
    }

    void OnUpgraded(uint32_t nPrevVersion)
    {
        if constexpr (g_CurrentVersion)
            Env::Halt_if(nPrevVersion != g_CurrentVersion - 1);
        else
            Env::Halt();
    }
}
