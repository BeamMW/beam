#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static const ShaderID s_pSid[] = { Faucet::s_SID };

static void OnKind() { Env::DocAddText("kind", "Faucet"); }

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(Faucet::Params))
		{
			auto* p = (const Faucet::Params*) pArg;
			Env::DocGroup gr("params");
			Env::DocAddNum("Backlog period", p->m_BacklogPeriod);
			DocAddAmount("Max withdraw", p->m_MaxWithdraw);
		}
		break;

	case 1: Env::DocAddText("method", "Destroy"); break;

	case Faucet::Deposit::s_iMethod:  Env::DocAddText("method", "deposit"); break;
	case Faucet::Withdraw::s_iMethod: Env::DocAddText("method", "withdraw"); break;
	}
}

BEAM_EXPORT void Method_0(const ShaderID&, const ContractID&, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr(""); OnKind();
	OnMethod_Inner(iMethod, pArg, nArg);
}

PARSER_MODULE_EXPORT_SIDS(s_pSid)
PARSER_MODULE_EXPORT_KIND_ONLY(OnKind)
