////////////////////////
#include "pbft_stat.h"

namespace PBFT_STAT {

using namespace I_PBFT;

struct ValidatorCtx_NoLoad
{
    State::Validator::Key m_Key;
    State::Validator m_Val;

    ValidatorCtx_NoLoad(const Address& addr)
    {
        _POD_(m_Key.m_Address) = addr;
    }

    bool SaveRaw()
    {
        return Env::SaveVar_T(m_Key, m_Val);
    }

    void SaveFlex()
    {
        bool bKill =
            (State::Validator::Status::Tombed == m_Val.m_Status) &&
            !m_Val.m_Weight;

        if (bKill)
            Env::DelVar_T(m_Key);
        else
            SaveRaw();
    }
};

struct ValidatorCtx
    :public ValidatorCtx_NoLoad
{
    ValidatorCtx(const Address& addr)
        :ValidatorCtx_NoLoad(addr)
    {
        Env::Halt_if(!Env::LoadVar_T(m_Key, m_Val));
    }
};

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    for (uint32_t i = 0; i < r.m_Count; i++)
    {
        const auto& vi = r.get_VI()[i];
        Env::Halt_if(!vi.m_Weight);

        ValidatorCtx_NoLoad vctx(vi.m_Address);
        _POD_(vctx.m_Val).SetZero();
        vctx.m_Val.m_Weight = vi.m_Weight;

        Env::Halt_if(vctx.SaveRaw()); // fail if already existed
    }
}

BEAM_EXPORT void Dtor(void*)
{
    // ignore
}

BEAM_EXPORT void Method_2(void*)
{
    Env::Halt(); // TODO: upgrade
}

BEAM_EXPORT void Method_3(const I_PBFT::Method::ValidatorStatusUpdate& r)
{
    ValidatorCtx vctx(r.m_Address);
    Env::Halt_if(r.m_Status == vctx.m_Val.m_Status);

    typedef State::Validator::Status Status;
    switch (r.m_Status)
    {
    default:
        Env::Halt();
        // no break;

    case Status::Active:
    case Status::Jailed:
    case Status::Suspended:
    case Status::Tombed:
        Env::Halt_if(Status::Tombed == vctx.m_Val.m_Status);
        vctx.m_Val.m_Status = r.m_Status;
        break;

    case Status::Slash:
        {
            if (Status::Tombed != vctx.m_Val.m_Status)
                vctx.m_Val.m_Status = Status::Suspended; // Slash is a transition, not a state.

            // slash by 10%
            Amount stake = vctx.m_Val.m_Weight;
            Env::Halt_if(!stake);

            Events::Slash evt;
            evt.m_StakeBurned = stake / 10;
            _POD_(evt.m_Validator) = r.m_Address;

            Events::Slash::Key ek;
            Env::EmitLog_T(ek, evt);

            vctx.m_Val.m_Weight -= evt.m_StakeBurned;
            assert(vctx.m_Val.m_Weight);

            if (vctx.m_Val.m_NumSlashed < 0xff)
                vctx.m_Val.m_NumSlashed++;

            vctx.m_Val.m_hSuspend = Env::get_Height();
        }
        break;
    }

    vctx.SaveFlex();
}

BEAM_EXPORT void Method_4(const I_PBFT::Method::AddReward& r)
{
    Env::FundsLock(0, r.m_Amount);
}

} // namespace PBFT_STAT

