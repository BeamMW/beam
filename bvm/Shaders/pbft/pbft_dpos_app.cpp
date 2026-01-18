#include "../common.h"
#include "../app_common_impl.h"
#include "pbft_dpos.h"

#define PbftDpos_view_deployed(macro)
#define PbftDpos_view_params(macro) macro(ContractID, cid)
#define PbftDpos_view_validators(macro) macro(ContractID, cid)
#define PbftDpos_view_key(macro) macro(ContractID, cid)

#define PbftDpos_view_delegator(macro) \
    macro(ContractID, cid) \
    macro(PubKey, pkDelegator) \

#define PbftDpos_update_delegator(macro) \
    macro(ContractID, cid) \
    macro(Address, validator) \
    macro(Amount, newBond) \

#define PbftDposActions_All(macro) \
    macro(view_deployed) \
    macro(view_params) \
    macro(view_validators) \
    macro(view_key) \
    macro(view_delegator) \
    macro(update_delegator) \

namespace PBFT_DPOS {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("actions");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_ACTION(name) { Env::DocGroup gr(#name);  PbftDpos_##name(THE_FIELD) }
        
        PbftDposActions_All(THE_ACTION)
#undef THE_ACTION
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(name) void On_##name(PbftDpos_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

ON_METHOD(view_deployed)
{
    EnumAndDumpContracts(s_SID);
}

bool LoadGlobal(State::Global& g, const ContractID& cid)
{
    Env::Key_T<uint8_t> k;
    _POD_(k.m_Prefix.m_Cid) = cid;
    k.m_KeyInContract = State::Tag::s_Global;

    if (Env::VarReader::Read_T(k, g))
        return true;

    OnError("no such contract");
    return false;
}

ON_METHOD(view_params)
{
    State::Global g;
    if (LoadGlobal(g, cid))
    {
        Env::DocGroup gr("res");
        Env::DocAddNum("StakeNonJailed", g.m_TotakStakeNonJailed);
    }
}

bool TakeSelfReward(State::ValidatorPlus& vp, State::Delegator& dp, const PubKey& pkDelegator)
{
    if (_POD_(vp.m_Self.m_Delegator) != pkDelegator)
        return false;

    dp.m_RewardRemaining += vp.m_Self.m_Commission;
    vp.m_Self.m_Commission = 0;

    return true;
}

ON_METHOD(view_validators)
{
    State::Global g;
    if (!LoadGlobal(g, cid))
        return;

    g.FlushRewardPending();

    Env::DocGroup gr("res");
    Env::DocArray gr1("validators");

	Env::Key_T<PBFT_DPOS::State::Validator::Key> vk0, vk1;
	_POD_(vk0.m_Prefix.m_Cid) = cid;
	_POD_(vk1.m_Prefix.m_Cid) = cid;
	_POD_(vk0.m_KeyInContract.m_Address).SetZero();
	_POD_(vk1.m_KeyInContract.m_Address).SetObject(0xff);

	Env::Key_T<PBFT_DPOS::State::Delegator::Key> dk0, dk1;
	_POD_(dk0.m_Prefix.m_Cid) = cid;
	_POD_(dk1.m_Prefix.m_Cid) = cid;

	for (Env::VarReader r(vk0, vk1); ; )
	{
		PBFT_DPOS::State::ValidatorPlus vp;
		if (!r.MoveNext_T(vk0, vp))
			break;

		vp.FlushRewardPending(g);

		Env::DocGroup gr2("");

		Env::DocAddBlob_T("key", vk0.m_KeyInContract.m_Address);
		Env::DocAddNum("status", (uint32_t) vp.m_Status);
		Env::DocAddNum32("commission_cpc", vp.m_Commission_cpc);
		Env::DocAddNum("stake", vp.m_Weight);

        Env::DocArray gr3("delegators");

		_POD_(dk0.m_KeyInContract.m_Validator) = vk0.m_KeyInContract.m_Address;
		_POD_(dk1.m_KeyInContract.m_Validator) = vk0.m_KeyInContract.m_Address;

		_POD_(dk0.m_KeyInContract.m_Delegator).SetZero();
		_POD_(dk1.m_KeyInContract.m_Delegator).SetObject(0xff);

		for (Env::VarReader r2(dk0, dk1); ; )
		{
			PBFT_DPOS::State::Delegator dp;
			if (!r2.MoveNext_T(dk0, dp))
				break;

            Env::DocGroup gr4("");
            Env::DocAddBlob_T("key", dk0.m_KeyInContract.m_Delegator);

			Amount stake = dp.Pop(vp, g);
            Env::DocAddNum("stake", stake);

            if (TakeSelfReward(vp, dp, dk0.m_KeyInContract.m_Delegator))
                Env::DocAddNum32("self", 1);


            Env::DocAddNum("reward_pending", dp.m_RewardRemaining);
		}

	}

}

ON_METHOD(view_key)
{
    PubKey pk;
    Env::KeyID(cid).get_Pk(pk);
    Env::DocAddBlob_T("res", pk);
}

struct DelegatorBondInfo
{
    Env::Key_T<PBFT_DPOS::State::Delegator::Key> m_dk;
    PBFT_DPOS::State::Delegator m_dp;

    Env::VarReaderEx<true> m_R;
    Amount m_Stake;

    void Init(const ContractID& cid, const PubKey& pkDelegator)
    {
        _POD_(m_dk.m_Prefix.m_Cid) = cid;
        _POD_(m_dk.m_KeyInContract.m_Delegator) = pkDelegator;

        Env::Key_T<PBFT_DPOS::State::Validator::Key> vk0, vk1;
        _POD_(vk0.m_Prefix.m_Cid) = cid;
        _POD_(vk0.m_KeyInContract.m_Address).SetZero();
        _POD_(vk1.m_Prefix.m_Cid) = cid;
        _POD_(vk1.m_KeyInContract.m_Address).SetObject(0xff);

        m_R.Enum_T(vk0, vk1);
    }

    bool MoveNext(State::Global& g)
    {
        while (true)
        {
            Env::Key_T<PBFT_DPOS::State::Validator::Key> vk;
            PBFT_DPOS::State::ValidatorPlus vp;
            if (!m_R.MoveNext_T(vk, vp))
                break;

            _POD_(m_dk.m_KeyInContract.m_Validator) = vk.m_KeyInContract.m_Address;

            if (!Env::VarReader::Read_T(m_dk, m_dp))
                continue;

            vp.FlushRewardPending(g);

            m_Stake = m_dp.Pop(vp, g);

            TakeSelfReward(vp, m_dp, m_dk.m_KeyInContract.m_Delegator);

            return true;
        }

        return false;
    }
};

struct DelegatorUnBondedInfo
{
    Env::Key_T<PBFT_DPOS::State::Unbonded::Key> m_uk;
    PBFT_DPOS::State::Unbonded m_uv;
    Env::VarReaderEx<true> m_R;

    bool m_IsAvail;

    void Init(const ContractID& cid, const PubKey& pkDelegator)
    {
        Env::Key_T<PBFT_DPOS::State::Unbonded::Key> uk0, uk1;
        _POD_(uk0.m_Prefix.m_Cid) = cid;
        _POD_(uk0.m_KeyInContract.m_Delegator) = pkDelegator;
        uk0.m_KeyInContract.m_hLock_BE = 0;
        _POD_(uk1.m_Prefix.m_Cid) = cid;
        _POD_(uk1.m_KeyInContract.m_Delegator) = pkDelegator;
        uk1.m_KeyInContract.m_hLock_BE = (Height) -1;

        m_R.Enum_T(uk0, uk1);
    }

    bool MoveNext()
    {
        if (!m_R.MoveNext_T(m_uk, m_uv))
            return false;

        Height h = Utils::FromBE(m_uk.m_KeyInContract.m_hLock_BE);
        m_IsAvail = (h <= Env::get_Height());

        return true;
    }
};

void OnViewDelegator(const ContractID& cid, const PubKey& pkDelegator)
{
    State::Global g;
    if (!LoadGlobal(g, cid))
        return;
    g.FlushRewardPending();

    Amount reward = 0;

    Env::DocGroup gr("res");

    {
        Env::DocArray gr1("bondings");

        DelegatorBondInfo dbi;
        for (dbi.Init(cid, pkDelegator); dbi.MoveNext(g); )
        {
            Env::DocGroup gr2("");
            Env::DocAddBlob_T("validator", dbi.m_dk.m_KeyInContract.m_Validator);
            Env::DocAddNum("stake", dbi.m_Stake);
            reward += dbi.m_dp.m_RewardRemaining;
        }
    }

    Env::DocAddNum("reward", reward);

    Amount unbondedTotal = 0;
    Amount unbondedAvail = 0;

    {
        DelegatorUnBondedInfo dui;
        for (dui.Init(cid, pkDelegator); dui.MoveNext(); )
        {
            unbondedTotal += dui.m_uv.m_Amount;
            if (dui.m_IsAvail)
                unbondedAvail += dui.m_uv.m_Amount;
        }
    }

    Env::DocAddNum("unbonded_total", unbondedTotal);
    Env::DocAddNum("unbonded_avail", unbondedAvail);

}

ON_METHOD(view_delegator)
{
    if (_POD_(pkDelegator).IsZero())
    {
        PubKey pk;
        Env::KeyID(cid).get_Pk(pk);
        OnViewDelegator(cid, pk);
    }
    else
        OnViewDelegator(cid, pkDelegator);
}

ON_METHOD(update_delegator)
{
    State::Global g;
    if (!LoadGlobal(g, cid))
        return;

    Env::KeyID kid(cid);

    // This is our logic.
    // 1. Discover how much unbonded stake do we have (both avail and locked)
    // 2. If validator isn't specified - just withdraw all avail stake
    // 3. If validator is specified:
    //      4. Withdraw current reward
    //      5. If adding stake - take as much as possible from unbonded (both avail and locked can be used). The rest should be sent in the tx
    // 6. Withdraw what remains from the avail stake

    FundsChange pFc[2];
    _POD_(pFc).SetZero();
    pFc[1].m_Aid = g.m_Settings.m_aidStake;

    uint32_t nCost =
        Env::Cost::Cycle * 100 +
        Env::Cost::AddSig;

    Method::DelegatorUpdate args;
    _POD_(args.m_Validator) = validator;
    kid.get_Pk(args.m_Delegator);

    bool bHaveValidator = !_POD_(validator).IsZero();
    if (bHaveValidator)
    {
        Env::Key_T<State::Validator::Key> vk;
        _POD_(vk.m_Prefix.m_Cid) = cid;
        _POD_(vk.m_KeyInContract.m_Address) = validator;

        State::ValidatorPlus vp;
        if (!Env::VarReader::Read_T(vk, vp))
            return OnError("no such validator");

        g.FlushRewardPending();
        vp.FlushRewardPending(g);

        Env::Key_T<State::Delegator::Key> dk;
        _POD_(dk.m_Prefix.m_Cid) = cid;
        _POD_(dk.m_KeyInContract.m_Validator) = validator;
        _POD_(dk.m_KeyInContract.m_Delegator) = args.m_Delegator;

        Amount stake = 0;

        State::Delegator dp;
        if (Env::VarReader::Read_T(dk, dp))
        {
            stake = dp.Pop(vp, g);
            TakeSelfReward(vp, dp, args.m_Delegator);
            args.m_RewardClaim = dp.m_RewardRemaining;

            if (args.m_RewardClaim)
            {
                pFc[0].m_Amount = args.m_RewardClaim;

                nCost +=
                    Env::Cost::Cycle * 50 +
                    Env::Cost::FundsLock;
            }
        }

        args.m_StakeBond = newBond - stake;

        nCost +=
            Env::Cost::LoadVar_For(sizeof(g)) +
            Env::Cost::SaveVar_For(sizeof(g)) +
            Env::Cost::LoadVar_For(sizeof(vp)) +
            Env::Cost::SaveVar_For(sizeof(vp)) +
            Env::Cost::LoadVar_For(sizeof(dp)) +
            Env::Cost::SaveVar_For(sizeof(dp)) +
            Env::Cost::Cycle * 10000;
    }

    // fix tx balance and args.m_StakeDeposit, use unbonded stake if available
    uint32_t nCountAvail = 0;
    Utils::Vector<Amount> vUnbonded;

    DelegatorUnBondedInfo dui;
    for (dui.Init(cid, args.m_Delegator); dui.MoveNext(); )
    {
        vUnbonded.emplace_back() = dui.m_uv.m_Amount;
        if (dui.m_IsAvail)
            nCountAvail++;
    }

    args.m_StakeDeposit = args.m_StakeBond;

    for (uint32_t iEntry = vUnbonded.m_Count; iEntry--; )
    {
        auto val = vUnbonded.m_p[iEntry];

        if (args.m_StakeDeposit > 0)
        {
            // take from this entry
            nCost +=
                Env::Cost::LoadVar_For(sizeof(Amount)) +
                Env::Cost::SaveVar_For(sizeof(Amount)) +
                Env::Cost::Cycle * 100;

            if (val >= (Amount) args.m_StakeDeposit)
            {
                val -= args.m_StakeDeposit;
                args.m_StakeDeposit = 0;
            }
            else
            {
                args.m_StakeDeposit -= val;
                val = 0;
            }
        }

        if (val && (iEntry < nCountAvail))
        {
            args.m_StakeDeposit -= val;

            nCost +=
                Env::Cost::LoadVar_For(sizeof(Amount)) +
                Env::Cost::SaveVar_For(sizeof(Amount)) +
                Env::Cost::Cycle * 100;
        }
    }

    if (args.m_StakeDeposit > 0)
    {
        pFc[1].m_Amount = args.m_StakeDeposit;
        pFc[1].m_Consume = 1;
    }
    else
        pFc[1].m_Amount = (Amount) -args.m_StakeDeposit;

    if (pFc[0].m_Amount)
        nCost += Env::Cost::FundsLock;
    if (pFc[1].m_Amount)
        nCost += Env::Cost::FundsLock;

    if (args.m_RewardClaim || args.m_StakeBond || args.m_StakeDeposit)
        Env::GenerateKernel(&cid, args.s_iMethod, &args, sizeof(args), pFc, _countof(pFc), &kid, 1, "Delegator udpate", nCost);
    else
        OnError("no effect");
}

#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
{
    Env::DocGroup root("");

    char szAction[0x20];

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(name) \
        static_assert(sizeof(szAction) >= sizeof(#name)); \
        if (!Env::Strcmp(szAction, #name)) { \
            PbftDpos_##name(PAR_READ) \
            On_##name(PbftDpos_##name(PAR_PASS) 0); \
            return; \
        }

    PbftDposActions_All(THE_METHOD)

#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown action");
}

} // namespace PBFT_DPOS
