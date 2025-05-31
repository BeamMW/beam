////////////////////////
#include "../common.h"
#include "../Math.h" // Strict
#include "contract_l1.h"
#include "../upgradable3/contract_impl.h"

namespace L2Tst1_L1 {

#pragma pack (push, 1)
    struct MyState_NoLoad :public State
    {
        void Load()
        {
            Env::LoadVar_T((uint8_t) Tags::s_State, *this);
        }

        void Save()
        {
            Env::SaveVar_T((uint8_t) Tags::s_State, *this);
        }

    };

    struct MyState :public MyState_NoLoad
    {
        MyState() { Load(); }
    };

#pragma pack (pop)

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    r.m_Upgradable.TestNumApprovers();
    r.m_Upgradable.Save();

    MyState_NoLoad s;
    _POD_(s).SetZero();
    _POD_(s.m_Settings) = r.m_Settings;
    s.Save();

    Env::Halt_if(!r.m_Validators || (r.m_Validators > Validator::s_Max));

    const Validator* pV = (const Validator*) (&r + 1);

    auto key = Tags::s_Validators;
    Env::SaveVar(&key, sizeof(key), pV, sizeof(Validator) * r.m_Validators, KeyTag::Internal);
    
}

struct Validators
{
    uint32_t m_Number;
    Validator m_pV[Validator::s_Max];

    bool IsQuorumReached(uint32_t n) const
    {
        return Validator::IsQuorumReached(n, m_Number);
    }

    void Load()
    {
        auto key = Tags::s_Validators;
        auto res = Env::LoadVar(&key, sizeof(key), m_pV, sizeof(m_pV), KeyTag::Internal);

        assert(res && !(res % sizeof(Validator)) && (res < sizeof(m_pV)));
        m_Number = res / sizeof(Validator);
        assert(m_Number);
    }

    void Test(uint32_t nApproveMask) const
    {
        uint32_t nCount = 0;

        for (uint32_t i = 0; i < m_Number; i++)
        {
            if (1u & (nApproveMask >> i))
            {
                Env::AddSig(m_pV[i].m_pk);
                nCount++;
            }
        }

        Env::Halt_if(!IsQuorumReached(nCount));
    }
};

BEAM_EXPORT void Dtor(void*)
{
    // ignore
}

BEAM_EXPORT void Method_3(const Method::UserStake& r)
{
    Env::Halt_if(!r.m_Amount);

    MyState s;

    Height h = Env::get_Height();
    Env::Halt_if(h >= s.m_Settings.m_hPreEnd);

    User::Key uk;
    _POD_(uk.m_pk) = r.m_pkUser;

    User u;
    if (Env::LoadVar_T(uk, u))
        Strict::Add(u.m_Stake, r.m_Amount);
    else
        u.m_Stake = r.m_Amount;

    Env::SaveVar_T(uk, u);

    Env::FundsLock(s.m_Settings.m_aidStaking, r.m_Amount);
}

void OnBridgeOp(const Method::BridgeOp& r, uint8_t nTag)
{
    BridgeOpSave::Key k;
    k.m_Tag = nTag;
    _POD_(k.m_pk) = r.m_pk;

    BridgeOpSave val;
    val.m_Height = Env::get_Height();
    Env::Halt_if(Env::SaveVar_T(k, val)); // fail if already existed

    Env::EmitLog_T(nTag, r);
}

BEAM_EXPORT void Method_4(const Method::BridgeExport& r)
{
    OnBridgeOp(r, Tags::s_BridgeExp);
    Env::FundsLock(r.m_Aid, r.m_Amount);
}

BEAM_EXPORT void Method_5(const Method::BridgeImport& r)
{
    OnBridgeOp(r, Tags::s_BridgeImp);
    Env::FundsUnlock(r.m_Aid, r.m_Amount);

    Env::AddSig(r.m_pk);

    Validators vals;
    vals.Load();
    vals.Test(r.m_ApproveMask);
}

} // namespace L2Tst1_L1

namespace Upgradable3 {

    const uint32_t g_CurrentVersion = _countof(L2Tst1_L1::s_pSID) - 1;

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
