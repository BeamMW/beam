////////////////////////
// Simple 'vault' shader
#include "../common.h"
#include "../Math.h"
#include "contract.h"

Amount LoadAccount(const Vault::Key& key)
{
    Amount ret;
    return Env::LoadVar_T(key, ret) ? ret : 0;
}

void SaveAccount(const Vault::Key& key, Amount amount)
{
    if (amount)
        Env::SaveVar_T(key, amount);
    else
        Env::DelVar_T(key);
}

export void Ctor(void*)
{
}
export void Dtor(void*)
{
}

export void Method_2(const Vault::Deposit& r)
{
    Amount total = LoadAccount(r);

    Strict::Add(total, r.m_Amount);

    SaveAccount(r, total);

    Env::FundsLock(r.m_Aid, r.m_Amount);
}

export void Method_3(const Vault::Withdraw& r)
{
    Amount total = LoadAccount(r);

    Strict::Sub(total, r.m_Amount);

    SaveAccount(r, total);

    Env::FundsUnlock(r.m_Aid, r.m_Amount);
    Env::AddSig(r.m_Account);
}
