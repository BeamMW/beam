#include "common.h"
#include "dummy.h"
#include "vault.h"
#include "Math.h"

// Demonstration of the inter-shader interaction.

export void Ctor(void*)
{
    uint8_t ok = Env::RefAdd(Vault::s_ID);
    Env::Halt_if(!ok); // if the target shader doesn't exist the VM still gives a chance to run, but we don't need it.
}
export void Dtor(void*)
{
    Env::RefRelease(Vault::s_ID);
}

export void Method_2(void*)
{
    Vault::Deposit r;
    Utils::ZeroObject(r);
    r.m_Amount = 318;
    Env::CallFar_T(Vault::s_ID, r);
}

export void Method_3(Dummy::MathTest1& r)
{
    auto res =
        MultiPrecision::From(r.m_Value) *
        MultiPrecision::From(r.m_Rate) *
        MultiPrecision::From(r.m_Factor);

    //    Env::Trace("res", &res);

    auto trg = MultiPrecision::FromEx<2>(r.m_Try);

    //	Env::Trace("trg", &trg);

    r.m_IsOk = (trg <= res);
}
