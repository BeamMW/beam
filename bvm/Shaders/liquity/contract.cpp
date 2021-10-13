////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"

template <uint8_t nTag>
struct EpochStorage
{
    static Liquity::EpochKey get_Key(uint32_t iEpoch) {
        Liquity::EpochKey k;
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
    :public Liquity::Global
{
    void Load()
    {
        auto key = Liquity::Tags::s_State;
        Env::LoadVar_T(key, *this);
    }

    void Save()
    {
        auto key = Liquity::Tags::s_State;
        Env::SaveVar_T(key, *this);
    }

    static void FundsAccountForUser(Liquity::FundsMove::Component& cContract, const Liquity::FundsMove::Component& cUser, AssetID aid)
    {
        if (!cUser.m_Val)
            return;

        if (cUser.m_Spend)
            Env::FundsLock(aid, cUser.m_Val);
        else
            Env::FundsUnlock(aid, cUser.m_Val);

        cContract -= cUser;
    }

    static void BalanceAdjustStrict(Amount& x, const Liquity::FundsMove::Component& c)
    {
        if (c.m_Spend)
            Strict::Sub(x, c.m_Val);
        else
            Strict::Add(x, c.m_Val);
    }

    void FinalyzeTx(const Liquity::FundsMove& fmUser, const Liquity::FundsMove& fmContract, const PubKey& pk) const
    {
        auto fmDiff = fmContract;
        FundsAccountForUser(fmDiff.s, fmUser.s, m_Aid);
        FundsAccountForUser(fmDiff.b, fmUser.b, 0);

        if (fmDiff.s.m_Val || fmDiff.b.m_Val)
        {
            Liquity::Balance::Key kub;
            _POD_(kub.m_Pk) = pk;

            Liquity::Balance ub;
            if (!Env::LoadVar_T(kub, ub))
                _POD_(ub).SetZero();

            BalanceAdjustStrict(ub.m_Amounts.s, fmDiff.s);
            BalanceAdjustStrict(ub.m_Amounts.b, fmDiff.b);

            if (ub.m_Amounts.s || ub.m_Amounts.b)
                Env::SaveVar_T(kub, ub);
            else
                Env::DelVar_T(kub);
        }
    }

    void FinalyzeTxAndEmission(const Liquity::FundsMove& fmUser, Liquity::FundsMove& fmContract, Liquity::Trove& t)
    {
        const auto& s_ = fmContract.s;
        if (s_.m_Val && !s_.m_Spend)
            Env::Halt_if(!Env::AssetEmit(m_Aid, s_.m_Val, 1));

        FinalyzeTx(fmUser, fmContract, t.m_pkOwner);

        if (s_.m_Val && s_.m_Spend)
            Env::Halt_if(!Env::AssetEmit(m_Aid, s_.m_Val, 0));
    }

    void AdjustTroveAndTotals(const Liquity::FundsMove& fmContract, Liquity::Trove& t)
    {
        auto b_ = fmContract.b;
        b_.m_Spend = !b_.m_Spend; // invert

        BalanceAdjustStrict(m_Troves.m_Totals.s, fmContract.s);
        BalanceAdjustStrict(m_Troves.m_Totals.b, b_);

        BalanceAdjustStrict(t.m_Amounts.s, fmContract.s);
        BalanceAdjustStrict(t.m_Amounts.b, b_);
    }

    Liquity::Trove::ID TrovePop(Liquity::Trove& t, Liquity::Trove::ID iPrev)
    {
        Liquity::Trove::Key tk;
        if (iPrev)
        {
            Liquity::Trove::Key tkPrev;
            tkPrev.m_iTrove = iPrev;
            Liquity::Trove tPrev;
            Env::Halt_if(!Env::LoadVar_T(tkPrev, tPrev));

            tk.m_iTrove = tPrev.m_iRcrNext;
            Env::Halt_if(!Env::LoadVar_T(tk, t));

            tPrev.m_iRcrNext = t.m_iRcrNext;
            Env::SaveVar_T(tkPrev, tPrev); // TODO - we may omit this, if after manipulations t goes at the same position
           
        }
        else
        {
            tk.m_iTrove = m_Troves.m_iRcrLow;
            Env::Halt_if(!Env::LoadVar_T(tk, t));

            m_Troves.m_iRcrLow = t.m_iRcrNext;
        }

        EpochStorage<Liquity::Tags::s_Epoch_Redist> stor;
        m_RedistPool.Remove(t, stor);

        return tk.m_iTrove;
    }

    void TrovePush(Liquity::Trove& t, Liquity::Trove::ID iTrove, MultiPrecision::Float rcr, Liquity::Trove::ID iPrev)
    {
        Liquity::Trove::Key tk;

        if (iPrev)
        {
            Liquity::Trove tPrev;
            tk.m_iTrove = iPrev;
            Env::Halt_if(!Env::LoadVar_T(tk, tPrev));

            tk.m_iTrove = tPrev.m_iRcrNext;
            tPrev.m_iRcrNext = iTrove;
            Env::SaveVar_T(tk, tPrev);

            Env::Halt_if(tPrev.get_Rcr() > rcr);
        }
        else
        {
            tk.m_iTrove = m_Troves.m_iRcrLow;
            m_Troves.m_iRcrLow = iTrove;
        }

        if (tk.m_iTrove)
        {
            Liquity::Trove tNext;
            Env::Halt_if(!Env::LoadVar_T(tk, tNext));
            Env::Halt_if(rcr > tNext.get_Rcr());
        }

        t.m_iRcrNext = tk.m_iTrove;
        m_RedistPool.Add(t);

        tk.m_iTrove = iTrove;
        Env::SaveVar_T(tk, t);
    }

    void TrovePushValidate(Liquity::Trove& t, Liquity::Trove::ID iTrove, Liquity::Trove::ID iPrev, const Liquity::Global::Price* pPrice)
    {
        MultiPrecision::Float rcr = t.get_Rcr();
        TrovePush(t, iTrove, rcr, iPrev);

        if (!pPrice)
            return; // forced trove update, no need to verify icr

        Env::Halt_if(t.m_Amounts.s < m_Settings.m_TroveMinTokens); // trove must have minimum tokens

        MultiPrecision::Float trcr =
            MultiPrecision::Float(m_Troves.m_Totals.b) /
            MultiPrecision::Float(m_Troves.m_Totals.s);

        if (pPrice->IsBelow150(trcr))
            // recovery mode
            Env::Halt_if(pPrice->IsBelow150(rcr));
        else
            Env::Halt_if(pPrice->IsBelow110(rcr));
    }

    Liquity::Global::Price get_Price()
    {
        Liquity::Method::OracleGet args;
        Env::CallFar_T(m_Settings.m_cidOracle, args, 0);

        Liquity::Global::Price ret;
        ret.m_Value = args.m_Val;
        return ret;
    }
};

BEAM_EXPORT void Ctor(const Liquity::Method::Create& r)
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

BEAM_EXPORT void OnMethod_2(void*)
{
    // called on upgrade
}

BEAM_EXPORT void OnMethod_3(const Liquity::Method::OpenTrove& r)
{
    MyGlobal g;
    g.Load();

    Liquity::Trove t;
    _POD_(t).SetZero();
    _POD_(t.m_pkOwner) = r.m_pkOwner;

    Liquity::FundsMove fm;
    fm.s.m_Val = r.m_Amounts.s;
    fm.s.m_Spend = 0;
    fm.b.m_Val = r.m_Amounts.b;
    fm.b.m_Spend = 1;

    g.AdjustTroveAndTotals(fm, t);

    Strict::Add(fm.b.m_Val, g.m_Settings.m_CloseCompensation);
    g.FinalyzeTxAndEmission(fm, fm, t);

    auto price = g.get_Price();
    g.TrovePushValidate(t, ++g.m_Troves.m_iLastCreated, r.m_iRcrPos1, &price);

    g.Save();
}

BEAM_EXPORT void OnMethod_4(const Liquity::Method::CloseTrove& r)
{
    MyGlobal g;
    g.Load();

    Liquity::Trove::Key tk;
    Liquity::Trove t;
    tk.m_iTrove = g.TrovePop(t, r.m_iRcrPos0);

    Env::DelVar_T(tk);

    Liquity::FundsMove fm;
    fm.s.m_Val = t.m_Amounts.s;
    fm.s.m_Spend = 1;
    fm.b.m_Val = t.m_Amounts.b;
    fm.b.m_Spend = 0;

    g.AdjustTroveAndTotals(fm, t);

    Strict::Add(fm.b.m_Val, g.m_Settings.m_CloseCompensation);
    g.FinalyzeTxAndEmission(r.m_Fm, fm, t);

    g.Save();
    Env::AddSig(t.m_pkOwner);
}

BEAM_EXPORT void OnMethod_5(const Liquity::Method::FundsAccess& r)
{
    Liquity::FundsMove fm;
    _POD_(fm).SetZero();

    MyGlobal g;
    g.Load();

    g.FinalyzeTx(r.m_Fm, fm, r.m_pkUser);
    Env::AddSig(r.m_pkUser);
}

BEAM_EXPORT void OnMethod_6(Liquity::Method::ModifyTrove& r)
{
    MyGlobal g;
    g.Load();

    Liquity::Trove t;
    auto iTrove = g.TrovePop(t, r.m_iRcrPos0);

    g.AdjustTroveAndTotals(r.m_FmTrove, t);
    g.FinalyzeTxAndEmission(r.m_Fm, r.m_FmTrove, t);

    auto price = g.get_Price();
    g.TrovePushValidate(t, iTrove, r.m_iRcrPos1, &price);

    g.Save();
    Env::AddSig(t.m_pkOwner);
}

BEAM_EXPORT void OnMethod_7(Liquity::Method::UpdStabPool& r)
{
    MyGlobal g;
    g.Load();

    Liquity::StabPoolEntry::Key spk;
    _POD_(spk.m_pkUser) = r.m_pkUser;

    Liquity::FundsMove fm;
    _POD_(fm).SetZero();

    Liquity::StabPoolEntry spe;
    if (!Env::LoadVar_T(spk, spe))
        _POD_(spe).SetZero();
    else
    {
        EpochStorage<Liquity::Tags::s_Epoch_Stable> stor;

        Liquity::Pair out;
        g.m_StabPool.UserDel(spe.m_User, out, stor);

        fm.s.m_Val = out.s;
        fm.b.m_Val = out.b;
    }

    if (r.m_NewAmount)
    {
        g.m_StabPool.UserAdd(spe.m_User, r.m_NewAmount);
        Env::SaveVar_T(spk, spe);

        fm.s.Add(r.m_NewAmount, 1);
    }
    else
        Env::DelVar_T(spk);

    g.FinalyzeTx(r.m_Fm, fm, r.m_pkUser);
    g.Save();

    Env::AddSig(r.m_pkUser);
}
