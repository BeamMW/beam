////////////////////////
#include "pbft_dpos.h"

namespace PBFT_DPOS {

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

struct GlobalCtx_NoLoad
    :public State::Global
{
    void Save()
    {
        auto gk = State::Tag::s_Global;
        Env::SaveVar_T(gk, *this);
    }
};

struct GlobalCtx
    :public GlobalCtx_NoLoad
{
    GlobalCtx()
    {
        auto gk = State::Tag::s_Global;
        Env::LoadVar_T(gk, *this);
    }
};

struct ValidatorCtx_NoLoad
{
    State::Validator::Key m_Key;
    State::ValidatorPlus m_Val;

    ValidatorCtx_NoLoad(const Address& addr)
    {
        _POD_(m_Key.m_Address) = addr;
    }

    bool SaveRaw()
    {
        return Env::SaveVar_T(m_Key, m_Val);
    }

    void OnLoaded(State::Global& g)
    {
        g.FlushRewardPending();
        m_Val.FlushRewardPending(g);
    }

    void SaveFlex()
    {
        bool bKill =
            (State::Validator::Status::Tombed == m_Val.m_Status) &&
            !m_Val.m_Weight &&
            !m_Val.m_Self.m_Commission;

        if (bKill)
            Env::DelVar_T(m_Key);
        else
            SaveRaw();
    }
};

struct ValidatorCtx
    :public ValidatorCtx_NoLoad
{
    ValidatorCtx(State::Global& g, const Address& addr)
        :ValidatorCtx_NoLoad(addr)
    {
        Env::Halt_if(!Env::LoadVar_T(m_Key, m_Val));
        OnLoaded(g);
    }
};

struct DelegatorCtx
{
    State::Delegator::Key m_Key;
    State::Delegator m_Val;

    bool m_Self;
    Amount m_StakeBonded;

    DelegatorCtx(State::Global& g, ValidatorCtx_NoLoad& vctx, const PubKey& pkDelegator)
    {
        _POD_(m_Key.m_Validator) = vctx.m_Key.m_Address;
        _POD_(m_Key.m_Delegator) = pkDelegator;

        if (!Env::LoadVar_T(m_Key, m_Val))
            _POD_(m_Val).SetZero();

        m_StakeBonded = m_Val.Pop(vctx.m_Val, g);

        m_Self = (_POD_(vctx.m_Val.m_Self.m_Delegator) == pkDelegator);
        if (m_Self)
        {
            Strict::Add(m_Val.m_RewardRemaining, vctx.m_Val.m_Self.m_Commission);
            vctx.m_Val.m_Self.m_Commission = 0;
        }
    }

    void Finalize(State::Global& g, ValidatorCtx_NoLoad& vctx)
    {
        // re-insert to pool
        if (m_StakeBonded)
        {
            vctx.m_Val.StakeChange<true>(g, m_StakeBonded);
            m_Val.m_Bonded.m_kStakeScaled = vctx.m_Val.m_kStakeScale * State::Float(m_StakeBonded);
        }
        else
        {
            if (!m_Val.m_RewardRemaining)
            {
                Env::DelVar_T(m_Key);
                return;
            }

            m_Val.m_Bonded.m_kStakeScaled.Set0();
        }

        Env::SaveVar_T(m_Key, m_Val);
    }
};

BEAM_EXPORT void Ctor(const Method::Create& r)
{
    GlobalCtx_NoLoad g;
    _POD_(g).SetZero();
    _POD_(g.m_Settings) = r.m_Settings;
    g.Save();
}

BEAM_EXPORT void Dtor(void*)
{
    // ignore
}

BEAM_EXPORT void Method_2(void*)
{
    Env::Halt(); // TODO: upgrade
}

BEAM_EXPORT void Method_3(const Method::ValidatorStatusUpdate& r)
{
    GlobalCtx g;
    ValidatorCtx vctx(g, r.m_Address);
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
        vctx.m_Val.ChangeStatus(g, r.m_Status);
        break;

    case Status::Slash:
        {
            if (Status::Tombed != vctx.m_Val.m_Status)
                vctx.m_Val.ChangeStatus(g, Status::Suspended); // Slash is a transition, not a state.

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

            // don't assume it's exactly 10%, it can be slightly different due to roundoffs
            vctx.m_Val.m_kStakeScale *= State::Float(stake) / State::Float(vctx.m_Val.m_Weight);

            if (vctx.m_Val.m_NumSlashed < 0xff)
                vctx.m_Val.m_NumSlashed++;

            vctx.m_Val.m_hSuspend = Env::get_Height();
        }
        break;
    }

    vctx.SaveFlex();
    g.Save();
}

BEAM_EXPORT void Method_4(const Method::AddReward& r)
{
    GlobalCtx g;
    Strict::Add(g.m_RewardPending, r.m_Amount);
    g.Save();

    Env::FundsLock(0, r.m_Amount);
}


BEAM_EXPORT void Method_5(const Method::DelegatorUpdate& r)
{
    GlobalCtx g;

    auto depositUnbonded = r.m_StakeDeposit;

    if (r.m_RewardClaim || r.m_StakeBond)
    {
        ValidatorCtx vctx(g, r.m_Validator);
        DelegatorCtx dctx(g, vctx, r.m_Delegator);

        // Reward
        if (r.m_RewardClaim)
        {
            Env::FundsUnlock(0, r.m_RewardClaim);
            Strict::Sub(dctx.m_Val.m_RewardRemaining, r.m_RewardClaim);
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

            Strict::Add(dctx.m_StakeBonded, (Amount) r.m_StakeBond);
        }
        else
        {
            if (r.m_StakeBond < 0)
            {
                Amount val = -r.m_StakeBond;
                Strict::Sub(dctx.m_StakeBonded, val);

                if (dctx.m_Self && (State::ValidatorPlus::Status::Tombed != vctx.m_Val.m_Status))
                    // don't allow to withdraw funds below minimum threshold
                    Env::Halt_if(dctx.m_StakeBonded < g.m_Settings.m_MinValidatorStake);

                AddUnbonded(val, r.m_Delegator, Env::get_Height() + g.m_Settings.m_hUnbondLock);
            }
        }

        dctx.Finalize(g, vctx);
        vctx.SaveFlex();
        g.Save();
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

BEAM_EXPORT void Method_6(const Method::ValidatorRegister& r)
{
    Env::Halt_if(r.m_Commission_cpc > State::ValidatorPlus::s_CommissionMax);

    GlobalCtx g;
    ValidatorCtx_NoLoad vctx(r.m_Validator);
    _POD_(vctx.m_Val).SetZero();
    vctx.m_Val.m_kStakeScale.set_1();
    vctx.m_Val.m_Commission_cpc = r.m_Commission_cpc;
    vctx.m_Val.m_Self.m_Delegator = r.m_Delegator;
    static_assert(0 == (uint8_t) State::Validator::Status::Active, "");

    vctx.OnLoaded(g);

    DelegatorCtx dctx(g, vctx, r.m_Delegator);

    Strict::Add(dctx.m_StakeBonded, r.m_Stake);
    Env::FundsLock(g.m_Settings.m_aidStake, r.m_Stake);

    Env::Halt_if(dctx.m_StakeBonded < g.m_Settings.m_MinValidatorStake);

    dctx.Finalize(g, vctx);
    Env::Halt_if(vctx.SaveRaw()); // fail if duplicated
    g.Save();
}

BEAM_EXPORT void Method_7(const Method::ValidatorUpdate& r)
{
    GlobalCtx g;
    ValidatorCtx vctx(g, r.m_Validator);

    Env::Halt_if(State::Validator::Status::Tombed == vctx.m_Val.m_Status);

    if (State::ValidatorPlus::s_CommissionTagTomb == r.m_Commission_cpc)
        vctx.m_Val.ChangeStatus(g, State::Validator::Status::Tombed);
    else
    {
        // currently don't allow to raise commission
        Env::Halt_if(vctx.m_Val.m_Commission_cpc <= r.m_Commission_cpc);
        vctx.m_Val.m_Commission_cpc = r.m_Commission_cpc;
    }

    vctx.SaveFlex();
    g.Save();

    Env::AddSig(vctx.m_Val.m_Self.m_Delegator);
}


} // namespace PBFT_DPOS

