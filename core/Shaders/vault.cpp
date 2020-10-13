////////////////////////
// Simple 'vault' shader
#include "common.h"
#include "vault.h"

Amount Vault::Key::Load() const
{
    Amount ret;
    return Env::LoadVar_T(*this, ret) ? ret : 0;
}

void Vault::Key::Save(Amount amount) const
{
    Env::SaveVar_T(*this, amount);
}

export void Ctor(void*)
{
}
export void Dtor(void*)
{
}

export void Method_2(const Vault::Request& r)
{
    // deposit
    Amount total = r.Load();

    Strict::Add(total, r.m_Amount);

    r.Save(total);

    Env::FundsLock(r.m_Aid, r.m_Amount);
}

export void Method_3(const Vault::Request& r)
{
    // withdraw
    Amount total = r.Load();

    Strict::Sub(total, r.m_Amount);

    r.Save(total);

    Env::FundsUnlock(r.m_Aid, r.m_Amount);
    Env::AddSig(r.m_Account);
}
