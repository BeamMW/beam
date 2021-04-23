#include "../common.h"
#include "../Math.h"
#include "contract.h"

export void Ctor(const Faucet::Params& r)
{
    Env::SaveVar_T((uint8_t) 0, r);
}
export void Dtor(void*)
{
    Env::DelVar_T((uint8_t) 0);
}

export void Method_2(const Faucet::Deposit& r)
{
    Env::FundsLock(r.m_Aid, r.m_Amount);
}

export void Method_3(const Faucet::Withdraw& r)
{
    Height h = Env::get_Height();

    Faucet::Params pars;
    Env::LoadVar_T((uint8_t) 0, pars);

    Faucet::AccountData ad;
    bool bLoaded = Env::LoadVar_T(r.m_Key, ad);
    bool bEmpty = !bLoaded || (h - ad.m_h0 >= pars.m_BacklogPeriod);

    if (r.m_Amount)
    {
        if (bEmpty)
        {
            ad.m_h0 = h;
            ad.m_Amount = r.m_Amount;
        }
        else
            Strict::Add(ad.m_Amount, r.m_Amount);

        Env::Halt_if(ad.m_Amount > pars.m_MaxWithdraw);
        Env::SaveVar_T(r.m_Key, ad);

        Env::FundsUnlock(r.m_Key.m_Aid, r.m_Amount); // would fail if not enough funds in the contract
    }
    else
    {
        if (bLoaded && bEmpty)
            Env::DelVar_T(r.m_Key);
    }

    Env::AddSig(r.m_Key.m_Account);
}
