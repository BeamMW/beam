////////////////////////
#include "../common.h"
#include "contract.h"

namespace PBFT {


BEAM_EXPORT void Ctor(const Method::Create& r)
{
    State::Global g;
    _POD_(g.m_Settings) = r.m_Settings;
    _POD_(g.m_Pool).SetZero();

    State::Validator::Key vk;
    State::Validator val;
    _POD_(val).SetZero();

    for (uint32_t i = 0; i < r.m_Validators; i++)
    {
        const auto& v = ((const ValidatorInit*)(&r + 1))[i];
        _POD_(vk.m_Address) = v.m_Address;

        val.m_Weight = v.m_Stake * 100ul;
        Env::Halt_if(val.m_Weight <= v.m_Stake); // stake is zero or overflow

        Env::Halt_if(Env::SaveVar_T(vk, val)); // fail if key is duplicated

        Strict::Add(g.m_Pool.m_WeightNonJailed, val.m_Weight);
    }

    auto key = State::Tag::s_Global;
    Env::SaveVar_T(key, g);
}

BEAM_EXPORT void Dtor(void*)
{
    // ignore
}

BEAM_EXPORT void Method_2(const Method::ValidatorStatusUpdate& r)
{
    State::Validator::Key vk;
    _POD_(vk.m_Address) = r.m_Address;

    State::Validator val;
    Env::Halt_if(!Env::LoadVar_T(vk, val));

    auto flags = r.m_Flags;
    uint8_t diff = val.m_Flags ^ flags;
    Env::Halt_if(!diff); // fail if no change

    typedef State::Validator::Flags F;
    Env::Halt_if(diff & ~F::Jail); // we support only jail flag atm

    State::Global g;
    auto gk = State::Tag::s_Global;
    Env::LoadVar_T(gk, g);

    if (F::Jail & flags)
        g.m_Pool.m_WeightNonJailed -= val.m_Weight; // assume valid
    else
        Strict::Add(g.m_Pool.m_WeightNonJailed, val.m_Weight);

    Env::SaveVar_T(gk, g);


    val.m_Flags = flags;
    Env::SaveVar_T(vk, val);
}

} // namespace PBFT
