#include "../common.h"
#include "../Math.h"
#include "contract.h"

export void Ctor(const Roulette::Params& r)
{
    Roulette::State s;
    Utils::ZeroObject(s);
    s.m_iWinner = s.s_Sectors;

    static const char szMeta[] = "roulette jetton";
    s.m_Aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
    Env::Halt_if(!s.m_Aid);

    s.m_Dealer = r.m_Dealer;
    Env::SaveVar_T((uint8_t) 0, s);
}

export void Dtor(void*)
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);

    Env::Halt_if(!Env::AssetDestroy(s.m_Aid));
    Env::DelVar_T((uint8_t) 0);

    Env::AddSig(s.m_Dealer);
}

export void Method_2(const Roulette::Spin& r)
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);

    if (r.m_PlayingSectors)
    {
        Env::Halt_if(r.m_PlayingSectors > Roulette::State::s_Sectors);
        s.m_PlayingSectors = r.m_PlayingSectors;
    }
    else
        s.m_PlayingSectors = Roulette::State::s_Sectors;

    s.m_iRound++;
    s.m_iWinner = s.s_Sectors; // invalid, i.e. no winner yet

    Env::SaveVar_T((uint8_t) 0, s);

    Env::AddSig(s.m_Dealer);
}

export void Method_3(void*)
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);
    Env::Halt_if(s.m_iWinner != s.s_Sectors); // already stopped

    // set the winner
    BlockHeader hdr;
    hdr.m_Height = Env::get_Height();
    Env::get_Hdr(hdr); // would fail if this height isn't reached yet

    uint64_t val;
    Env::Memcpy(&val, &hdr.m_Hash, sizeof(val));
    s.m_iWinner = static_cast<uint32_t>(val % s.m_PlayingSectors);

    Env::SaveVar_T((uint8_t) 0, s);

    Env::AddSig(s.m_Dealer);
}

export void Method_4(const Roulette::Bid& r)
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);
    Env::Halt_if(s.m_iWinner != s.s_Sectors); // already stopped

    Env::Halt_if(r.m_iSector >= s.s_Sectors);

    Roulette::BidInfo bi;
    Env::Halt_if(Env::LoadVar_T(r.m_Player, bi) && (bi.m_iRound == s.m_iRound)); // bid already placed for this round

    // looks good
    bi.m_iSector = r.m_iSector; // don't care if out-of-bounds for the current round with limited num of sectors
    bi.m_iRound = s.m_iRound;
    Env::SaveVar_T(r.m_Player, bi);

    Env::AddSig(r.m_Player);
}

export void Method_5(const Roulette::Take& r)
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);
    Env::Halt_if(s.m_iWinner == s.s_Sectors); // in progress

    Roulette::BidInfo bi;
    Env::Halt_if(!Env::LoadVar_T(r.m_Player, bi) || (s.m_iRound != bi.m_iRound));

    Amount nWonAmount;
    if (bi.m_iSector == s.m_iWinner)
        nWonAmount = s.s_PrizeSector;
    else
    {
        Env::Halt_if(1 & (bi.m_iSector ^ s.m_iWinner));
        nWonAmount = s.s_PrizeParity;
    }

    Env::DelVar_T(r.m_Player);

    // looks good. The amount that should be withdrawn is: val / winners

    Env::Halt_if(!Env::AssetEmit(s.m_Aid, nWonAmount, 1));
    Env::FundsUnlock(s.m_Aid, nWonAmount);
    Env::AddSig(r.m_Player);
}
