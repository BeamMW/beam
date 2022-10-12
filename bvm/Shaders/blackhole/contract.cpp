////////////////////////
// BlackHole
#include "../common.h"
#include "contract.h"

namespace BlackHole {

BEAM_EXPORT void Ctor(void*)
{
}

BEAM_EXPORT void Dtor(void*)
{
}

BEAM_EXPORT void Method_2(const Method::Deposit& r)
{
	Env::FundsLock(r.m_Aid, r.m_Amount);
}

} // namespace BlackHole
