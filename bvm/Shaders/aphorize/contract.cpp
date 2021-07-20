#include "../common.h"
#include "../Math.h"
#include "contract.h"


BEAM_EXPORT void Ctor(const Aphorize::Create& r)
{
    Aphorize::State s;
    _POD_(s.m_Cfg) = r.m_Cfg;
    s.m_hRoundStart = 0;

    uint8_t key = s.s_Key;
    Env::SaveVar_T(key, s);
}

BEAM_EXPORT void Dtor(void*)
{
    // irrelevant
}

void LoadAccount(Aphorize::Account& x, const PubKey& pk, const Aphorize::State& s)
{
    if (Env::LoadVar_T(pk, x))
    {
        if (x.m_hRoundStart == s.m_hRoundStart)
            return;

        if (x.m_Locked)
        {
            Strict::Add(x.m_Avail, x.m_Locked);
            x.m_Locked = 0;
        }
    }
    else
    {
        x.m_Avail = 0;
        x.m_Locked = 0;
    }
    x.m_hRoundStart = s.m_hRoundStart;
}


void FinishRound(Aphorize::StatePlus& s, uint32_t nVariants)
{
    Aphorize::Events::RoundEnd evt;
    evt.m_iWinner = 0;
    
    for (uint32_t i = 1; i < nVariants; i++)
        if (s.m_pArr[i] > s.m_pArr[evt.m_iWinner])
            evt.m_iWinner = i;

    PubKey pk;

    if (s.m_pArr[evt.m_iWinner])
        Env::LoadVar_T(evt.m_iWinner, pk);
    else
    {
        // all variants were banned. No winner. Prize goes to the moderator
        evt.m_iWinner = static_cast<uint32_t>(-1);
        _POD_(pk) = s.m_Cfg.m_Moderator;
    }

    Aphorize::Account acc;
    LoadAccount(acc, pk, s);

    Amount prize = s.m_Cfg.m_PriceSubmit * nVariants;

    Strict::Add(acc.m_Avail, prize);
    Env::SaveVar_T(pk, acc);

    uint8_t key = Aphorize::Events::RoundEnd::s_Key;
    Env::EmitLog_T(key, evt);
}

uint32_t LoadState(Aphorize::StatePlus& s)
{
    uint8_t key = s.s_Key;
    auto ret = Env::LoadVar(&key, sizeof(key), &s, sizeof(s), KeyTag::Internal);
    uint32_t nVariants = (ret - sizeof(Aphorize::State)) / sizeof(Amount);

    if (nVariants)
    {
        Height h = Env::get_Height();
        if (h - s.m_hRoundStart >= s.m_Cfg.m_hPeriod)
        {
            FinishRound(s, nVariants);

            s.m_hRoundStart = 0;
            nVariants = 0;
        }
    }

    return nVariants;
}

void SaveState(const Aphorize::StatePlus& s, uint32_t nVariants)
{
    uint8_t key = s.s_Key;
    Env::SaveVar(&key, sizeof(key), &s, sizeof(Aphorize::State) + nVariants * sizeof(Amount), KeyTag::Internal);
}

BEAM_EXPORT void Method_2(const Aphorize::Submit& r)
{
    Aphorize::StatePlus s;
    auto nVariants = LoadState(s);

    Env::Halt_if(nVariants >= s.s_MaxVariants);
    if (!nVariants)
        // start new round
        s.m_hRoundStart = Env::get_Height();

    Env::Halt_if(r.m_Len > r.s_MaxLen);

    Env::SaveVar_T(nVariants, r.m_Pk);

    s.m_pArr[nVariants++] = s.m_Cfg.m_PriceSubmit; // auto vote for yourself
    SaveState(s, nVariants);

    Env::FundsLock(0, s.m_Cfg.m_PriceSubmit);

    uint8_t key = Aphorize::Events::Submitted::s_Key;
    Env::EmitLog(&key, sizeof(key), &r + 1, r.m_Len, KeyTag::Internal);
}

BEAM_EXPORT void Method_3(const Aphorize::Vote& r)
{
    Aphorize::StatePlus s;
    auto nVariants = LoadState(s);

    Env::Halt_if((r.m_iVariant >= nVariants) || !s.m_pArr[r.m_iVariant]);
    Strict::Add(s.m_pArr[r.m_iVariant], r.m_Amount);
    SaveState(s, nVariants);

    Env::FundsLock(0, r.m_Amount);

    Aphorize::Account acc;
    LoadAccount(acc, r.m_Pk, s);

    Strict::Add(acc.m_Locked, r.m_Amount);
    Env::SaveVar_T(r.m_Pk, acc);
}

BEAM_EXPORT void Method_4(const Aphorize::Ban& r)
{
    Aphorize::StatePlus s;
    auto nVariants = LoadState(s);

    Env::Halt_if((r.m_iVariant >= nVariants) || !s.m_pArr[r.m_iVariant]);

    s.m_pArr[r.m_iVariant] = 0; // ban
    SaveState(s, nVariants);

    Env::AddSig(s.m_Cfg.m_Moderator);

    uint8_t key = Aphorize::Events::Ban::s_Key;
    Env::EmitLog_T(key, r.m_iVariant);
}

BEAM_EXPORT void Method_5(const Aphorize::Withdraw& r)
{
    Aphorize::StatePlus s;
    LoadState(s);

    Aphorize::Account acc;
    LoadAccount(acc, r.m_Pk, s);

    Strict::Sub(acc.m_Avail, r.m_Amount);
    Env::SaveVar_T(r.m_Pk, acc);

    Env::FundsUnlock(0, r.m_Amount);
    Env::AddSig(r.m_Pk);
}

