// Parser module for DaoVault (versioned, behind Upgradable3).
#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static void OnKind() { Env::DocAddText("kind", "DaoVault"); }

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 1: Env::DocAddText("method", "Destroy"); break;

	case DaoVault::Method::Create::s_iMethod:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(DaoVault::Method::Create))
		{
			auto* p = (const DaoVault::Method::Create*) pArg;
			Env::DocGroup gr("params");
			WriteUpgradeSettings(p->m_Upgradable);
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		// Control method handling intentionally minimal — host emits wrapper info.
		Env::DocAddText("method", "Upgradable3 Control");
		break;

	case DaoVault::Method::Deposit::s_iMethod:
		Env::DocAddText("method", "Deposit");
		break;

	case DaoVault::Method::Withdraw::s_iMethod:
		if (nArg >= sizeof(DaoVault::Method::Withdraw))
		{
			auto* p = (const DaoVault::Method::Withdraw*) pArg;
			Env::DocAddText("method", "Withdraw");
			Env::DocGroup gr("params");
			WriteUpgradeAdminsMask(p->m_ApproveMask);
		}
		break;
	}
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, DaoVault::s_pSID, _countof(DaoVault::s_pSID));
}
BEAM_EXPORT void Method_0(const ShaderID&, const ContractID&, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr(""); OnKind();
	OnMethod_Inner(iMethod, pArg, nArg);
}
BEAM_EXPORT void Method_1(const ShaderID&, const ContractID&) { Env::DocGroup gr(""); OnKind(); }
BEAM_EXPORT void Method_2(const ShaderID&, const ContractID&) { Env::DocGroup gr(""); OnKind(); }
