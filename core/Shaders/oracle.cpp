////////////////////////
// Oracle shader
#include "common.h"
#include "oracle.h"

#pragma pack (push, 1)

struct ProviderData
{
    Oracle::ValueType m_Value;
    PubKey m_Pk;
};

#pragma pack (pop)


export void Ctor(const Oracle::Create& r)
{
    Env::Halt_if(!r.m_Providers);

    uint8_t key = 0;
    Env::SaveVar_T(key, r.m_InitialValue);

    ProviderData pd;
    pd.m_Value = r.m_InitialValue;

    for (uint32_t i = 0; i < r.m_Providers; i++)
    {
        pd.m_Pk = r.m_pPk[i];

        Env::AddSig(pd.m_Pk);
        Env::SaveVar_T(i, pd);
    }
}

export void Dtor(void*)
{
    uint8_t key = 0;
    Env::DelVar_T(key);

    for (uint32_t i = 0; ; i++)
    {
        ProviderData pd;
        if (!Env::LoadVar_T(i, pd))
            break; // no more providers

        Env::AddSig(pd.m_Pk); // all providers must authorize the destruction
        Env::DelVar_T(i);
    }
}

export void Method_2(const Oracle::Set& r)
{
    ProviderData pd;
    Env::Halt_if(!Env::LoadVar_T(r.m_iProvider, pd)); // are you a valid provider?
    Env::AddSig(pd.m_Pk); // please authorize

    if (pd.m_Value == r.m_Value)
        return;

    pd.m_Value = r.m_Value;
    Env::SaveVar_T(r.m_iProvider, pd);

    // TODO: recalculate the new value, which is the median of all the providers (or whatever logic)
    Oracle::ValueType newVal = r.m_Value;

    uint8_t key = 0;
    Env::SaveVar_T(key, newVal);
}

export void Method_3(Oracle::Get& r)
{
    uint8_t key = 0;
    Env::LoadVar_T(key, r.m_Value); // must succeed
}
