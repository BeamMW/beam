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
    {
        Vault::Request rr;
        Cast::Down<Vault::Key>(rr) = key;
        rr.m_Amount = amount;

        Env::EmitLog(&rr, sizeof(rr));
    }


    if (amount)
        Env::SaveVar_T(key, amount);
    else
        Env::DelVar_T(key);
}

// Method_0 - constructor, called once when the contract is deployed
export void Ctor(void*)
{
}

// Method_1 - destructor, called once when the contract is destroyed
export void Dtor(void*)
{
}

// Public method 2 mapped to Deposit call
// NOTE: Anyone can deposit funds to any account
export void Method_2(const Vault::Deposit& r)
{
    // Load current account (public key + asset id_
    // Ammount will always be non negative
    Amount total = LoadAccount(r);

    Strict::Add(total, r.m_Amount); // add new amount to account balance while checking for overflow

    SaveAccount(r, total); // save new account balance

    Env::FundsLock(r.m_Aid, r.m_Amount); // locks specified amount of the asset from the transaction and moves it to the contract.
    // at this point the actual transaction is accessed, all operations before that were pure contract logic

    // IMPORTANT: If for any reason, this operation will fail, the entire contract operation will be reverted.
}

// Public method 3 mapped to the Withdraw call
// NOTE: Only the owner of the account may withdraw funds from it
export void Method_3(const Vault::Withdraw& r)
{
    Amount total = LoadAccount(r);

    Strict::Sub(total, r.m_Amount);

    SaveAccount(r, total);

    Env::FundsUnlock(r.m_Aid, r.m_Amount); // Contract returns funds to the transaction
    // Since in Mimblewimble all amounts need to balance out, this returned amount should have been explicitly specified in the output of the transaction.

    // Require this method call to be signed by the private key that belongs to this public key (m_Account)
    // This is required to make sure that only the owner of the account may withdraw funds from it.
    Env::AddSig(r.m_Account);
}
