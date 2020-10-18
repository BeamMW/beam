#include "common.h"
#include "StableCoin.h"
#include "oracle.h"
#include "Math.h"

#pragma pack (push, 1)

struct State
{
	ContractID m_RateOracle;
	AssetID m_Aid;
	uint64_t m_CollateralizationRatio;
};

struct Position
{
	Amount m_Beam;
	Amount m_Asset;

	void Load(const PubKey&);
	void Save(const PubKey&) const;

	bool IsOk(uint64_t rate, const State& s) const;
	void Update(const StableCoin::FundsIO&, const State& s);
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
	State s;
	uint8_t key = 0;
	Env::LoadVar_T(key, s);

	Position pos;
	pos.Load(arg.m_Pk);

	Oracle::Get data;
	Env::CallFar(s.m_RateOracle, data.s_iMethod, &data);

	pos.Update(arg, s);
	Env::Halt_if(!pos.IsOk(data.m_Value, s));

	pos.Save(arg.m_Pk);
}

void Position::Load(const PubKey& pk)
{
	if (!Env::LoadVar_T(pk, *this))
		Env::Memset(this, 0, sizeof(*this));
}

void Position::Save(const PubKey& pk) const
{
	if (Env::Memis0(this, sizeof(*this)))
		Env::DelVar_T(pk);
	else
		Env::SaveVar_T(pk, *this);
}

bool Position::IsOk(uint64_t rate, const State& s) const
{
	// m_Beam * rate >= m_Asset * collRatio
	return
		(
			MultiPrecision::From(m_Beam) *
			MultiPrecision::From(rate)
		) >= (
			MultiPrecision::From(m_Asset) *
			MultiPrecision::From(s.m_CollateralizationRatio)
		);
}

void Position::Update(const StableCoin::FundsIO& arg, const State& s)
{
	if (arg.m_BeamAdd)
	{
		Env::FundsLock(0, arg.m_Beam);
		Strict::Add(m_Beam, arg.m_Beam);
	}
	else
	{
		Strict::Sub(m_Beam, arg.m_Beam);
		Env::FundsUnlock(0, arg.m_Beam);
	}

	if (arg.m_AssetAdd)
	{
		Env::FundsLock(s.m_Aid, arg.m_Asset);
		Strict::Sub(m_Asset, arg.m_Asset);
		Env::Halt_if(!Env::AssetEmit(s.m_Aid, arg.m_Asset, 0));
	}
	else
	{
		Env::Halt_if(!Env::AssetEmit(s.m_Aid, arg.m_Asset, 1));
		Strict::Add(m_Asset, arg.m_Asset);
		Env::FundsUnlock(s.m_Aid, arg.m_Asset);
	}
}
