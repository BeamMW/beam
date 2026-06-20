// Parser module for DaoAccumulator (versioned, behind Upgradable3).
#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static void OnKind() { Env::DocAddText("kind", "DaoAccumulator"); }

static void OnPoolType(uint8_t nType)
{
	static const char s_szName[] = "Pool";
	switch (nType)
	{
	case DaoAccumulator::Method::UserLock::Type::BeamX_PrePhase: Env::DocAddText(s_szName, "Beam-BeamX pre-phase"); break;
	case DaoAccumulator::Method::UserLock::Type::BeamX:          Env::DocAddText(s_szName, "Beam-BeamX"); break;
	case DaoAccumulator::Method::UserLock::Type::Nph:            Env::DocAddText(s_szName, "Beam-Nph"); break;
	}
}

static void OnUserWithdraw(uint8_t nType, const void* pArg, uint32_t nArg)
{
	if (nArg < sizeof(DaoAccumulator::Method::UserWithdraw_Base))
		return;
	auto* p = (const DaoAccumulator::Method::UserWithdraw_Base*) pArg;
	Env::DocAddText("method", "Withdraw");
	Env::DocGroup gr("params");
	OnPoolType(nType);
	DocAddPk("pk", p->m_pkUser);
}

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 1: Env::DocAddText("method", "Destroy"); break;

	case DaoAccumulator::Method::Create::s_iMethod:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(DaoAccumulator::Method::Create))
		{
			auto* p = (const DaoAccumulator::Method::Create*) pArg;
			Env::DocGroup gr("params");
			WriteUpgradeSettings(p->m_Upgradable);
			DocAddAid("beamX", p->m_aidBeamX);
			DocAddHeight("Per-phase end", p->m_hPrePhaseEnd);
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		// Control method handling intentionally minimal — host emits wrapper info.
		Env::DocAddText("method", "Upgradable3 Control");
		break;

	case DaoAccumulator::Method::FarmStart::s_iMethod:
		if (nArg >= sizeof(DaoAccumulator::Method::FarmStart))
		{
			auto* p = (const DaoAccumulator::Method::FarmStart*) pArg;
			Env::DocAddText("method", "Farm Start");
			Env::DocGroup gr("params");
			WriteUpgradeAdminsMask(p->m_ApproveMask);
			DocAddAid("LP-Token", p->m_aidLpToken);
			DocAddAmount("Total Reward", p->m_FarmBeamX);
			Env::DocAddNum("Total Duration", p->m_hFarmDuration);
		}
		break;

	case DaoAccumulator::Method::UserLock::s_iMethod:
		if (nArg >= sizeof(DaoAccumulator::Method::UserLock))
		{
			auto* p = (const DaoAccumulator::Method::UserLock*) pArg;
			Env::DocAddText("method", "Lock");
			Env::DocGroup gr("params");
			OnPoolType(p->m_PoolType);
			DocAddPk("pk", p->m_pkUser);
			DocAddHeight("hEnd", p->m_hEnd);
		}
		break;

	case DaoAccumulator::Method::UserWithdraw_FromBeamNph::s_iMethod:
		OnUserWithdraw(DaoAccumulator::Method::UserLock::Type::Nph, pArg, nArg);
		break;

	case DaoAccumulator::Method::UserWithdraw_FromBeamBeamX::s_iMethod:
		OnUserWithdraw(DaoAccumulator::Method::UserLock::Type::BeamX, pArg, nArg);
		break;
	}
}

static void OnState_Pool(DaoAccumulator::Pool& p, const char* szName)
{
	Env::DocGroup gr(szName);
	p.Update(Env::get_Height());
	DocAddAmount("Reward remaining", p.m_AmountRemaining);
	Env::DocAddNum("Farming duration remaining", p.m_hRemaining);
}

static void OnState_Users(const ContractID& cid, DaoAccumulator::Pool& p, uint8_t type, const char* szName)
{
	Env::DocGroup gr2(szName);
	DocSetType("table");
	Env::DocArray gr3("value");

	{
		Env::DocArray gr4("");
		DocAddTableHeader("LP-Tokens");
		DocAddTableHeader("Locked until");
		DocAddTableHeader("Reward");
		DocAddTableHeader("Key");
	}

	Env::Key_T<DaoAccumulator::User::KeyBase> k0, k1;
	k0.m_KeyInContract.m_Tag = type;
	k1.m_KeyInContract.m_Tag = type;
	_POD_(k0.m_Prefix.m_Cid) = cid;
	_POD_(k1.m_Prefix.m_Cid) = cid;
	_POD_(k0.m_KeyInContract.m_pk).SetZero();
	_POD_(k1.m_KeyInContract.m_pk).SetObject(0xff);

	for (Env::VarReader r(k0, k1); ; )
	{
		DaoAccumulator::User u;
		if (!r.MoveNext_T(k0, u))
			break;

		Env::DocArray gr4("");
		DocAddAmount("", u.m_LpToken);
		DocAddHeight("", u.m_hEnd);

		u.m_EarnedBeamX += p.Remove(u.m_PoolUser);
		DocAddAmount("", u.m_EarnedBeamX);

		DocAddPk("", k0.m_KeyInContract.m_pk);
	}
}

static void OnState_Inner(const ContractID& cid)
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = cid;
	k.m_KeyInContract = DaoAccumulator::Tags::s_State;

	DaoAccumulator::State s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	DocAddAid("BeamX", s.m_aidBeamX);
	DocAddHeight("Pre-phaseend height ", s.m_hPreEnd);

	if (s.m_aidLpToken)
	{
		DocAddAid("LP-token", s.m_aidLpToken);
		OnState_Pool(s.m_Pool, "Pool Beam/BeamX");
	}

	DaoAccumulator::Pool p_Nph;
	k.m_KeyInContract = DaoAccumulator::Tags::s_PoolBeamNph;

	if (Env::VarReader::Read_T(k, p_Nph))
		OnState_Pool(p_Nph, "Pool Beam/Nph");
	else
		_POD_(p_Nph).SetZero();

	OnState_Users(cid, s.m_Pool, DaoAccumulator::Tags::s_User, "Beam/BeamX users");
	OnState_Users(cid, p_Nph, DaoAccumulator::Tags::s_UserBeamNph, "Beam/Nph users");
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, DaoAccumulator::s_pSID, _countof(DaoAccumulator::s_pSID));
}
BEAM_EXPORT void Method_0(const ShaderID&, const ContractID&, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr(""); OnKind();
	OnMethod_Inner(iMethod, pArg, nArg);
}
BEAM_EXPORT void Method_1(const ShaderID&, const ContractID&) { Env::DocGroup gr(""); OnKind(); }
BEAM_EXPORT void Method_2(const ShaderID&, const ContractID& cid)
{
	Env::DocGroup gr(""); OnKind();
	{ Env::DocGroup grSt("State"); OnState_Inner(cid); }
}
