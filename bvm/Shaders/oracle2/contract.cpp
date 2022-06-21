////////////////////////
// Oracle shader
#include "../common.h"
#include "contract.h"
#include "../upgradable3/contract_impl.h"
#include "../Sort.h"

namespace Oracle2 {

struct MyMedian
    :public Median
{
    void Load() {
        Env::LoadVar_T((uint8_t) Tags::s_Median, *this);
    }

    void Save() const {
        Env::SaveVar_T((uint8_t) Tags::s_Median, *this);
    }
};


struct MyState
    :public StateMax
{
    uint32_t m_Provs;

    void Load()
    {
        uint8_t k = Tags::s_StateFull;
        uint32_t nSize = Env::LoadVar(&k, sizeof(k), this, sizeof(StateMax), KeyTag::Internal);

        assert(nSize >= sizeof(State0));
        m_Provs = (nSize - sizeof(State0)) / sizeof(Entry);
    }

    void Save() const
    {
        uint8_t k = Tags::s_StateFull;
        Env::SaveVar(&k, sizeof(k), this, sizeof(State0) + sizeof(Entry) * m_Provs, KeyTag::Internal);
    }

    bool Calculate(Median& res, Height h) const
    {
        h = (h > m_Settings.m_hValidity) ? (h - m_Settings.m_hValidity) : 0;
        uint32_t nValid = 0;

        struct HI
        {
            Height m_h;
            uint32_t m_Idx;
            bool operator < (const HI& x) const { return m_h < x.m_h; }
        };

        HI pArr[s_ProvsMax];

        {
            HI pAux[s_ProvsMax];

            for (uint32_t i = 0; i < m_Provs; i++)
            {
                const auto& e = m_pE[i];
                if (e.m_hUpdated > h)
                {
                    pArr[nValid].m_h = e.m_hUpdated;
                    pArr[nValid].m_Idx = i;
                    nValid++;
                }
            }

            auto nThreshold = m_Settings.m_MinProviders - 1; // will overflow if m_MinProviders is 0, which is ok
            if (nValid <= nThreshold)
                return false;

            auto* pSorted = MergeSort<HI>::Do(pArr, pAux, nValid); // worst validity height at the beginning
            res.m_hEnd = pSorted[nValid - m_Settings.m_MinProviders].m_h + m_Settings.m_hValidity;
        }

        {
            ValueType pVals[s_ProvsMax], pAux[s_ProvsMax];

            for (uint32_t i = 0; i < nValid; i++)
            {
                auto iProv = pArr[i].m_Idx;
                pVals[i] = m_pE[iProv].m_Val;
            }

            auto* pSorted = MergeSort<ValueType>::Do(pVals, pAux, nValid); // worst validity height at the beginning

            uint32_t iMid = nValid / 2;
            res.m_Res = pSorted[iMid];
            if (!(1 & nValid))
            {
                // even number
                res.m_Res = res.m_Res + pSorted[iMid - 1];
                res.m_Res.m_Order--;
            }
        }

        return true;
    }

    void OnChanged() const
    {
        Save();

        MyMedian med;

        if (!Calculate(med, Env::get_Height()))
            _POD_(med).SetZero();

        med.Save();
    }

};

struct MyState_AutoUpd
    :public MyState
{
    MyState_AutoUpd() {
        Load();
    }

    ~MyState_AutoUpd() {
        OnChanged();
    }
};

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    r.m_Upgradable.TestNumApprovers();
    r.m_Upgradable.Save();

    MyMedian med;
    _POD_(med).SetZero();
    med.Save();

    MyState s;
    _POD_(s.m_Settings) = r.m_Settings;
    s.m_Provs = 0;

    s.Save();
}

BEAM_EXPORT void Dtor(void*)
{
}

BEAM_EXPORT void Method_3(Method::Get& r)
{
    MyMedian med;
    med.Load();
    Env::Halt_if(med.m_hEnd < Env::get_Height());
    r.m_Value = med.m_Res;
}

BEAM_EXPORT void Method_4(const Method::FeedData& r)
{
    MyState_AutoUpd s;

    Env::Halt_if(r.m_iProvider >= s.m_Provs); // are you a valid provider?
    Env::AddSig(s.m_pE[r.m_iProvider].m_Pk); // please authorize

    auto& e = s.m_pE[r.m_iProvider];
    e.m_Val = r.m_Value;
    e.m_hUpdated = Env::get_Height();
}

void TestAdminSigs(const Method::Signed& r)
{
    Upgradable3::Settings stg;
    stg.Load();
    stg.TestAdminSigs(r.m_ApproveMask);
}

BEAM_EXPORT void Method_5(const Method::SetSettings& r)
{
    TestAdminSigs(r);

    MyState_AutoUpd s;
    _POD_(s.m_Settings) = r.m_Settings;
}

BEAM_EXPORT void Method_6(const Method::ProviderAdd& r)
{
    TestAdminSigs(r);

    MyState_AutoUpd s;

    Env::Halt_if(s.m_Provs == s.s_ProvsMax);
    auto& e = s.m_pE[s.m_Provs];
    s.m_Provs++;

    _POD_(e.m_Pk) = r.m_pk;
    e.m_Val.Set0();
    e.m_hUpdated = 0;
}

BEAM_EXPORT void Method_7(const Method::ProviderDel& r)
{
    TestAdminSigs(r);

    MyState_AutoUpd s;

    Env::Halt_if(r.m_iProvider >= s.m_Provs);

    // delete it
    s.m_Provs--;
    Env::Memcpy(s.m_pE + r.m_iProvider, s.m_pE + r.m_iProvider + 1, sizeof(State0::Entry) * (s.m_Provs - r.m_iProvider)); // it's actually memmove
}


} // namespace Oracle2

namespace Upgradable3 {

    const uint32_t g_CurrentVersion = _countof(Oracle2::s_pSID) - 1;

    uint32_t get_CurrentVersion()
    {
        return g_CurrentVersion;
    }

    void OnUpgraded(uint32_t nPrevVersion)
    {
        if constexpr (g_CurrentVersion)
            Env::Halt_if(nPrevVersion != g_CurrentVersion - 1);
        else
            Env::Halt();
    }
}
