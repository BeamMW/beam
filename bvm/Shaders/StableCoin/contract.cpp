#include "../common.h"
#include "../Math.h"
#include "../oracle/contract.h"
#include "../vault/contract.h"
#include "contract.h"

using StableCoin::Balance;

#pragma pack (push, 1)

// Global contract state var
struct State
{
	ContractID m_RateOracle; // The source of rate info
	AssetID m_Aid; // the asset we've created to represent the coin
	uint64_t m_CollateralizationRatio;
	Height m_BiddingDuration;

	// consume/release funds to/from tx, emit/burn the stable coin accordingly
	void MoveFunds(const Balance& change, const Balance::Direction&) const;
};

struct Worker
{
	State m_State;
	Oracle::Get m_CurrentRate;

	void Load()
	{
		uint8_t key = 0;
		Env::LoadVar_T(key, m_State);
		Env::CallFar_T(m_State.m_RateOracle, m_CurrentRate);
	}

	bool IsStable(const Balance&, MultiPrecision::UInt<4>* pReserve = nullptr) const;
};

struct Position
{
	Balance m_Balance;

	struct Bid
		:public Balance
	{
		Balance m_Best; // Set to 0 if no bidding
		Height m_Height; // of the most recently updated bid
		PubKey m_Pk; // the bidder
	} m_Bid;

	void Load(const PubKey&);
	void Save(const PubKey&) const;

	void ChangeBy(const Balance& change, const Balance::Direction&); // update balance
	static void ChangeBy_(Balance& trg, const Balance& change, const Balance::Direction&);

	void ReleaseBidStrict(const Worker&);
};

#pragma pack (pop)

export void Ctor(const StableCoin::Create<0>& arg)
{
	Env::Halt_if(!Env::RefAdd(arg.m_RateOracle)); // Lock oracle contract
	Env::Halt_if(!Env::RefAdd(Vault::s_CID)); // Lock vault contract

	State s;
	s.m_Aid = Env::AssetCreate(arg.m_pMetaData, arg.m_nMetaData); // Create the asset to represent the coin
	Env::Halt_if(!s.m_Aid);

	s.m_RateOracle = arg.m_RateOracle; // Save params to global state
	s.m_CollateralizationRatio = arg.m_CollateralizationRatio;
	s.m_BiddingDuration = arg.m_BiddingDuration;

	uint8_t key = 0; // key of the global state
	Env::SaveVar_T(key, s);
}

export void Dtor(void*)
{
	State s;
	uint8_t key = 0;
	Env::LoadVar_T(key, s);

	// Unlock the contracts we depend on
	Env::RefRelease(Vault::s_CID);
	Env::RefRelease(s.m_RateOracle);

	Env::Halt_if(!Env::AssetDestroy(s.m_Aid)); // Destroy the coin asset. Will fail unless entirely burned
	Env::DelVar_T(key);
}

void UpdatePosInternal(Worker& wrk, const StableCoin::UpdatePosition& arg)
{
	Position pos;
	pos.Load(arg.m_Pk);

	pos.ChangeBy(arg.m_Change, arg.m_Direction); // Update pos by the specified amounts
	Env::Halt_if(!wrk.IsStable(pos.m_Balance)); // Is it stable?

	if (pos.m_Bid.m_Height) // is there bidding underway?
		pos.ReleaseBidStrict(wrk); // cancel it

	Env::AddSig(arg.m_Pk); // The owner must sign this tx
	pos.Save(arg.m_Pk);
}

export void Method_2(const StableCoin::UpdatePosition& arg)
{
	Worker wrk;
	wrk.Load();

	UpdatePosInternal(wrk, arg);

	wrk.m_State.MoveFunds(arg.m_Change, arg.m_Direction); // move funds in/out the tx
}

export void Method_3(const StableCoin::PlaceBid& arg)
{
	Worker wrk;
	wrk.Load();

	Position pos;
	pos.Load(arg.m_PkTarget);

	Height h = Env::get_Height();

	Balance::Direction d;
	d.m_BeamAdd = 1;
	d.m_AssetAdd = 1;

	Balance futureBalance = pos.m_Balance; // the balance after it receives the bidder funds
	Position::ChangeBy_(futureBalance, arg.m_Bid, d);

	if (pos.m_Bid.m_Height)
	{
		MultiPrecision::UInt<4> reserve, reserveOlder;
		Env::Halt_if(!wrk.IsStable(futureBalance, &reserve)); // is this bid good enough?

		Balance olderBalance = pos.m_Balance;
		Position::ChangeBy_(olderBalance, pos.m_Bid, d);

		if (wrk.IsStable(olderBalance, &reserveOlder)) // is older bid good enough?
		{
			Env::Halt_if(h - pos.m_Bid.m_Height > wrk.m_State.m_BiddingDuration); // is bidding over?
			Env::Halt_if(reserve <= reserveOlder); // is our bid any better?
		}

		pos.ReleaseBidStrict(wrk);
	}
	else
	{
		Env::Halt_if(wrk.IsStable(pos.m_Balance)); // stable position can't be targeted for bidding
		Env::Halt_if(!wrk.IsStable(futureBalance)); // is this bid good enough?
	}

	wrk.m_State.MoveFunds(arg.m_Bid, d);

	// the bid is ok. Save it
	pos.m_Bid.m_Best = arg.m_Bid;
	pos.m_Bid.m_Pk = arg.m_PkBidder;
	pos.m_Bid.m_Height = h;

	pos.Save(arg.m_PkTarget);
	Env::AddSig(arg.m_PkBidder);
}

export void Method_4(const StableCoin::Grab& arg)
{
	Worker wrk;
	wrk.Load();

	Position pos;
	pos.Load(arg.m_PkTarget);

	Height h = Env::get_Height();
	Env::Halt_if(!pos.m_Bid.m_Height); // was there a bid?
	Env::Halt_if(h - pos.m_Bid.m_Height <= wrk.m_State.m_BiddingDuration); // is bidding over?

	Balance::Direction d;
	d.m_BeamAdd = 1;
	d.m_AssetAdd = 1;
	pos.ChangeBy(pos.m_Bid, d);
	Env::Halt_if(!wrk.IsStable(pos.m_Balance)); // is this bid still good?

	// Looks good. Terminate this position, and merge it with winner's
	Env::DelVar_T(arg.m_PkTarget);

	StableCoin::UpdatePosition arg2;
	arg2.m_Change = pos.m_Balance;
	arg2.m_Direction.m_BeamAdd = 1;
	arg2.m_Direction.m_AssetAdd = 0;
	arg2.m_Pk = pos.m_Bid.m_Pk;

	UpdatePosInternal(wrk, arg2);
}

void Position::Load(const PubKey& pk)
{
	if (!Env::LoadVar_T(pk, *this))
		Utils::ZeroObject(*this);
}

void Position::Save(const PubKey& pk) const
{
	if (Env::Memis0(this, sizeof(*this)))
		Env::DelVar_T(pk);
	else
		Env::SaveVar_T(pk, *this);
}

bool Worker::IsStable(const Balance& x, MultiPrecision::UInt<4>* pReserve /* = nullptr */) const
{
	// m_Beam * rate >= m_Asset * collRatio
	auto valCollateral =
		MultiPrecision::From(x.m_Beam) *
		MultiPrecision::From(m_CurrentRate.m_Value);

	auto valDebt =
		MultiPrecision::From(x.m_Asset) *
		MultiPrecision::From(m_State.m_CollateralizationRatio);

	if (valCollateral < valDebt)
		return false;

	if (pReserve)
		pReserve->SetSub(valCollateral, valDebt);
	return true;
}

void State::MoveFunds(const Balance& change, const Balance::Direction& d) const
{
	if (d.m_BeamAdd)
		Env::FundsLock(0, change.m_Beam);
	else
		Env::FundsUnlock(0, change.m_Beam);

	if (d.m_AssetAdd)
	{
		Env::FundsLock(m_Aid, change.m_Asset);
		Env::Halt_if(!Env::AssetEmit(m_Aid, change.m_Asset, 0));
	}
	else
	{
		Env::Halt_if(!Env::AssetEmit(m_Aid, change.m_Asset, 1));
		Env::FundsUnlock(m_Aid, change.m_Asset);
	}
}

void Position::ChangeBy(const Balance& change, const Balance::Direction& d)
{
	ChangeBy_(m_Balance, change, d);
}

void Position::ChangeBy_(Balance& trg, const Balance& change, const Balance::Direction& d)
{
	if (d.m_BeamAdd)
		Strict::Add(trg.m_Beam, change.m_Beam);
	else
		Strict::Sub(trg.m_Beam, change.m_Beam);

	if (d.m_AssetAdd)
		Strict::Sub(trg.m_Asset, change.m_Asset);
	else
		Strict::Add(trg.m_Asset, change.m_Asset);
}

void Position::ReleaseBidStrict(const Worker& wrk)
{
	// Release the amounts locked by the bid
	Balance::Direction d;
	d.m_BeamAdd = 0;
	d.m_AssetAdd = 0;

	wrk.m_State.MoveFunds(m_Bid.m_Best, d);

	// transfer the released amounts to the appropriate account in the vault
	Vault::Deposit arg;
	arg.m_Account = m_Bid.m_Pk;

	if (m_Bid.m_Asset)
	{
		arg.m_Aid = wrk.m_State.m_Aid;
		arg.m_Amount = m_Bid.m_Asset;
		Env::CallFar_T(Vault::s_CID, arg);
	}

	if (m_Bid.m_Beam)
	{
		arg.m_Aid = 0;
		arg.m_Amount = m_Bid.m_Beam;
		Env::CallFar_T(Vault::s_CID, arg);
	}

	Utils::ZeroObject(m_Bid);
}
