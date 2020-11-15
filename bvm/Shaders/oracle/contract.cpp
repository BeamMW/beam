////////////////////////
// Oracle shader
#include "../common.h"
#include "../MergeSort.h"
#include "../Math.h"
#include "contract.h"

#pragma pack (push, 1)

struct ProviderData
{
    Oracle::ValueType m_Value;
    PubKey m_Pk;
};

struct Status
{
    Oracle::ValueType m_Value;
    uint32_t m_Count;
};

#pragma pack (pop)


export void Ctor(const Oracle::Create<0>& r)
{
    Env::Halt_if(!r.m_Providers);

    Status s;
    s.m_Value = r.m_InitialValue;
    s.m_Count = r.m_Providers;

    uint8_t sKey = 0;
    Env::SaveVar_T(sKey, s);

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
    Status s;
    uint8_t sKey = 0;
    Env::LoadVar_T(sKey, s);
    Env::DelVar_T(sKey);

    for (uint32_t i = 0; i < s.m_Count; i++)
    {
        ProviderData pd;
        Env::LoadVar_T(i, pd);

        Env::AddSig(pd.m_Pk); // all providers must authorize the destruction
        Env::DelVar_T(i);
    }
}

export void Method_2(const Oracle::Set& r)
{
    Status s;
    uint8_t sKey = 0;
    Env::LoadVar_T(sKey, s);
    Env::Halt_if(r.m_iProvider >= s.m_Count); // are you a valid provider?

    ProviderData pd;
    Env::LoadVar_T(r.m_iProvider, pd);
    Env::AddSig(pd.m_Pk); // please authorize

    if (pd.m_Value == r.m_Value)
        return; // unchanged

    pd.m_Value = r.m_Value;
    Env::SaveVar_T(r.m_iProvider, pd);

    // recalculate the new value, which is the median of all the providers
    // Merge sort requires additional buffer of the same size
    Oracle::ValueType* pData = Env::StackAlloc_T<Oracle::ValueType>(s.m_Count * 2);

    for (uint32_t i = 0; i < s.m_Count; i++)
    {
        if (i == r.m_iProvider)
            pData[i] = r.m_Value; // small shortcut, try to avoid unnecessary data load
        else
        {
            Env::LoadVar_T(i, pd);
            pData[i] = pd.m_Value;
        }
    }

    pData = MergeSort<Oracle::ValueType>::Do(pData, pData + s.m_Count, s.m_Count);

    // select median if the count is odd. For even - select the average of 2.
    uint32_t iMid = s.m_Count / 2;

    if (1 & s.m_Count)
        s.m_Value = pData[iMid];
    else
        s.m_Value = Utils::AverageUnsigned(pData[iMid], pData[iMid - 1]);

    Env::SaveVar_T(sKey, s);
}

export void Method_3(Oracle::Get& r)
{
    Status s;
    uint8_t sKey = 0;
    Env::LoadVar_T(sKey, s);

    r.m_Value = s.m_Value; // must succeed
}
