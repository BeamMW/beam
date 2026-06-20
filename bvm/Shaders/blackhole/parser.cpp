#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static const ShaderID s_pSid[] = { BlackHole::s_SID };

static void OnKind() { Env::DocAddText("kind", "BlackHole"); }

static void OnMethod_Inner(uint32_t iMethod)
{
	switch (iMethod)
	{
	case 0: Env::DocAddText("method", "Create"); break;
	case 1: Env::DocAddText("method", "Destroy"); break;

	case BlackHole::Method::Deposit::s_iMethod:
		Env::DocAddText("method", "Deposit");
		break;
	}
}

BEAM_EXPORT void Method_0(const ShaderID&, const ContractID&, uint32_t iMethod, const void*, uint32_t)
{
	Env::DocGroup gr(""); OnKind();
	OnMethod_Inner(iMethod);
}

PARSER_MODULE_EXPORT_SIDS(s_pSid)
PARSER_MODULE_EXPORT_KIND_ONLY(OnKind)
