////////////////////////
#include "../common.h"
#include "contract.h"

namespace AssetConverter {

BEAM_EXPORT void Ctor(const void*)
{
}

BEAM_EXPORT void Dtor(void*)
{
}

struct Wrk
{
    Pool m_Pool;
    Pool::Key m_Key;

    Wrk(const Pool::ID& pid)
    {
        m_Key.m_ID = pid;
    }

    void LoadStrict()
    {
        Env::Halt_if(!Env::LoadVar_T(m_Key, m_Pool));
    }

    bool Save()
    {
        return Env::SaveVar_T(m_Key, m_Pool);
    }
};

BEAM_EXPORT void Method_2(const Method::PoolCreate& r)
{
    Env::Halt_if(r.m_Pid.m_AidFrom == r.m_Pid.m_AidTo);

    Wrk wrk(r.m_Pid);
    _POD_(wrk.m_Pool).SetZero();

    Env::Halt_if(wrk.Save()); // fail if already existed
}

BEAM_EXPORT void Method_3(const Method::PoolDeposit& r)
{
    Wrk wrk(r.m_Pid);
    wrk.LoadStrict();

    Env::FundsLock(wrk.m_Key.m_ID.m_AidTo, r.m_Amount);

    wrk.m_Pool.m_ToRemaining += r.m_Amount;

    wrk.Save();
}

BEAM_EXPORT void Method_4(const Method::PoolConvert& r)
{
    Wrk wrk(r.m_Pid);
    wrk.LoadStrict();

    Env::FundsLock(wrk.m_Key.m_ID.m_AidFrom, r.m_Amount);
    Env::FundsUnlock(wrk.m_Key.m_ID.m_AidTo, r.m_Amount);

    wrk.m_Pool.m_ToRemaining -= r.m_Amount;
    wrk.m_Pool.m_FromLocked += r.m_Amount;

    wrk.Save();
}

} // namespace AssetConverter
