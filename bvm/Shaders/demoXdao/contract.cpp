#include "../common.h"
#include "contract.h"
#include "../Math.h"

export void Ctor(const void*)
{
    if (Env::get_CallDepth() > 1)
    {
        DemoXdao::State s;

        static const char szMeta[] = "STD:SCH_VER=1;N=DemoX Coin;SN=DemoX;UN=DEMOX;NTHUN=DGROTH";
        s.m_Aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
        Env::Halt_if(!s.m_Aid);

        Env::Halt_if(!Env::AssetEmit(s.m_Aid, s.s_TotalEmission + DemoXdao::Farming::State::s_Emission, 1));

        Env::SaveVar_T((uint8_t) s.s_Key, s);
    }
}

export void Dtor(void*)
{
}

export void Method_2(void*)
{
    // called on upgrade
}

export void Method_3(const DemoXdao::LockAndGet& r)
{
    Env::Halt_if(r.m_Amount < r.s_MinLockAmount);

    DemoXdao::State s;
    Env::LoadVar_T((uint8_t) s.s_Key, s);

    Env::FundsLock(0, r.m_Amount);
    Env::FundsUnlock(s.m_Aid, s.s_ReleasePerLock);
    Env::AddSig(r.m_Pk);

    Amount val;
    if (Env::LoadVar_T(r.m_Pk, val))
        Strict::Add(val, r.m_Amount);
    else
        val = r.m_Amount;

    if (val)
        Env::SaveVar_T(r.m_Pk, val);
}

export void Method_4(const DemoXdao::UpdPosFarming& r)
{
    Height h = Env::get_Height();

    DemoXdao::Farming::State fs;
    if (Env::LoadVar_T((uint8_t) DemoXdao::Farming::s_Key, fs))
        fs.Update(h);
    else
        _POD_(fs).SetZero();
    fs.m_hLast = h;

    DemoXdao::Farming::UserPos up;
    DemoXdao::Farming::UserPos::Key uk;
    _POD_(uk.m_Pk) = r.m_Pk;

    if (Env::LoadVar_T(uk, up))
    {
        Amount newBeamX = fs.RemoveFraction(up);
        Strict::Add(up.m_BeamX, newBeamX);
    }
    else
        _POD_(up).SetZero();

    up.m_SigmaLast = fs.m_Sigma;

    if (r.m_WithdrawBeamX)
    {
        Strict::Sub(up.m_BeamX, r.m_WithdrawBeamX);

        DemoXdao::State s;
        Env::LoadVar_T((uint8_t) s.s_Key, s);

        Env::FundsUnlock(s.m_Aid, r.m_WithdrawBeamX);
    }

    if (r.m_Beam)
    {
        if (r.m_BeamLock)
        {
            Strict::Add(up.m_Beam, r.m_Beam);
            Env::FundsLock(0, r.m_Beam);
        }
        else
        {
            Strict::Sub(up.m_Beam, r.m_Beam);
            Env::FundsUnlock(0, r.m_Beam);
        }
    }

    fs.m_WeightTotal += DemoXdao::Farming::Weight::Calculate(up.m_Beam);

    // fin
    Env::SaveVar_T((uint8_t) DemoXdao::Farming::s_Key, fs);

    if (up.m_Beam || up.m_BeamX)
        Env::SaveVar_T(uk, up);
    else
        Env::DelVar_T(uk);

    Env::AddSig(r.m_Pk);

}
