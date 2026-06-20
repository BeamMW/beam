// Parser module for SidechainPos L1 and L2 bridges.
#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract_l1.h"
#include "contract_l2.h"

static ShaderID g_Sids[1 + _countof(SidechainPos::L1::s_pSID)];

static uint32_t init_sids()
{
	uint32_t n = 0;
	_POD_(g_Sids[n++]) = SidechainPos::L2::s_SID;
	for (uint32_t i = 0; i < _countof(SidechainPos::L1::s_pSID); i++)
		_POD_(g_Sids[n++]) = SidechainPos::L1::s_pSID[i];
	return n;
}

static const char* KindFor(const ShaderID& sid)
{
	if (_POD_(sid) == SidechainPos::L2::s_SID) return "Bridge_L2";
	for (uint32_t i = 0; i < _countof(SidechainPos::L1::s_pSID); i++)
		if (_POD_(sid) == SidechainPos::L1::s_pSID[i])
			return "Bridge_L1";
	return "Bridge_?";
}

static void WriteSposSettings(const SidechainPos::L1::Settings& stg)
{
	DocAddAid("Staking Token", stg.m_aidStaking);
	DocAddAid("Liquidity Token", stg.m_aidLiquidity);
	DocAddHeight("Per-phase End", stg.m_hPreEnd);
}

static void WriteSposValidators(const SidechainPos::L1::Validator* pV, uint32_t nV)
{
	Env::DocGroup gr1("Validators");
	DocSetType("table");
	Env::DocArray gr2("value");

	{
		Env::DocArray gr3("");
		DocAddTableHeader("Index");
		DocAddTableHeader("Key");
	}

	for (uint32_t i = 0; i < nV; i++)
	{
		Env::DocArray gr3("");
		Env::DocAddNum("", i);
		DocAddPk("", pV[i].m_pk);
	}
}

static void OnSposBridgeOp(const SidechainPos::L1::Method::BridgeOp& op)
{
	DocAddAidAmount("Value", op.m_Aid, op.m_Amount);
	DocAddMonoblob("cookie", op.m_Cookie);
	DocAddPk("pk", op.m_pk);
}

static void OnMethod_L1(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 1: Env::DocAddText("method", "Destroy"); break;

	case SidechainPos::L1::Method::Create::s_iMethod:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(SidechainPos::L1::Method::Create))
		{
			auto* p = (const SidechainPos::L1::Method::Create*) pArg;
			Env::DocGroup gr("params");
			WriteSposSettings(p->m_Settings);
			WriteUpgradeSettings(p->m_Upgradable);
			if (nArg >= sizeof(*p) + sizeof(SidechainPos::L1::Validator) * p->m_Validators)
				WriteSposValidators((const SidechainPos::L1::Validator*) (p + 1), p->m_Validators);
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		// Control method handling intentionally minimal — host emits wrapper info.
		Env::DocAddText("method", "Upgradable3 Control");
		break;

	case SidechainPos::L1::Method::UserStake::s_iMethod:
		if (nArg >= sizeof(SidechainPos::L1::Method::UserStake))
		{
			auto* p = (const SidechainPos::L1::Method::UserStake*) pArg;
			Env::DocAddText("method", "User stake");
			Env::DocGroup gr("params");
			DocAddAmount("Amount", p->m_Amount);
			DocAddPk("Pk", p->m_pkUser);
		}
		break;

	case SidechainPos::L1::Method::BridgeExport::s_iMethod:
		if (nArg >= sizeof(SidechainPos::L1::Method::BridgeExport))
		{
			auto* p = (const SidechainPos::L1::Method::BridgeExport*) pArg;
			Env::DocAddText("method", "Bridge Export");
			Env::DocGroup gr("params");
			OnSposBridgeOp(*p);
		}
		break;

	case SidechainPos::L1::Method::BridgeImport::s_iMethod:
		if (nArg >= sizeof(SidechainPos::L1::Method::BridgeImport))
		{
			auto* p = (const SidechainPos::L1::Method::BridgeImport*) pArg;
			Env::DocAddText("method", "Bridge Import");
			Env::DocGroup gr("params");
			OnSposBridgeOp(*p);
			WriteUpgradeAdminsMask(p->m_ApproveMask);
		}
		break;
	}
}

static void OnState_L1(const ContractID& cid)
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = cid;
	k.m_KeyInContract = SidechainPos::L1::Tags::s_State;

	SidechainPos::L1::State s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	{
		Env::DocGroup gr2("Settings");
		WriteSposSettings(s.m_Settings);
	}

	{
		k.m_KeyInContract = SidechainPos::L1::Tags::s_Validators;
		Env::VarReader r(k, k);

		SidechainPos::L1::Validator pV[SidechainPos::L1::Validator::s_Max];
		uint32_t nKey = 0, nVal = sizeof(pV);

		if (r.MoveNext(nullptr, nKey, pV, nVal, 0) && (nVal >= sizeof(SidechainPos::L1::Validator)) && (nVal <= sizeof(pV)))
			WriteSposValidators(pV, nVal / sizeof(SidechainPos::L1::Validator));
	}
}

static void OnMethod_L2(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0: Env::DocAddText("method", "Create"); break;
	case 1: Env::DocAddText("method", "Destroy"); break;

	case SidechainPos::L2::Method::BridgeEmit::s_iMethod:
		if (nArg >= sizeof(SidechainPos::L2::Method::BridgeEmit))
		{
			auto* p = (const SidechainPos::L2::Method::BridgeEmit*) pArg;
			Env::DocAddText("method", "Mint");
			Env::DocGroup gr("params");
			OnSposBridgeOp(*p);
		}
		break;

	case SidechainPos::L2::Method::BridgeBurn::s_iMethod:
		if (nArg >= sizeof(SidechainPos::L2::Method::BridgeBurn))
		{
			auto* p = (const SidechainPos::L2::Method::BridgeBurn*) pArg;
			Env::DocAddText("method", "Burn");
			Env::DocGroup gr("params");
			OnSposBridgeOp(*p);
		}
		break;
	}
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	uint32_t n = init_sids();
	return ParserModule_FillSids(out_buf, out_cap, g_Sids, n);
}

BEAM_EXPORT void Method_0(const ShaderID& sid, const ContractID&, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr("");
	Env::DocAddText("kind", KindFor(sid));
	if (_POD_(sid) == SidechainPos::L2::s_SID)
		OnMethod_L2(iMethod, pArg, nArg);
	else
		OnMethod_L1(iMethod, pArg, nArg);
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
	if (_POD_(sid) == SidechainPos::L2::s_SID)
		return; // L2 has no state
	Env::DocGroup grSt("State");
	OnState_L1(cid);
}
