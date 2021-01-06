#include "../common.h"
#include "../Math.h"
#include "contract.h"

const uint32_t g_nPerc_mp = 100000;
const uint32_t nMargin_mp_max = 10 * g_nPerc_mp;

export void Ctor(const Perpetual::Create& r)
{
	Env::Halt_if(r.m_MarginRequirement_mp > nMargin_mp_max);

	uint8_t key = 0;
	Env::SaveVar_T(key, r);
	Env::Halt_if(!Env::RefAdd(r.m_Oracle));
}

export void Dtor(void*)
{
	uint8_t key = 0;
	Perpetual::Global g;
	Env::LoadVar_T(key, g);
	Env::RefRelease(g.m_Oracle);
	Env::DelVar_T(key);
}

bool LoadAccount(const PubKey& pk, Perpetual::OfferState& s)
{
	if (Env::LoadVar_T(pk, s))
		return true;

	Utils::ZeroObject(s);
	return false;
}

export void Method_2(const Perpetual::CreateOffer& r)
{
	uint8_t key = 0;
	Perpetual::Global g;
	Env::LoadVar_T(key, g);

	Perpetual::OfferState s;
	LoadAccount(r.m_Account, s);
	Env::Halt_if(s.m_Amount1 != 0); // ensure this user doesn't have the account yet

	// check there's enough collateral
	// r.m_AmountBeam * (1 + g.m_MarginRequirement_mp / 100000) <= r.m_TotalBeams
	// r.m_AmountBeam * (100000 + g.m_MarginRequirement_mp) <= r.m_TotalBeams * 100000

	const uint32_t nAmountWords = sizeof(Amount) / sizeof(MultiPrecision::Word);

	auto a = MultiPrecision::UInt<nAmountWords>(r.m_AmountBeam) * MultiPrecision::UInt<1>(g.m_MarginRequirement_mp + g_nPerc_mp);
	auto b = MultiPrecision::UInt<nAmountWords>(r.m_TotalBeams) * MultiPrecision::UInt<1>(g_nPerc_mp);
	Env::Halt_if(a > b);

	s.m_Amount1 = r.m_TotalBeams;
	s.m_AmountBeam = r.m_AmountBeam;
	s.m_AmountToken = r.m_AmountToken;

	Env::SaveVar_T(r.m_Account, s);
}

export void Method_3(const Perpetual::CancelOffer& r)
{
}
