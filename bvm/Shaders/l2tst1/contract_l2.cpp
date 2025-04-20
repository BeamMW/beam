////////////////////////
#include "../common.h"
#include "contract_l2.h"

namespace L2Tst1_L2 {


BEAM_EXPORT void Ctor(void*)
{
    // ignore
}

BEAM_EXPORT void Dtor(void*)
{
    // ignore
}

//__attribute__((noinline))
void OnBridgeOp(const Method::BridgeOp& r, uint8_t bEmit)
{
    Env::Halt_if(!r.m_Amount);

    Env::AddSig(r.m_pk);
    Env::Halt_if(!Env::AssetEmit(r.m_Aid, r.m_Amount, bEmit));

    Env::EmitLog_T(bEmit, r);
}

BEAM_EXPORT void Method_2(const Method::BridgeEmit& r)
{
    OnBridgeOp(r, 1);
    Env::FundsUnlock(r.m_Aid, r.m_Amount);
}

BEAM_EXPORT void Method_3(const Method::BridgeBurn& r)
{
    Env::FundsLock(r.m_Aid, r.m_Amount);
    OnBridgeOp(r, 0);
}

} // namespace L2Tst1
