////////////////////////
// Oracle shader
#include "../common.h"
#include "../Sort.h"
#include "../Math.h"
#include "contract.h"

namespace Oracle2 {

struct MyMedian
    :public Median
{
    void Load() {
        Env::LoadVar_T((uint8_t) Tags::s_Median, *this);
    }

    void Save() {
        Env::SaveVar_T((uint8_t) Tags::s_Median, *this);
    }
};


struct MyStateFull
    :public StateFull
{
    uint32_t m_Provs;

    MyStateFull() {
        Load();
    }

    MyStateFull(bool) {} // skip auto-load

    void Load()
    {
        uint8_t k = Tags::s_StateFull;
        uint32_t nSize = Env::LoadVar(&k, sizeof(k), m_pE, sizeof(m_pE), KeyTag::Internal);
        m_Provs = nSize / sizeof(Entry);
    }

    void Save()
    {
        uint8_t k = Tags::s_StateFull;
        Env::SaveVar(&k, sizeof(k), m_pE, sizeof(Entry) * m_Provs, KeyTag::Internal);
    }

    void Assign(uint32_t iPos, uint32_t iProv, ValueType val)
    {
        m_pE[iPos].m_Val = val;
        m_pE[iPos].m_iProv = iProv;
        m_pE[iProv].m_iPos = iPos;
    }
};


BEAM_EXPORT void Ctor(const Method::Create<0>& r)
{
    MyMedian med;
    med.m_Res = r.m_InitialValue;
    med.Save();

    MyStateFull s(false);
    s.m_Provs = r.m_Providers;
    Env::Halt_if(!s.m_Provs || (s.m_Provs > StateFull::s_ProvsMax));

    for (uint32_t i = 0; i < s.m_Provs; i++)
    {
        auto& e = s.m_pE[i];
        _POD_(e.m_Pk) = r.m_pPk[i];
        s.Assign(i, i, r.m_InitialValue);
    }

    s.Save();
}

BEAM_EXPORT void Dtor(void*)
{
    Env::DelVar_T((uint8_t) Tags::s_Median);

    // all providers must authorize the destruction
    MyStateFull s;

    for (uint32_t i = 0; i < s.m_Provs; i++)
        Env::AddSig(s.m_pE[i].m_Pk);

    Env::DelVar_T((uint8_t) Tags::s_StateFull);
}

BEAM_EXPORT void Method_2(void*)
{
    // to be called on update
}

BEAM_EXPORT void Method_3(Method::Get& r)
{
    MyMedian med;
    med.Load();
    r.m_Value = med.m_Res;
}

BEAM_EXPORT void Method_4(const Method::Set& r)
{
    MyStateFull s;

    Env::Halt_if(r.m_iProvider >= s.m_Provs); // are you a valid provider?
    Env::AddSig(s.m_pE[r.m_iProvider].m_Pk); // please authorize

    auto iPos = s.m_pE[r.m_iProvider].m_iPos;

    int i = r.m_Value.cmp(s.m_pE[iPos].m_Val);
    if (!i)
        return; // unchanged

    // update the val, move positions to keep the array sorted
    while (true)
    {
        uint32_t iPosNext = (i > 0) ? (iPos + 1) : (iPos - 1);
        if (iPosNext >= s.m_Provs)
            break; // covers both cases

        ValueType vNext = s.m_pE[iPosNext].m_Val;
        int i2 = r.m_Value.cmp(vNext);
        if (i != i2)
            break;

        auto iProv = s.m_pE[iPosNext].m_iProv;
        assert(s.m_pE[iProv].m_iPos == iPosNext);

        s.Assign(iPos, iProv, vNext);

        iPos = iPosNext;
    }

    s.Assign(iPos, r.m_iProvider, r.m_Value);

    // update median
    uint32_t iMid = s.m_Provs / 2;

    MyMedian med;
    med.m_Res = s.m_pE[iMid].m_Val;

    if (!(s.m_Provs & 1))
    {
        // for even take average
        med.m_Res = med.m_Res + s.m_pE[iMid - 1].m_Val;
        med.m_Res.m_Order--;
    }

    med.Save();
}


} // namespace Oracle2
