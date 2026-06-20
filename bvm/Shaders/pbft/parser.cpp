// Parser module for PBFT_DPOS and PBFT_STAT.
#include "../common.h"
#include "../Sort.h"
#include "../Explorer/parser_module_abi.h"
#include "pbft_dpos.h"
#include "pbft_stat.h"

static const ShaderID s_pSid[] = { PBFT_DPOS::s_SID, PBFT_STAT::s_SID };

static const char* KindFor(const ShaderID& sid)
{
	if (_POD_(sid) == PBFT_DPOS::s_SID) return "PBFT_DPOS";
	if (_POD_(sid) == PBFT_STAT::s_SID) return "PBFT_STAT";
	return "PBFT_?";
}

static void On_PBFT_Settings(const PBFT_DPOS::Settings& stg)
{
	DocAddAid("Stake-Aid", stg.m_aidStake);
	Env::DocAddNum("Unbond lock", stg.m_hUnbondLock);
	DocAddAmount("Min stake", stg.m_MinValidatorStake);
}

static void On_PBFT_ValidatorAddr(const PBFT_DPOS::Address& addr) { DocAddMonoblob("Address", addr); }
static void On_PBFT_DelegatorAddr(const PubKey& addr)             { DocAddMonoblob("Delegator", addr); }

static void On_PBFT_Status(const char* szName, I_PBFT::State::Validator::Status status)
{
	const char* szStatus = nullptr;
	switch (status)
	{
	case I_PBFT::State::Validator::Status::Active:    szStatus = "Active";    break;
	case I_PBFT::State::Validator::Status::Jailed:    szStatus = "Jailed";    break;
	case I_PBFT::State::Validator::Status::Suspended: szStatus = "Suspended"; break;
	case I_PBFT::State::Validator::Status::Tombed:    szStatus = "Tombed";    break;
	case I_PBFT::State::Validator::Status::Slash:     szStatus = "Slash";     break;
	default:
		Env::DocAddNum(szName, static_cast<uint32_t>(status));
		return;
	}
	Env::DocAddText(szName, szStatus);
}

static void On_PBFT_Commission(uint16_t commission_cpc, bool bIsTbl = false)
{
	DocAddFixedPoint(bIsTbl ? "" : "Commission", commission_cpc, 100, 2);
}

// ---------- PBFT_DPOS ----------

static void OnMethod_DPOS(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case PBFT_DPOS::Method::Create::s_iMethod:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(PBFT_DPOS::Method::Create))
		{
			auto* p = (const PBFT_DPOS::Method::Create*) pArg;
			Env::DocGroup gr("params");
			On_PBFT_Settings(p->m_Settings);
		}
		break;
	case 1: Env::DocAddText("method", "Destroy"); break;

	case PBFT_DPOS::Method::ValidatorStatusUpdate::s_iMethod:
		Env::DocAddText("method", "ValidatorStatusUpdate");
		if (nArg >= sizeof(PBFT_DPOS::Method::ValidatorStatusUpdate))
		{
			auto* p = (const PBFT_DPOS::Method::ValidatorStatusUpdate*) pArg;
			Env::DocGroup gr("params");
			On_PBFT_ValidatorAddr(p->m_Address);
			On_PBFT_Status("Status", p->m_Status);
		}
		break;

	case PBFT_DPOS::Method::AddReward::s_iMethod:
		Env::DocAddText("method", "AddReward");
		break;

	case PBFT_DPOS::Method::DelegatorUpdate::s_iMethod:
		Env::DocAddText("method", "DelegatorUpdate");
		if (nArg >= sizeof(PBFT_DPOS::Method::DelegatorUpdate))
		{
			auto* p = (const PBFT_DPOS::Method::DelegatorUpdate*) pArg;
			Env::DocGroup gr("params");
			DocAddMonoblob("Delegator", p->m_Delegator);
			if (p->m_RewardClaim || p->m_StakeBond)
			{
				DocAddMonoblob("Validator", p->m_Validator);
				DocAddAmountSigned("Bond_change", p->m_StakeBond);
			}
		}
		break;

	case PBFT_DPOS::Method::ValidatorRegister::s_iMethod:
		Env::DocAddText("method", "ValidatorRegister");
		if (nArg >= sizeof(PBFT_DPOS::Method::ValidatorRegister))
		{
			auto* p = (const PBFT_DPOS::Method::ValidatorRegister*) pArg;
			Env::DocGroup gr("params");
			On_PBFT_ValidatorAddr(p->m_Validator);
			On_PBFT_DelegatorAddr(p->m_Delegator);
			DocAddAmountSigned("Stake", p->m_Stake, true);
			On_PBFT_Commission(p->m_Commission_cpc);
		}
		break;

	case PBFT_DPOS::Method::ValidatorUpdate::s_iMethod:
		Env::DocAddText("method", "ValidatorUpdate");
		if (nArg >= sizeof(PBFT_DPOS::Method::ValidatorUpdate))
		{
			auto* p = (const PBFT_DPOS::Method::ValidatorUpdate*) pArg;
			Env::DocGroup gr("params");
			On_PBFT_ValidatorAddr(p->m_Validator);
			if (PBFT_DPOS::State::ValidatorPlus::s_CommissionTagTomb == p->m_Commission_cpc)
				Env::DocAddText("Action", "Tomb");
			else
				On_PBFT_Commission(p->m_Commission_cpc);
		}
		break;
	}
}

static void OnState_DPOS(const ContractID& cid)
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = cid;
	k.m_KeyInContract = PBFT_DPOS::State::Tag::s_Global;

	PBFT_DPOS::State::Global g;
	if (!Env::VarReader::Read_T(k, g))
		return;

	g.FlushRewardPending();

	const Amount nProbeOnePercent = 100000000;

	PBFT_DPOS::State::Global g2;
	_POD_(g2) = g;
	g2.m_RewardPending = nProbeOnePercent * 100;
	g2.FlushRewardPending();

	struct ValidatorPlus
	{
		PBFT_DPOS::State::ValidatorPlus m_Validador;
		PBFT_DPOS::Address m_Address;

		bool HasVotingPower() const {
			return m_Validador.m_Status < I_PBFT::State::Validator::Status::Suspended;
		}

		void operator = (const ValidatorPlus& x) { _POD_(*this) = x; }

		bool operator < (const ValidatorPlus& x) const {
			if (m_Validador.m_Weight > x.m_Validador.m_Weight) return true;
			if (m_Validador.m_Weight < x.m_Validador.m_Weight) return false;
			if (m_Validador.m_Status < x.m_Validador.m_Status) return true;
			if (m_Validador.m_Status > x.m_Validador.m_Status) return false;
			return _POD_(m_Address).Cmp(x.m_Address) < 0;
		}
	};

	Utils::Vector<ValidatorPlus> vVals;
	Amount totalStake = 0;
	uint64_t totalPower = 0;

	{
		Env::Key_T<PBFT_DPOS::State::Validator::Key> vk0, vk1;
		_POD_(vk0.m_Prefix.m_Cid) = cid;
		_POD_(vk1.m_Prefix.m_Cid) = cid;
		_POD_(vk0.m_KeyInContract.m_Address).SetZero();
		_POD_(vk1.m_KeyInContract.m_Address).SetObject(0xff);

		for (Env::VarReader r(vk0, vk1); ; )
		{
			auto& x = vVals.emplace_back();
			if (!r.MoveNext_T(vk0, x.m_Validador))
			{
				vVals.m_Count--;
				break;
			}
			_POD_(x.m_Address) = vk0.m_KeyInContract.m_Address;
			totalStake += x.m_Validador.m_Weight;
			if (x.HasVotingPower())
				totalPower += x.m_Validador.m_Weight;
		}
	}

	{
		Env::DocGroup gr("Settings");
		On_PBFT_Settings(g.m_Settings);
	}

	DocAddAmount("Total stake", totalStake);

	vVals.Prepare(vVals.m_Count * 2);
	auto* pVals = MergeSort<ValidatorPlus>::Do(vVals.m_p, vVals.m_p + vVals.m_Count, vVals.m_Count);

	{
		Env::DocGroup gr2("Validators/Delegators");

		DocSetType("table");
		Env::DocArray gr3("value");

		{
			Env::DocArray gr4("");
			DocAddTableHeader("Validator");
			DocAddTableHeader("Delegator");
			DocAddTableHeader("Status");
			DocAddTableHeader("Commission");
			DocAddTableHeader("Voting Power %");
			DocAddTableHeader("Stake");
			DocAddTableHeader("Reward Pending");
			DocAddTableHeader("Reward %");
		}

		Env::Key_T<PBFT_DPOS::State::Delegator::Key> dk0, dk1;
		_POD_(dk0.m_Prefix.m_Cid) = cid;
		_POD_(dk1.m_Prefix.m_Cid) = cid;

		for (uint32_t iV = 0; iV < vVals.m_Count; iV++)
		{
			auto& x = pVals[iV];
			auto& vp = x.m_Validador;

			PBFT_DPOS::State::ValidatorPlus vp2;
			_POD_(vp2) = vp;
			vp2.FlushRewardPending(g2);
			vp.FlushRewardPending(g);

			_POD_(dk0.m_KeyInContract.m_Validator) = x.m_Address;
			_POD_(dk0.m_KeyInContract.m_Delegator) = vp.m_Self.m_Delegator;

			auto weight = x.HasVotingPower() ? vp.m_Weight : 0;

			PBFT_DPOS::State::Delegator dp, dp2;
			Amount dpStake = 0;

			bool bFoundSelf = Env::VarReader::Read_T(dk0, dp);
			if (bFoundSelf)
			{
				_POD_(dp2) = dp;
				dp2.Pop(vp2, g2);
				dp2.m_RewardRemaining += vp2.m_Self.m_Commission;

				dpStake = dp.Pop(vp, g);
				dp.m_RewardRemaining += vp.m_Self.m_Commission;
			}
			else
			{
				_POD_(dp).SetZero();
				_POD_(dp2).SetZero();
			}

			{
				Env::DocArray gr4("");
				DocAddMonoblob("", x.m_Address);
				DocAddMonoblob("", vp.m_Self.m_Delegator);
				On_PBFT_Status("", vp.m_Status);
				On_PBFT_Commission(vp.m_Commission_cpc, true);
				DocAddFixedPoint("", weight * 100, totalPower, 4);
				DocAddAmount("", dpStake);
				DocAddAmount("", dp.m_RewardRemaining);
				DocAddFixedPoint("", dp2.m_RewardRemaining - dp.m_RewardRemaining, nProbeOnePercent, 4);
			}

			_POD_(dk1.m_KeyInContract.m_Validator) = x.m_Address;
			_POD_(dk0.m_KeyInContract.m_Delegator).SetZero();
			_POD_(dk1.m_KeyInContract.m_Delegator).SetObject(0xff);

			for (Env::VarReader r2(dk0, dk1); ; )
			{
				if (!r2.MoveNext_T(dk0, dp))
					break;
				if (_POD_(vp.m_Self.m_Delegator) == dk0.m_KeyInContract.m_Delegator)
					continue;

				_POD_(dp2) = dp;
				dp2.Pop(vp2, g2);
				dpStake = dp.Pop(vp, g);

				Env::DocArray gr4("");
				Env::DocAddText("", "");
				DocAddMonoblob("", dk0.m_KeyInContract.m_Delegator);
				Env::DocAddText("", "");
				Env::DocAddText("", "");
				Env::DocAddText("", "");
				DocAddAmount("", dpStake);
				DocAddAmount("", dp.m_RewardRemaining);
				DocAddFixedPoint("", dp2.m_RewardRemaining - dp.m_RewardRemaining, nProbeOnePercent, 4);
			}
		}
	}
}

// ---------- PBFT_STAT ----------

static void OnMethod_STAT(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0: Env::DocAddText("method", "Create"); break;
	case 1: Env::DocAddText("method", "Destroy"); break;

	case I_PBFT::Method::ValidatorStatusUpdate::s_iMethod:
		Env::DocAddText("method", "ValidatorStatusUpdate");
		if (nArg >= sizeof(I_PBFT::Method::ValidatorStatusUpdate))
		{
			auto* p = (const I_PBFT::Method::ValidatorStatusUpdate*) pArg;
			Env::DocGroup gr("params");
			On_PBFT_ValidatorAddr(p->m_Address);
			On_PBFT_Status("Status", p->m_Status);
		}
		break;

	case PBFT_DPOS::Method::AddReward::s_iMethod:
		Env::DocAddText("method", "AddReward");
		break;
	}
}

static void OnState_STAT(const ContractID& cid)
{
	Env::DocGroup gr2("Validators");

	DocSetType("table");
	Env::DocArray gr3("value");

	{
		Env::DocArray gr4("");
		DocAddTableHeader("Validator");
		DocAddTableHeader("Status");
		DocAddTableHeader("Weight");
	}

	Env::Key_T<I_PBFT::State::Validator::Key> vk0, vk1;
	_POD_(vk0.m_Prefix.m_Cid) = cid;
	_POD_(vk1.m_Prefix.m_Cid) = cid;
	_POD_(vk0.m_KeyInContract.m_Address).SetZero();
	_POD_(vk1.m_KeyInContract.m_Address).SetObject(0xff);

	for (Env::VarReader r(vk0, vk1); ; )
	{
		I_PBFT::State::Validator vp;
		if (!r.MoveNext_T(vk0, vp))
			break;

		Env::DocArray gr4("");
		DocAddMonoblob("", vk0.m_KeyInContract.m_Address);
		On_PBFT_Status("", vp.m_Status);
		DocAddAmount("", vp.m_Weight);
	}
}

// ---------- ABI ----------

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, s_pSid, _countof(s_pSid));
}

BEAM_EXPORT void Method_0(const ShaderID& sid, const ContractID&, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr("");
	Env::DocAddText("kind", KindFor(sid));
	if (_POD_(sid) == PBFT_DPOS::s_SID)
		OnMethod_DPOS(iMethod, pArg, nArg);
	else
		OnMethod_STAT(iMethod, pArg, nArg);
}

BEAM_EXPORT void Method_1(const ShaderID& sid, const ContractID&)
{
	Env::DocGroup gr("");
	Env::DocAddText("kind", KindFor(sid));
}

BEAM_EXPORT void Method_2(const ShaderID& sid, const ContractID& cid)
{
	Env::DocGroup gr("");
	Env::DocAddText("kind", KindFor(sid));
	{
		Env::DocGroup grSt("State");
		if (_POD_(sid) == PBFT_DPOS::s_SID)
			OnState_DPOS(cid);
		else
			OnState_STAT(cid);
	}
}
