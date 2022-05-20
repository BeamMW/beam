////////////////////////
#include "../common.h"
#include "contract.h"
#include "../Math.h"
namespace VaultAnon {

BEAM_EXPORT void Ctor(void*)
{
}

BEAM_EXPORT void Dtor(void*)
{
}

struct MyAccount
    :public Account
{
    KeyMax m_Key;
    uint32_t m_KeySize;

    void Load(const Method::BaseTx& tx)
    {
        Env::Halt_if(tx.m_SizeCustom > sizeof(m_Key.m_pCustom));
        m_KeySize = sizeof(Account::Key0) + tx.m_SizeCustom;
        Env::Memcpy(&m_Key, &tx.m_Key, m_KeySize);

        if (Env::LoadVar(&m_Key, m_KeySize, &m_Amount, sizeof(m_Amount), KeyTag::Internal) != sizeof(m_Amount))
            m_Amount = 0;
    }

    void Save()
    {
        if (m_Amount)
            Env::SaveVar(&m_Key, m_KeySize, &m_Amount, sizeof(m_Amount), KeyTag::Internal);
        else
            Env::SaveVar(&m_Key, m_KeySize, nullptr, 0, KeyTag::Internal);
    }
};

BEAM_EXPORT void Method_2(const Method::Deposit& r)
{
    MyAccount x;
    x.Load(r);

    Strict::Add(x.m_Amount, r.m_Amount);

    x.Save();

    Env::FundsLock(r.m_Key.m_Aid, r.m_Amount);
}

BEAM_EXPORT void Method_3(const Method::Withdraw& r)
{
    MyAccount x;
    x.Load(r);

    Strict::Sub(x.m_Amount, r.m_Amount);

    x.Save();

    Env::FundsUnlock(r.m_Key.m_Aid, r.m_Amount);
    Env::AddSig(r.m_Key.m_pkOwner);
}

} // namespace VaultAnon
