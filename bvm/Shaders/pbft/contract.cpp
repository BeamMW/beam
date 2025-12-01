////////////////////////
#include "../common.h"
#include "contract.h"

namespace PBFT {

void State::ValidatorPlus::Init(const State::Global& g)
{
    _POD_(*this).SetZero();
    m_cuRewardGlobal.Update(g.m_accReward, 0); // just move 
    m_kStakeScale.set_1();
}

void EraseUnbonded(Amount amount, const PubKey& pkDelegator, Height hMax)
{
    assert(amount);

    State::Unbonded::Key uk;
    _POD_(uk.m_Delegator) = pkDelegator;
    uk.m_hLock_BE = Utils::FromBE(hMax); // assume from/to BE is the same

    while (true)
    {
        State::Unbonded u;
        uint32_t nVal = sizeof(u);
        uint32_t nKey = sizeof(uk);

        Env::LoadVarEx(&uk, nKey, nKey, &u, nVal, KeyTag::Internal, KeySearchFlags::Exact);
        Env::Halt_if((sizeof(uk) != nKey) || (sizeof(u) != nVal) || (State::Tag::s_Unbonded != uk.m_Tag));

        if (amount >= u.m_Amount)
        {
            amount -= u.m_Amount;
            Env::DelVar_T(uk);
        }
        else
        {
            u.m_Amount -= amount;
            Env::SaveVar_T(uk, u);
            amount = 0;
        }

        if (!amount)
            break;
    }
}

void AddUnbonded(Amount amount, const PubKey& pkDelegator, Height h)
{
    assert(amount);

    State::Unbonded::Key uk;
    _POD_(uk.m_Delegator) = pkDelegator;
    uk.m_hLock_BE = Utils::FromBE(h);

    State::Unbonded u;
    if (Env::LoadVar_T(uk, u))
        Strict::Add(u.m_Amount, amount);
    else
        u.m_Amount = amount;

    Env::SaveVar_T(uk, u);
}


BEAM_EXPORT void Ctor(const Method::Create& r)
{
    State::Global g;
    _POD_(g).SetZero();
    _POD_(g.m_Settings) = r.m_Settings;

    State::Validator::Key vk;
    State::ValidatorPlus vp;
    vp.Init(g);

    State::Delegator::Key dk;
    State::Delegator d;
    _POD_(d).SetZero();

    for (uint32_t i = 0; i < r.m_Validators; i++)
    {
        const auto& v = ((const State::Validator::Init*)(&r + 1))[i];
        _POD_(vk.m_Address) = v.m_Address;

        Env::Halt_if(!v.m_Stake);
        vp.m_Weight = v.m_Stake;
        _POD_(vp.m_Self.m_Delegator) = v.m_Delegator;

        Env::Halt_if(Env::SaveVar_T(vk, vp)); // fail if key is duplicated

        d.m_Bonded.m_kStakeScaled = v.m_Stake;
        _POD_(dk.m_Validator) = v.m_Address;
        _POD_(dk.m_Delegator) = v.m_Delegator;
        Env::SaveVar_T(dk, d);

        vp.StakeChangeExternal<true>(g);
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

    State::ValidatorPlus vp;
    Env::Halt_if(!Env::LoadVar_T(vk, vp));

    typedef State::Validator::Status Status;

    Env::Halt_if(r.m_Status == vp.m_Status);

    State::Global g;
    auto gk = State::Tag::s_Global;
    Env::LoadVar_T(gk, g);

    g.FlushRewardPending();
    vp.FlushRewardPending(g);

    vp.StakeChangeExternal<false>(g);

    switch (r.m_Status)
    {
    default:
        Env::Halt();
        // no break;

    case Status::Active:
    case Status::Jailed:
    case Status::Suspended:
        vp.m_Status = r.m_Status;
        break;

    case Status::Slash:
        {
            vp.m_Status = Status::Suspended; // Slash is a transition, not a state.

            // slash by 10%
            Amount stake = vp.m_Weight;
            Env::Halt_if(!stake);

            Events::Slash evt;
            evt.m_StakeBurned = stake / 10;
            _POD_(evt.m_Validator) = r.m_Address;

            Events::Slash::Key ek;
            Env::EmitLog_T(ek, evt);

            vp.m_Weight -= evt.m_StakeBurned;
            assert(vp.m_Weight);

            // don't assume it's exactly 10%, it can be slightly different due to roundoffs
            vp.m_kStakeScale *= State::Float(stake) / State::Float(vp.m_Weight);

            if (vp.m_NumSlashed < 0xff)
                vp.m_NumSlashed++;

            vp.m_hSuspend = Env::get_Height();
        }
        break;
    }

    vp.StakeChangeExternal<true>(g);

    Env::SaveVar_T(vk, vp);
    Env::SaveVar_T(gk, g);
}

BEAM_EXPORT void Method_3(const Method::AddReward& r)
{
    State::Global g;
    auto gk = State::Tag::s_Global;
    Env::LoadVar_T(gk, g);

    Strict::Add(g.m_RewardPending, r.m_Amount);
    Env::SaveVar_T(gk, g);

    Env::FundsLock(0, r.m_Amount);
}

BEAM_EXPORT void Method_4(const Method::DelegatorUpdate& r)
{
    State::Global g;
    auto gk = State::Tag::s_Global;
    Env::LoadVar_T(gk, g);

    auto depositUnbonded = r.m_StakeDeposit;

    if (r.m_RewardClaim || r.m_StakeBond)
    {
        // Load everything, update statuses
        g.FlushRewardPending();

        State::Validator::Key vk;
        _POD_(vk.m_Address) = r.m_Validator;

        State::ValidatorPlus vp;
        bool bValidatorExisted = Env::LoadVar_T(vk, vp);
        bool bSelf = !bValidatorExisted;
        if (bValidatorExisted)
        {
            vp.FlushRewardPending(g);
            bSelf = _POD_(vp.m_Self.m_Delegator) == r.m_Delegator;
        }
        else
        {
            vp.Init(g);
            _POD_(vp.m_Self.m_Delegator) = r.m_Delegator;
        }

        State::Delegator::Key dk;
        _POD_(dk.m_Validator) = r.m_Validator;
        _POD_(dk.m_Delegator) = r.m_Delegator;

        State::Delegator d;
        bool bDelegatorExisted = Env::LoadVar_T(dk, d);
        if (!bDelegatorExisted)
            _POD_(d).SetZero();

        Amount stakeBonded = d.Pop(vp, g);
        if (bSelf)
        {
            Strict::Add(d.m_RewardRemaining, vp.m_Self.m_Commission);
            vp.m_Self.m_Commission = 0;
        }

        // Reward
        if (r.m_RewardClaim)
        {
            Env::FundsUnlock(0, r.m_RewardClaim);
            Strict::Sub(d.m_RewardRemaining, r.m_RewardClaim);
        }

        // Update bond
        if (r.m_StakeBond > 0)
        {
            // take as much as possible from current deposit, the rest is from unbonded
            if (depositUnbonded >= r.m_StakeBond)
                depositUnbonded -= r.m_StakeBond;
            else
            {
                Amount fromUnbonded = r.m_StakeBond;
                if (depositUnbonded > 0)
                {
                    assert(r.m_StakeBond > depositUnbonded);
                    fromUnbonded -= depositUnbonded;
                    depositUnbonded = 0;
                }

                EraseUnbonded(fromUnbonded, r.m_Delegator, static_cast<Height>(-1));
            }

            Strict::Add(stakeBonded, (Amount) r.m_StakeBond);
        }
        else
        {
            if (r.m_StakeBond < 0)
            {
                Amount val = -r.m_StakeBond;
                Strict::Sub(stakeBonded, val);
                AddUnbonded(val, r.m_Delegator, Env::get_Height() + g.m_Settings.m_hUnbondLock);
            }
        }

        bool bEmptyDelegator = false;

        // re-insert to pool
        if (stakeBonded)
        {
            vp.StakeChange<true>(g, stakeBonded);
            d.m_Bonded.m_kStakeScaled = vp.m_kStakeScale * State::Float(stakeBonded);
        }
        else
        {
            d.m_Bonded.m_kStakeScaled.Set0();

            if (!d.m_RewardRemaining)
                bEmptyDelegator = true;
        }

        if (bEmptyDelegator)
            Env::DelVar_T(dk);
        else
            Env::SaveVar_T(dk, d);

        if (vp.m_Weight)
            Env::SaveVar_T(vk, vp);
        else
            Env::DelVar_T(vk);

        Env::SaveVar_T(gk, g);
    }

    if (depositUnbonded > 0)
        AddUnbonded(depositUnbonded, r.m_Delegator, 0);
    else
        if (depositUnbonded < 0)
            EraseUnbonded(-depositUnbonded, r.m_Delegator, Env::get_Height());

    // finalyze tx balance and sig
    if (r.m_StakeDeposit > 0)
        Env::FundsLock(g.m_Settings.m_aidStake, r.m_StakeDeposit);
    else
        if (r.m_StakeDeposit < 0)
            Env::FundsUnlock(g.m_Settings.m_aidStake, -r.m_StakeDeposit);

    Env::AddSig(r.m_Delegator);

}

} // namespace PBFT
