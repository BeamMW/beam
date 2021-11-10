////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"

namespace Liquity {

template <uint8_t nTag>
struct EpochStorage
{
    static EpochKey get_Key(uint32_t iEpoch) {
        EpochKey k;
        k.m_Tag = nTag;
        k.m_iEpoch = iEpoch;
        return k;
    }

    static void Load(uint32_t iEpoch, ExchangePool::Epoch& e) {
        Env::LoadVar_T(get_Key(iEpoch), e);
    }

    static void Save(uint32_t iEpoch, const ExchangePool::Epoch& e) {
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

    static void AdjustStrict(Amount& x, Amount val, uint8_t bAdd)
    {
        if (bAdd)
            Strict::Add(x, val);
        else
            Strict::Sub(x, val);
    }

    static void AdjustBank(const FlowPair& fp, const PubKey& pk)
    {
        Env::AddSig(pk);

        if (fp.Tok.m_Val || fp.Col.m_Val)
        {
            Balance::Key kub;
            _POD_(kub.m_Pk) = pk;

            Balance ub;
            if (!Env::LoadVar_T(kub, ub))
                _POD_(ub).SetZero();

            AdjustStrict(ub.m_Amounts.Tok, fp.Tok.m_Val, fp.Tok.m_Spend);
            AdjustStrict(ub.m_Amounts.Col, fp.Col.m_Val, fp.Col.m_Spend);

            if (ub.m_Amounts.Tok || ub.m_Amounts.Col)
                Env::SaveVar_T(kub, ub);
            else
                Env::DelVar_T(kub);
        }
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

    void AdjustTxFunds(const Method::BaseTx& r) const
    {
        AdjustTxFunds(r.m_Flow.Tok, m_Aid);
        AdjustTxFunds(r.m_Flow.Col, 0);
    }

    void AdjustTxBank(const FlowPair& fpLogic, const Method::BaseTx& r, const PubKey& pk)
    {
        FlowPair fpDelta = r.m_Flow;
        fpDelta.Tok -= fpLogic.Tok;
        fpDelta.Col -= fpLogic.Col;

        AdjustBank(fpDelta, pk);
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

    void TrovePop(const Trove::Key& tk, Trove& t)
    {
        Env::Halt_if(!Env::LoadVar_T(tk, t));

        EpochStorage<Tags::s_Epoch_Redist> stor;
        m_RedistPool.Remove(t, stor);

        // just fore more safety. Theoreticall strict isn't necessary
        Strict::Sub(m_Troves.m_Totals.Tok, t.m_Amounts.Tok);
        Strict::Sub(m_Troves.m_Totals.Col, t.m_Amounts.Col);
    }

    bool TrovePush(const Trove::Key& tk, Trove& t)
    {
        Strict::Add(m_Troves.m_Totals.Tok, t.m_Amounts.Tok);
        Strict::Add(m_Troves.m_Totals.Col, t.m_Amounts.Col);

        m_RedistPool.Add(t);
        return Env::SaveVar_T(tk, t); // returns if existed already (i.e. overwriting)
    }

    void TroveModify(Trove::ID tid, const Pair* pNewVals, const PubKey* pPk, const Method::BaseTx& r)
    {
        bool bOpen = !!pPk;
        bool bClose = !pNewVals;

        Pair totals0 = m_Troves.m_Totals;
        FlowPair fpLogic;
        _POD_(fpLogic).SetZero();

        Trove t;
        Trove::Key tk;

        if (bOpen)
        {
            _POD_(t).SetZero();
            _POD_(t.m_pkOwner) = *pPk;
            tk.m_iTrove = ++m_Troves.m_iLastCreated;

            fpLogic.Tok.Add(m_Settings.m_TroveLiquidationReserve, 1);
        }
        else
        {
            tk.m_iTrove = tid;
            TrovePop(tk, t);

            fpLogic.Tok.Add(t.m_Amounts.Tok, 1);
            fpLogic.Col.Add(t.m_Amounts.Col, 0);
        }


        if (bClose)
        {
            fpLogic.Tok.Add(m_Settings.m_TroveLiquidationReserve, 0);
            Env::DelVar_T(tid);
        }
        else
        {
            fpLogic.Tok.Add(pNewVals->Tok, 0);
            fpLogic.Col.Add(pNewVals->Col, 1);

            t.m_Amounts = *pNewVals;
            Env::Halt_if(t.m_Amounts.Tok <= m_Settings.m_TroveLiquidationReserve);

            bool bExisted = TrovePush(tk, t);
            Env::Halt_if(bOpen && bExisted);

            // check cr
            Price price = get_Price();
            Float trcr = m_Troves.m_Totals.get_Rcr();

            if (price.IsBelow150(trcr))
            {
                // recovery mode.
                Env::Halt_if(!totals0.Tok); // the very first trove, should not drive us into recovery
                Env::Halt_if(totals0.get_Rcr() > trcr); // Ban txs that further decreese the tcr
            }
            else
                Env::Halt_if(price.IsBelow110(t.m_Amounts.get_Rcr()));


            if ((m_Troves.m_Totals.Tok > totals0.Tok) && !m_ProfitPool.IsEmpty())
            {

                Amount valMinted = m_Troves.m_Totals.Tok - totals0.Tok;
                Amount feeTokMin = valMinted / 200; // 0.5 percent
                Amount feeTokMax = valMinted / 20; // 5 percent

                m_BaseRate.Decay();
                Amount feeTok = feeTokMin + m_BaseRate.m_k * Float(valMinted);
                feeTok = std::min(feeTok, feeTokMax);

                Amount feeCol = price.T2C(feeTok);

                m_ProfitPool.AddValue(feeCol, 0);
                fpLogic.Col.Add(feeCol, 1);
            }
        }


        AdjustAll(r, totals0, fpLogic, t.m_pkOwner); // will invoke AddSig
    }


    //static void FundsAccountForUser(const FundsMove::Component& cLogic, const FundsMove::Component& cUser, AssetID aid)
    //{
    //    if (!cUser.m_Val)
    //        return;

    //    if (cUser.m_Spend)
    //        Env::FundsLock(aid, cUser.m_Val);
    //    else
    //        Env::FundsUnlock(aid, cUser.m_Val);

    //    cContract -= cUser;
    //}


    //void FinalyzeTx(const FundsMove& fmUser, const FundsMove& fmContract, const PubKey& pk) const
    //{
    //    auto fmDiff = fmContract;
    //    FundsAccountForUser(fmDiff.s, fmUser.s, m_Aid);
    //    FundsAccountForUser(fmDiff.b, fmUser.b, 0);

    //    if (fmDiff.s.m_Val || fmDiff.b.m_Val)
    //    {
    //        Balance::Key kub;
    //        _POD_(kub.m_Pk) = pk;

    //        Balance ub;
    //        if (!Env::LoadVar_T(kub, ub))
    //            _POD_(ub).SetZero();

    //        BalanceAdjustStrict(ub.m_Amounts.s, fmDiff.s);
    //        BalanceAdjustStrict(ub.m_Amounts.b, fmDiff.b);

    //        if (ub.m_Amounts.s || ub.m_Amounts.b)
    //            Env::SaveVar_T(kub, ub);
    //        else
    //            Env::DelVar_T(kub);
    //    }
    //}

    //void FinalyzeTxAndEmission(const FundsMove& fmUser, FundsMove& fmContract, Trove& t)
    //{
    //    const auto& s_ = fmContract.s;
    //    if (s_.m_Val && !s_.m_Spend)
    //        Env::Halt_if(!Env::AssetEmit(m_Aid, s_.m_Val, 1));

    //    FinalyzeTx(fmUser, fmContract, t.m_pkOwner);

    //    if (s_.m_Val && s_.m_Spend)
    //        Env::Halt_if(!Env::AssetEmit(m_Aid, s_.m_Val, 0));
    //}

    //void AdjustTroveAndTotals(const FundsMove& fmContract, Trove& t)
    //{
    //    auto b_ = fmContract.b;
    //    b_.m_Spend = !b_.m_Spend; // invert

    //    BalanceAdjustStrict(m_Troves.m_Totals.s, fmContract.s);
    //    BalanceAdjustStrict(m_Troves.m_Totals.b, b_);

    //    BalanceAdjustStrict(t.m_Amounts.s, fmContract.s);
    //    BalanceAdjustStrict(t.m_Amounts.b, b_);
    //}

    //Trove::ID TrovePop(Trove& t, Trove::ID iPrev)
    //{
    //    Trove::Key tk;
    //    if (iPrev)
    //    {
    //        Trove::Key tkPrev;
    //        tkPrev.m_iTrove = iPrev;
    //        Trove tPrev;
    //        Env::Halt_if(!Env::LoadVar_T(tkPrev, tPrev));

    //        tk.m_iTrove = tPrev.m_iRcrNext;
    //        Env::Halt_if(!Env::LoadVar_T(tk, t));

    //        tPrev.m_iRcrNext = t.m_iRcrNext;
    //        Env::SaveVar_T(tkPrev, tPrev); // TODO - we may omit this, if after manipulations t goes at the same position
    //       
    //    }
    //    else
    //    {
    //        tk.m_iTrove = m_Troves.m_iRcrLow;
    //        Env::Halt_if(!Env::LoadVar_T(tk, t));

    //        m_Troves.m_iRcrLow = t.m_iRcrNext;
    //    }

    //    EpochStorage<Tags::s_Epoch_Redist> stor;
    //    m_RedistPool.Remove(t, stor);

    //    return tk.m_iTrove;
    //}

    //void TrovePush(Trove& t, Trove::ID iTrove, MultiPrecision::Float rcr, Trove::ID iPrev)
    //{
    //    Trove::Key tk;

    //    if (iPrev)
    //    {
    //        Trove tPrev;
    //        tk.m_iTrove = iPrev;
    //        Env::Halt_if(!Env::LoadVar_T(tk, tPrev));

    //        tk.m_iTrove = tPrev.m_iRcrNext;
    //        tPrev.m_iRcrNext = iTrove;
    //        Env::SaveVar_T(tk, tPrev);

    //        Env::Halt_if(tPrev.get_Rcr() > rcr);
    //    }
    //    else
    //    {
    //        tk.m_iTrove = m_Troves.m_iRcrLow;
    //        m_Troves.m_iRcrLow = iTrove;
    //    }

    //    if (tk.m_iTrove)
    //    {
    //        Trove tNext;
    //        Env::Halt_if(!Env::LoadVar_T(tk, tNext));
    //        Env::Halt_if(rcr > tNext.get_Rcr());
    //    }

    //    t.m_iRcrNext = tk.m_iTrove;
    //    m_RedistPool.Add(t);

    //    tk.m_iTrove = iTrove;
    //    Env::SaveVar_T(tk, t);
    //}

    //void TrovePushValidate(Trove& t, Trove::ID iTrove, Trove::ID iPrev, const Global::Price* pPrice)
    //{
    //    MultiPrecision::Float rcr = t.get_Rcr();
    //    TrovePush(t, iTrove, rcr, iPrev);

    //    if (!pPrice)
    //        return; // forced trove update, no need to verify icr

    //    Env::Halt_if(t.m_Amounts.s < m_Settings.m_TroveMinTokens); // trove must have minimum tokens

    //    MultiPrecision::Float trcr = m_Troves.get_Trcr();
    //    if (pPrice->IsBelow150(trcr))
    //        // recovery mode
    //        Env::Halt_if(pPrice->IsBelow150(rcr));
    //    else
    //        Env::Halt_if(pPrice->IsBelow110(rcr));
    //}

    Global::Price get_Price()
    {
        Method::OracleGet args;
        Env::CallFar_T(m_Settings.m_cidOracle, args, 0);

        Global::Price ret;
        ret.m_Value = args.m_Val;
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
    MyGlobal g;
    _POD_(g).SetZero();

    _POD_(g.m_Settings) = r.m_Settings;
    g.m_StabPool.Init();
    g.m_RedistPool.Init();

    static const char szMeta[] = "STD:SCH_VER=1;N=Liquity Token;SN=Liqt;UN=LIQT;NTHUN=GROTHL";
    g.m_Aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
    Env::Halt_if(!g.m_Aid);

    Env::Halt_if(!Env::RefAdd(g.m_Settings.m_cidOracle));

    g.Save();
}

BEAM_EXPORT void Dtor(void*)
{
}

BEAM_EXPORT void Method_2(void*)
{
    // called on upgrade
}

BEAM_EXPORT void Method_3(const Method::TroveOpen& r)
{
    MyGlobal_LoadSave g;
    g.TroveModify(0, &r.m_Amounts, &r.m_pkUser, r);
}

BEAM_EXPORT void Method_4(const Method::TroveClose& r)
{
    MyGlobal_LoadSave g;
    g.TroveModify(r.m_iTrove, nullptr, nullptr, r);
}

BEAM_EXPORT void Method_5(Method::TroveModify& r)
{
    MyGlobal_LoadSave g;
    g.TroveModify(r.m_iTrove, &r.m_Amounts, nullptr, r);
}

BEAM_EXPORT void Method_6(const Method::FundsAccess& r)
{
    MyGlobal_Load g;
    g.AdjustTxFunds(r);
    g.AdjustBank(r.m_Flow, r.m_pkUser); // will invoke AddSig
}

BEAM_EXPORT void Method_7(Method::UpdStabPool& r)
{
    MyGlobal_LoadSave g;

    StabPoolEntry::Key spk;
    _POD_(spk.m_pkUser) = r.m_pkUser;

    FlowPair fpLogic;
    _POD_(fpLogic).SetZero();

    Height h = Env::get_Height();

    StabPoolEntry spe;
    if (!Env::LoadVar_T(spk, spe))
        _POD_(spe).SetZero();
    else
    {
        Env::Halt_if(spe.m_hLastModify == h);

        EpochStorage<Tags::s_Epoch_Stable> stor;

        HomogenousPool::Pair out;
        g.m_StabPool.UserDel(spe.m_User, out, stor);

        fpLogic.Tok.m_Val = out.s;
        fpLogic.Col.m_Val = out.b;
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
    g.AdjustTxBank(fpLogic, r, r.m_pkUser);
}

BEAM_EXPORT void Method_8(Method::Liquidate& r)
{
    MyGlobal_LoadSave g;

    FlowPair fpLogic;
    _POD_(fpLogic).SetZero();
    Pair totals0 = g.m_Troves.m_Totals;

    auto price = g.get_Price();

    auto pId = reinterpret_cast<const Trove::ID*>(&r + 1);
    Trove::Key tk;

    bool bStab = false, bRedist = false;

    for (uint32_t i = 0; i < r.m_Count; i++)
    {
        tk.m_iTrove = pId[i];
        Trove t;
        g.TrovePop(tk, t);

        auto rcr = t.m_Amounts.get_Rcr();
        auto cr = price.ToCR(rcr);

        bool bRecovery = 
            g.m_Troves.m_Totals.Tok &&
            price.IsBelow150(g.m_Troves.m_Totals.get_Rcr());

        if (bRecovery)
        {
        }
        else
        {
            Env::Halt_if(cr >= Global::Price::get_k110());
        }


        // TODO: deduce mode, decide if liquidation is ok

        Env::DelVar_T(tk);

        assert(t.m_Amounts.Tok >= g.m_Settings.m_TroveLiquidationReserve);

        Amount valBurn = g.get_LiquidationRewardReduce(cr);
        assert(valBurn < g.m_Settings.m_TroveLiquidationReserve);

        fpLogic.Tok.Add(g.m_Settings.m_TroveLiquidationReserve - valBurn, 0); // part of the reward, goes to the liquidator
        t.m_Amounts.Tok -= valBurn; // goes as a 'discount' for the stab pool

        if (cr > Global::Price::get_k100())
        {
            if (g.m_StabPool.LiquidatePartial(t, rcr))
                bStab = true;
        }

        if (t.m_Amounts.Tok || t.m_Amounts.Col)
        {
            bRedist = true;
            Env::Halt_if(!g.m_RedistPool.Liquidate(t));

            Strict::Add(g.m_Troves.m_Totals.Tok, t.m_Amounts.Tok);
            Strict::Add(g.m_Troves.m_Totals.Col, t.m_Amounts.Col);
        }

    }

    if (bStab)
    {
        EpochStorage<Tags::s_Epoch_Stable> stor;
        g.m_StabPool.OnPostTrade(stor);
    }

    if (bRedist)
    {
        EpochStorage<Tags::s_Epoch_Redist> stor;
        g.m_RedistPool.OnPostTrade(stor);
    }

    g.AdjustAll(r, totals0, fpLogic, r.m_pkUser);
}

BEAM_EXPORT void Method_9(Method::UpdProfitPool& r)
{
    MyGlobal_LoadSave g;

    ProfitPoolEntry::Key pk;
    _POD_(pk.m_pkUser) = r.m_pkUser;

    FlowPair fpLogic;
    _POD_(fpLogic).SetZero();

    Height h = Env::get_Height();

    ProfitPoolEntry pe;
    if (!Env::LoadVar_T(pk, pe))
        _POD_(pe).SetZero();
    else
    {
        Env::Halt_if(pe.m_hLastModify == h);

        Amount valOut;
        g.m_ProfitPool.Remove(&valOut, pe.m_User);
        fpLogic.Col.m_Val = valOut;
    }

    if (r.m_NewAmount > pe.m_User.m_Weight)
        Env::FundsLock(g.m_Settings.m_AidProfit, r.m_NewAmount - pe.m_User.m_Weight);

    if (pe.m_User.m_Weight > r.m_NewAmount)
        Env::FundsUnlock(g.m_Settings.m_AidProfit, pe.m_User.m_Weight - r.m_NewAmount);

    if (r.m_NewAmount)
    {
        pe.m_User.m_Weight = r.m_NewAmount;
        g.m_ProfitPool.Add(pe.m_User);

        pe.m_hLastModify = h;
        Env::SaveVar_T(pk, pe);
    }
    else
        Env::DelVar_T(pk);

    g.AdjustTxFunds(r);
    g.AdjustTxBank(fpLogic, r, r.m_pkUser);
}

BEAM_EXPORT void Method_10(Method::Redeem& r)
{
    MyGlobal_LoadSave g;

    Pair totals0 = g.m_Troves.m_Totals;
    FlowPair fpLogic;
    _POD_(fpLogic).SetZero();

    auto price = g.get_Price();

    auto pId = reinterpret_cast<const Trove::ID*>(&r + 1);
    Trove::Key tk;

    Amount valRemaining = r.m_Amount;

    for (uint32_t i = 0; (i < r.m_Count) && valRemaining; i++)
    {
        tk.m_iTrove = pId[i];
        Trove t;
        g.TrovePop(tk, t);

        //auto cr = price.ToCR(t.m_Amounts.get_Rcr());
        bool bCanRedeem =
            g.m_Troves.m_Totals.Tok &&
            (t.m_Amounts.get_Rcr() <= g.m_Troves.m_Totals.get_Rcr());

        Env::Halt_if(!bCanRedeem);

        assert(t.m_Amounts.Tok >= g.m_Settings.m_TroveLiquidationReserve);
        Amount valTok = t.m_Amounts.Tok - g.m_Settings.m_TroveLiquidationReserve;

        bool bFullRedeem = (valRemaining >= valTok);
        if (!bFullRedeem)
            valTok = valRemaining;

        Amount valCol = valTok / price.m_Value;
        Amount valColSurplus = t.m_Amounts.Col;
        Strict::Sub(valColSurplus, valCol);

        if (bFullRedeem)
        {
            // close trove
            Env::DelVar_T(tk);

            FlowPair fpTrove;
            _POD_(fpTrove).SetZero();
            fpTrove.Col.Add(valColSurplus, 1);
            g.AdjustBank(fpTrove, t.m_pkOwner);
        }
        else
        {
            t.m_Amounts.Tok -= valTok;
            t.m_Amounts.Col = valColSurplus;

            g.TrovePush(tk, t);
        }


        fpLogic.Tok.Add(valTok, 1);
        fpLogic.Col.Add(valCol, 0);

        valRemaining -= valTok;
    }

    if (!g.m_ProfitPool.IsEmpty() && fpLogic.Tok.m_Val)
    {
        Amount feeBase = fpLogic.Col.m_Val / 200; // redemption fee floor is 0.5 percent

        // update dynamic redeem ratio 
        g.m_BaseRate.Decay();
        Float kDrainRatio = Float(fpLogic.Tok.m_Val) / Float(fpLogic.Tok.m_Val + g.m_Troves.m_Totals.Tok);
        g.m_BaseRate.m_k = g.m_BaseRate.m_k + kDrainRatio;

        
        Amount fee = feeBase + g.m_BaseRate.m_k * Float(fpLogic.Col.m_Val);
        fee = std::min(fee, fpLogic.Col.m_Val); // fee can go as high as 100 percents

        Strict::Sub(fpLogic.Col.m_Val, fee);
        g.m_ProfitPool.AddValue(fee, 0);
    }

    g.AdjustAll(r, totals0, fpLogic, r.m_pkUser);
}


} // namespace Liquity
