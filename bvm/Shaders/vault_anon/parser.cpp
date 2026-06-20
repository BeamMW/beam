#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static const ShaderID s_pSid[] = { VaultAnon::s_SID };

static void OnKind() { Env::DocAddText("kind", "VaultAnon"); }

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0: Env::DocAddText("method", "Create"); break;
	case 1: Env::DocAddText("method", "Destroy"); break;

	case VaultAnon::Method::Deposit::s_iMethod:
		if (nArg >= sizeof(VaultAnon::Method::Deposit))
		{
			auto* p = (const VaultAnon::Method::Deposit*) pArg;
			Env::DocAddText("method", "Deposit");
			Env::DocGroup gr("params");
			DocAddPk("User", p->m_Key.m_pkOwner);
		}
		break;

	case VaultAnon::Method::Withdraw::s_iMethod:
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
