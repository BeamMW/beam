////////////////////////
// Simple 'vault' shader
#include "../common.h"
#include "../Math.h"
#include "contract.h"

const uint32_t nPerc_mp = 100000;
const uint32_t nMargin_mp_max = 10 * nPerc_mp;

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

export void Method_2(const Perpetual::CreateOffer& r)
{
	uint8_t key = 0;
	Perpetual::Global g;
	Env::LoadVar_T(key, g);

	Perpetual::OfferState s;
	Env::Halt_if(Env::LoadVar_T(r.m_Account, s));

	// check there's enough collateral
	/// r.m_AmountBeam * (1 + g.m_MarginRequirement_mp / 100000) <= r.m_TotalBeams

	Utils::ZeroObject(s);
	s.m_Amount1 = r.m_TotalBeams;

	Env::SaveVar_T(r.m_Account, s);
}

export void Method_3(const Perpetual::CancelOffer& r)
{
}
