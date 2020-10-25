////////////////////////
// Simple 'vault' shader
#include "common.h"
#include "vault.h"
#include "Math.h"

Amount Vault::Key::Load() const
{
    Amount ret;
    return Env::LoadVar_T(*this, ret) ? ret : 0;
}

void Vault::Key::Save(Amount amount) const
{
    if (amount)
        Env::SaveVar_T(*this, amount);
    else
        Env::DelVar_T(*this);
}

export void Ctor(void*)
{
}
export void Dtor(void*)
{
}

export void Method_2(const Vault::Deposit& r)
{
    Amount total = r.Load();

    Strict::Add(total, r.m_Amount);

    r.Save(total);

    Env::FundsLock(r.m_Aid, r.m_Amount);
}

export void Method_3(const Vault::Withdraw& r)
{
    Amount total = r.Load();

    Strict::Sub(total, r.m_Amount);

    r.Save(total);

    Env::FundsUnlock(r.m_Aid, r.m_Amount);
    Env::AddSig(r.m_Account);
}
