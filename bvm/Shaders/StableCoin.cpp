#include "common.h"
#include "StableCoin.h"
#include "oracle.h"
#include "Math.h"

using StableCoin::Balance;

#pragma pack (push, 1)

struct State
{
	ContractID m_RateOracle;
	AssetID m_Aid;
	uint64_t m_CollateralizationRatio;

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
		Env::CallFar(m_State.m_RateOracle, m_CurrentRate.s_iMethod, &m_CurrentRate);
	}

	bool IsStable(const Balance&) const;
};

struct Position
{
	Balance m_Balance;

	void Load(const PubKey&);
	void Save(const PubKey&) const;

	void ChangeBy(const Balance& change, const Balance::Direction&);
	static void ChangeBy_(Balance& trg, const Balance& change, const Balance::Direction&);
};

#pragma pack (pop)

export void Ctor(const StableCoin::Ctor<0>& arg)
{
	Env::Halt_if(!Env::RefAdd(arg.m_RateOracle));

	State s;
	s.m_Aid = Env::AssetCreate(arg.m_pMetaData, arg.m_nMetaData);
	Env::Halt_if(!s.m_Aid);

	s.m_RateOracle = arg.m_RateOracle;
	s.m_CollateralizationRatio = arg.m_CollateralizationRatio;

	uint8_t key = 0;
	Env::SaveVar_T(key, s);
}

export void Dtor(void*)
{
	State s;
	uint8_t key = 0;
	Env::LoadVar_T(key, s);

	Env::RefRelease(s.m_RateOracle);

	Env::Halt_if(!Env::AssetDestroy(s.m_Aid));
	Env::DelVar_T(key);
}


export void Method_2(const StableCoin::UpdatePosition& arg)
{
	Worker wrk;
	wrk.Load();

	Position pos;
	pos.Load(arg.m_Pk);

	pos.ChangeBy(arg.m_Change, arg.m_Direction);
	Env::Halt_if(!wrk.IsStable(pos.m_Balance));

	Env::AddSig(arg.m_Pk);
	pos.Save(arg.m_Pk);

	wrk.m_State.MoveFunds(arg.m_Change, arg.m_Direction);
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

bool Worker::IsStable(const Balance& x) const
{
	// m_Beam * rate >= m_Asset * collRatio
	auto valCollateral =
		MultiPrecision::From(x.m_Beam) *
		MultiPrecision::From(m_CurrentRate.m_Value);

	auto valDebt =
		MultiPrecision::From(x.m_Asset) *
		MultiPrecision::From(m_State.m_CollateralizationRatio);

	return (valCollateral >= valDebt);
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
