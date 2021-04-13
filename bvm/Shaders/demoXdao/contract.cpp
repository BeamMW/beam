#include "../common.h"
#include "contract.h"
#include "../Math.h"

export void Ctor(const void*)
{
    if (Env::get_CallDepth() > 1)
    {
        DemoXdao::State s;

        static const char szMeta[] = "STD:SCH_VER=1;N=DemoX Coin;SN=DemoX;UN=DEMOX;NTHUN=DGROTH";
        s.m_Aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
        Env::Halt_if(!s.m_Aid);

        Env::Halt_if(!Env::AssetEmit(s.m_Aid, s.s_TotalEmission, 1));

        Env::SaveVar_T((uint8_t) 0, s);
    }
}

export void Dtor(void*)
{
}

export void Method_2(void*)
{
    // called on upgrade
}

export void Method_3(const DemoXdao::LockAndGet& r)
{
    Env::Halt_if(r.m_Amount < r.s_MinLockAmount);

    DemoXdao::State s;
    Env::LoadVar_T((uint8_t) 0, s);

    Env::FundsLock(0, r.m_Amount);
    Env::FundsUnlock(s.m_Aid, s.s_ReleasePerLock);
    Env::AddSig(r.m_Pk);

    Amount val;
    if (Env::LoadVar_T(r.m_Pk, val))
        Strict::Add(val, r.m_Amount);
    else
        val = r.m_Amount;

    if (val)
        Env::SaveVar_T(r.m_Pk, val);
}

