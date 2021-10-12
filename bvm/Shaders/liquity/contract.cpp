////////////////////////
#include "../common.h"
#include "../Math.h"
#include "contract.h"

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

    void FundsAccountForUser(Amount& valContract, uint8_t& bSpendContract, Amount valUser, uint8_t bSpendUser, AssetID aid)
    {
        if (!valUser)
            return;

        if (bSpendUser)
            Env::FundsLock(aid, valUser);
        else
            Env::FundsUnlock(aid, valUser);

        if (bSpendContract == !bSpendUser) // NOTE: bSpendUser may be zero or any non-zero value, do not compare directly
            Strict::Add(valContract, valUser);
        else
        {
            if (valContract >= valUser)
                valContract -= valUser;
            else
            {
                valContract = valUser - valContract;
                bSpendContract = !bSpendContract;
            }
        }
    }

    void FinalyzeTx(const Liquity::FundsMove& fmUser, Liquity::FundsMove& fmContract, const PubKey& pk)
    {
        FundsAccountForUser(fmContract.m_Amounts.s, fmContract.m_SpendS, fmUser.m_Amounts.s, fmUser.m_SpendS, m_Aid);
        FundsAccountForUser(fmContract.m_Amounts.b, fmContract.m_SpendB, fmUser.m_Amounts.b, fmUser.m_SpendB, 0);

        if (fmContract.m_Amounts.s || fmContract.m_Amounts.b)
        {
            Liquity::Balance::Key kub;
            _POD_(kub.m_Pk) = pk;

            Liquity::Balance ub;
            if (!Env::LoadVar_T(kub, ub))
                _POD_(ub).SetZero();

            if (fmContract.m_SpendS)
                Strict::Sub(ub.m_Amounts.s, fmContract.m_Amounts.s);
            else
                Strict::Add(ub.m_Amounts.s, fmContract.m_Amounts.s);

            if (fmContract.m_SpendB)
                Strict::Sub(ub.m_Amounts.b, fmContract.m_Amounts.b);
            else
                Strict::Add(ub.m_Amounts.b, fmContract.m_Amounts.b);

            if (ub.m_Amounts.s || ub.m_Amounts.b)
                Env::SaveVar_T(kub, ub);
            else
                Env::DelVar_T(kub);
        }


    }

};

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

BEAM_EXPORT void Ctor(void*)
{
    MyGlobal s;
    _POD_(s).SetZero();

    s.m_StabPool.Init();
    s.m_RedistPool.Init();

    static const char szMeta[] = "STD:SCH_VER=1;N=Liquity Token;SN=Liqt;UN=LIQT;NTHUN=GROTHL";
    s.m_Aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
    Env::Halt_if(!s.m_Aid);

    s.Save();
}

BEAM_EXPORT void Dtor(void*)
{
}

BEAM_EXPORT void OnMethod_2(void*)
{
    Env::Halt();
}

BEAM_EXPORT void OnMethod_3(const Liquity::OpenTrove& r)
{
    MyGlobal s;
    s.Load();

    // TODO: test amounts. Must satisfy current ratio, and allowed min/max for trove

    Liquity::Trove::Key tk;
    tk.m_iTrove = ++s.m_Troves.m_iLastCreated;

    Liquity::Trove t;
    _POD_(t).SetZero();

    _POD_(t.m_pkOwner) = r.m_pkOwner;
    t.m_Amounts = r.m_Amounts;

    Strict::Add(s.m_Troves.m_Totals.s, t.m_Amounts.s);
    Strict::Add(s.m_Troves.m_Totals.b, t.m_Amounts.b);

    s.m_RedistPool.Add(t);

    s.Save();
    Env::SaveVar_T(tk, t);

    Env::FundsLock(0, t.m_Amounts.b);
    Env::Halt_if(!Env::AssetEmit(s.m_Aid, t.m_Amounts.s, 1));
    Env::FundsUnlock(0, t.m_Amounts.s);

}

BEAM_EXPORT void OnMethod_4(const Liquity::CloseTrove& r)
{
    MyGlobal s;
    s.Load();

    Liquity::Trove::Key tk;
    tk.m_iTrove = r.m_iTrove;

    Liquity::Trove t;
    Env::Halt_if(!Env::LoadVar_T(tk, t));
    Env::AddSig(t.m_pkOwner);

    {
        EpochStorage<Liquity::Tags::s_Epoch_Redist> stor;
        s.m_RedistPool.Remove(t, stor);
    }

    s.m_Troves.m_Totals = s.m_Troves.m_Totals - t.m_Amounts;


    s.Save();
    Env::DelVar_T(tk);

    Env::FundsUnlock(0, t.m_Amounts.b);
    Env::FundsLock(0, t.m_Amounts.s);
    Env::Halt_if(!Env::AssetEmit(s.m_Aid, t.m_Amounts.s, 0));
}
