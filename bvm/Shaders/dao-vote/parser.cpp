// Parser module for DaoVote (versioned, behind Upgradable3).
#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static void OnKind() { Env::DocAddText("kind", "DaoVote"); }

static void WriteDaoVoteCfg(const DaoVote::Cfg& cfg)
{
	DocAddAid("Voting asset", cfg.m_Aid);
	Env::DocAddNum("Epoch duration", cfg.m_hEpochDuration);
	DocAddPk("Admin", cfg.m_pkAdmin);
}

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 1: Env::DocAddText("method", "Destroy"); break;

	case DaoVote::Method::Create::s_iMethod:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(DaoVote::Method::Create))
		{
			auto* p = (const DaoVote::Method::Create*) pArg;
			Env::DocGroup gr("params");
			WriteUpgradeSettings(p->m_Upgradable);
			WriteDaoVoteCfg(p->m_Cfg);
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		// Control method handling intentionally minimal — host emits wrapper info.
		Env::DocAddText("method", "Upgradable3 Control");
		break;

	case DaoVote::Method::AddProposal::s_iMethod:
		if (nArg >= sizeof(DaoVote::Method::AddProposal))
		{
			auto* p = (const DaoVote::Method::AddProposal*) pArg;
			Env::DocAddText("method", "Add proposal");
			Env::DocGroup gr("params");
			DocAddPk("moderator", p->m_pkModerator);
		}
		break;

	case DaoVote::Method::MoveFunds::s_iMethod:
		if (nArg >= sizeof(DaoVote::Method::MoveFunds))
		{
			auto* p = (const DaoVote::Method::MoveFunds*) pArg;
			Env::DocAddText("method", p->m_Lock ? "Funds Lock" : "Funds Unlock");
		}
		break;

	case DaoVote::Method::Vote::s_iMethod:
		Env::DocAddText("method", "Vote");
		break;

	case DaoVote::Method::AddDividend::s_iMethod:
		Env::DocAddText("method", "Add dividend");
		break;

	case DaoVote::Method::GetResults::s_iMethod:
		Env::DocAddText("method", "Get results");
		break;

	case DaoVote::Method::SetModerator::s_iMethod:
		if (nArg >= sizeof(DaoVote::Method::SetModerator))
		{
			auto* p = (const DaoVote::Method::SetModerator*) pArg;
			Env::DocAddText("method", "Set moderator");
			Env::DocGroup gr("params");
			DocAddPk("moderator", p->m_pk);
			Env::DocAddNum32("Enable", p->m_Enable);
		}
		break;
	}
}

static void OnState_Inner(const ContractID& cid)
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = cid;
	k.m_KeyInContract = DaoVote::Tags::s_State;

	DaoVote::State s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	{
		Env::DocGroup gr("Settings");
		WriteDaoVoteCfg(s.m_Cfg);
	}
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, DaoVote::s_pSID, _countof(DaoVote::s_pSID));
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
