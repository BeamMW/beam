#include "../common.h"
#include "../Math.h"
#include "contract.h"

export void Ctor(const Roulette::Params& r)
{
    // Initialize global roulette state
    Roulette::State s;
    _POD_(s).SetZero();

    // Initialize winning segment (no winner)
    s.m_iWinner = s.s_Sectors;

    // Create new type of asset for this roulette
    // Since creating new asset type requires locking a deposit, this amount will automatically be substracted from the current transaction
    static const char szMeta[] = "roulette jetton";
    s.m_Aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
    Env::Halt_if(!s.m_Aid); // if for some reason we could not create an asset, the contract execution will halt (and transaction will fail)

    s.m_Dealer = r.m_Dealer;
    Env::SaveVar_T((uint8_t) 0, s); // global variable will be stored with key 00000000 (one byte set to zero)
}

// In this case, the destructor will only work if there were no winners
// When someone wins, the contract emits new jettons for the created asset type and stores the information about the winning bid
// The contract does not support burning emitted assets (and no one else has control over this asset type) 
// It is possible to add this ability and make the contract destroyable (in principle)
// Otherwise, the contract will exist forever
export void Dtor(void*)
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s); // load global state

    // asset destruction might fail, since asset operations have specific limitations. 
    // For example some amount of time should pass between asset type creation and destruction
    Env::Halt_if(!Env::AssetDestroy(s.m_Aid)); 
    Env::DelVar_T((uint8_t) 0); // remove global state

    Env::AddSig(s.m_Dealer); // Only the owner (dealer) can delete the contract

    // When the contract is destoyed the dealer gets back the deposit that was locked during asset creation
}

export void Method_2(const Roulette::Spin& r)
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);

    if (r.m_PlayingSectors) // used for debugging, limit number of playing sectors to improve chance of winning
    {
        Env::Halt_if(r.m_PlayingSectors > Roulette::State::s_Sectors);
        s.m_PlayingSectors = r.m_PlayingSectors;
    }
    else
        s.m_PlayingSectors = Roulette::State::s_Sectors;

    s.m_iRound++; // start new round to distinguish new bets
    s.m_iWinner = s.s_Sectors; // invalid, i.e. no winner yet

    Env::SaveVar_T((uint8_t) 0, s); // save global state
    // NOTE: Since the state is small, there is no need to optimize amount of writes
    // In case of large structure we might need to avoid large rewrites (> 100Kb) and store smaller portions under separate keys

    Env::AddSig(s.m_Dealer); // Only the dealer can spin the roulette
}

export void Method_3(void*) // Bets_off
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);
    Env::Halt_if(s.m_iWinner != s.s_Sectors); // already stopped

    // set the winner by a "random" hash of the last block
    // IMPORTANT: This is not really a random number, there are no random numbers in a blockchain
    // Dealer can always cheat by matching the block hash to the "winning" number.
    // Never play this roulette for real money
    BlockHeader::Info hdr;
    hdr.m_Height = Env::get_Height();
    Env::get_HdrInfo(hdr); 

    // Get first 8 byte of header hash, treat it like integer and extract the winning segment using modulo 36
    uint64_t val;
    Env::Memcpy(&val, &hdr.m_Hash, sizeof(val));
    s.m_iWinner = static_cast<uint32_t>(val % s.m_PlayingSectors);

    // TODO: Add example using contract Hash operation.

    Env::SaveVar_T((uint8_t) 0, s); // Save global state with the new winner 

    Env::AddSig(s.m_Dealer); // Make sure only the dealer can call betts off
}

export void Method_4(const Roulette::Bid& r) // Allow player to make bet
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);
    Env::Halt_if(s.m_iWinner != s.s_Sectors); // there is no active round, hence betting is not allowed

    Env::Halt_if(r.m_iSector >= s.s_Sectors); // Check that bet is in valid range

    // Make sure you can not change your bet within the same round
    Roulette::BidInfo bi;
    Env::Halt_if(Env::LoadVar_T(r.m_Player, bi) && (bi.m_iRound == s.m_iRound)); // bid already placed for this round

    // looks good
    bi.m_iSector = r.m_iSector; // don't care if out-of-bounds for the current round with limited num of sectors
    bi.m_iRound = s.m_iRound;
    Env::SaveVar_T(r.m_Player, bi); // Save player bet using public key as key

    Env::AddSig(r.m_Player); // Only the player himself can place the bet, others can not bet for other players
}

export void Method_5(const Roulette::Take& r) // take player winnings
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);
    Env::Halt_if(s.m_iWinner == s.s_Sectors); // halt if roung still in progress

    // NOTE: In the current implementation, player HAS to take the winning before the next round starts!!!
    Roulette::BidInfo bi;
    Env::Halt_if(!Env::LoadVar_T(r.m_Player, bi) || (s.m_iRound != bi.m_iRound)); 

    // In this implementation player can win either by guessing the exact number (big win)
    // or by just guessing the parity of the sector (small win)
    Amount nWonAmount;
    if (bi.m_iSector == s.m_iWinner)
        nWonAmount = s.s_PrizeSector;
    else
    {
        Env::Halt_if(1 & (bi.m_iSector ^ s.m_iWinner));
        nWonAmount = s.s_PrizeParity;
    }

    Env::DelVar_T(r.m_Player); // to avoid double extraction of the winnings

    // looks good. The amount that should be withdrawn is: val / winners

    // Now we emit the required amount of the asset type controlled by the roulette
    // These assets still belong to the contract
    Env::Halt_if(!Env::AssetEmit(s.m_Aid, nWonAmount, 1)); 

    // Move emitted assets from the contract balance to the current transaction
    Env::FundsUnlock(s.m_Aid, nWonAmount);
    Env::AddSig(r.m_Player); // Make sure only the player can take his / hers winnings
}
