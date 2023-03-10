#include "../common.h"
#include "contract.h"
#include "../Math.h"

namespace DaoCore2
{

BEAM_EXPORT void Ctor(const void*)
{
}

BEAM_EXPORT void Dtor(void*)
{
}

BEAM_EXPORT void Method_2(void*)
{
    // called on upgrade. N/A
}

BEAM_EXPORT void Method_3(const GetPreallocated& r)
{
    Preallocated::User::Key puk;
    _POD_(puk.m_Pk) = r.m_Pk;

    Preallocated::User pu;
    Env::Halt_if(!Env::LoadVar_T(puk, pu));

    Strict::Add(pu.m_Received, r.m_Amount);

    Height dh = Env::get_Height();
    Strict::Sub(dh, pu.m_Vesting_h0);
    dh = std::min(dh, pu.m_Vesting_dh);

    Env::Halt_if(
        MultiPrecision::From(pu.m_Received) * MultiPrecision::From(pu.m_Vesting_dh) >
        MultiPrecision::From(pu.m_Total) * MultiPrecision::From(dh));

    // ok
    if (pu.m_Received < pu.m_Total)
        Env::SaveVar_T(puk, pu);
    else
        Env::DelVar_T(puk);

    State s;
    Env::LoadVar_T((uint8_t) s.s_Key, s);
    Env::FundsUnlock(s.m_Aid, r.m_Amount);

    Env::AddSig(r.m_Pk);
}

BEAM_EXPORT void Method_4(const UpdPosFarming& r)
{
    Height h = Env::get_Height();
    Env::Halt_if(h < Preallocated::s_hLaunch); // farming starts at the same time

    Farming::State fs;
    Env::LoadVar_T((uint8_t) Farming::s_Key, fs);
    fs.m_hLast = h;

    Farming::UserPos up;
    Farming::UserPos::Key uk;
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

        State s;
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

    fs.m_WeightTotal += Farming::Weight::Calculate(up.m_Beam);

    // fin
    Env::SaveVar_T((uint8_t) Farming::s_Key, fs);

    if (up.m_Beam || up.m_BeamX)
        Env::SaveVar_T(uk, up);
    else
        Env::DelVar_T(uk);

    Env::AddSig(r.m_Pk);

}

} // namespace DaoCore2
