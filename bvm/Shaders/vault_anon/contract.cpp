////////////////////////
#include "../common.h"
#include "contract.h"


namespace VaultAnon {

BEAM_EXPORT void Ctor(void*)
{
}

BEAM_EXPORT void Dtor(void*)
{
}

BEAM_EXPORT void Method_2(const Method::SetAccount& r)
{
    Account::Key k;
    _POD_(k.m_Pk) = r.m_Pk;
    Env::AddSig(r.m_Pk);

    if (r.m_TitleLen)
    {
        Env::Halt_if(r.m_TitleLen > Account::s_TitleLenMax);
        Env::SaveVar(&k, sizeof(k), &r + 1, r.m_TitleLen, KeyTag::Internal);
    }
    else
        Env::Halt_if(!Env::DelVar_T(k));
}

BEAM_EXPORT void Method_3(const Method::Send& r)
{
    Deposit::Key k;
    _POD_(k.m_SpendKey) = r.m_SpendKey;

    Env::Halt_if(Env::SaveVar_T(k, r.m_Deposit)); // fail if exists

    Env::FundsLock(r.m_Deposit.m_Aid, r.m_Deposit.m_Amount);
}

BEAM_EXPORT void Method_4(const Method::Receive& r)
{
    Deposit::Key k;
    _POD_(k.m_SpendKey) = r.m_SpendKey;
    Env::AddSig(r.m_SpendKey);

    Deposit d;
    Env::Halt_if(!Env::LoadVar_T(k, d));
    Env::DelVar_T(k);

    Env::FundsUnlock(d.m_Aid, d.m_Amount);
}


} // namespace Nephrite
