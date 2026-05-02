#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static const ShaderID s_pSid[] = { Vault::s_SID };

static void OnKind() { Env::DocAddText("kind", "Vault"); }

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0: Env::DocAddText("method", "Create"); break;
	case 1: Env::DocAddText("method", "Destroy"); break;

	case Vault::Deposit::s_iMethod:
		if (nArg >= sizeof(Vault::Deposit))
		{
			auto* p = (const Vault::Deposit*) pArg;
			Env::DocAddText("method", "Deposit");
			Env::DocGroup gr("params");
			DocAddPk("User", p->m_Account);
		}
		break;

	case Vault::Withdraw::s_iMethod:
		Env::DocAddText("method", "Withdraw");
		break;
	}
}

BEAM_EXPORT void Method_0(const ShaderID&, const ContractID&, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr(""); OnKind();
	OnMethod_Inner(iMethod, pArg, nArg);
}

PARSER_MODULE_EXPORT_SIDS(s_pSid)
PARSER_MODULE_EXPORT_KIND_ONLY(OnKind)
