#include "../common.h"
#include "../Math.h"
#include "contract.h"

namespace Faucet2 {

struct MyState :public State {
    static uint8_t get_Key() { return s_Key; }
    void Load() { Env::LoadVar_T(get_Key(), *this); }
    void Save() { Env::SaveVar_T(get_Key(), *this); }
};

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    MyState s;
    _POD_(s.m_Params) = r.m_Params;
    _POD_(s.m_Epoch).SetZero();
    s.m_Enabled = true;
    s.Save();
}
BEAM_EXPORT void Dtor(void*)
{
    MyState s;
    s.Load();
    Env::AddSig(s.m_Params.m_pkAdmin);
    Env::DelVar_T(s.get_Key());
}

BEAM_EXPORT void Method_2(const Method::Deposit& r)
{
    Env::FundsLock(r.m_Value.m_Aid, r.m_Value.m_Amount);
}

BEAM_EXPORT void Method_3(const Method::Withdraw& r)
{
    MyState s;
    s.Load();

    Env::Halt_if(!s.m_Enabled);

    s.UpdateEpoch(Env::get_Height());

    Strict::Sub(s.m_Epoch.m_Amount, r.m_Value.m_Amount);
    Env::FundsUnlock(r.m_Value.m_Aid, r.m_Value.m_Amount); // would fail if not enough funds in the contract

    s.Save();
}

BEAM_EXPORT void Method_4(const Method::AdminCtl& r)
{
    MyState s;
    s.Load();
    Env::AddSig(s.m_Params.m_pkAdmin);

    s.m_Enabled = r.m_Enable;
    s.Save();
}

BEAM_EXPORT void Method_5(const Method::AdminWithdraw& r)
{
    MyState s;
    s.Load();
    Env::AddSig(s.m_Params.m_pkAdmin);

    Env::FundsUnlock(r.m_Value.m_Aid, r.m_Value.m_Amount); // would fail if not enough funds in the contract
}

} // namespace Faucet2