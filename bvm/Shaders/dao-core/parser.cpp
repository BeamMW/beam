// Parser module for DaoCore — handles mainnet, masternet, and testnet variants.
// All three share identical method dispatch; SIDs differ.
#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "../dao-core2/contract.h"
#include "contract.h"
namespace Masternet {
#include "../dao-core-masternet/contract.h"
}
namespace Testnet {
#include "../dao-core-testnet/contract.h"
}

static const ShaderID s_pSid[] = {
	DaoCore::s_SID,
	Masternet::DaoCore::s_SID,
	Testnet::DaoCore::s_SID,
};

static void OnKind() { Env::DocAddText("kind", "Dao-Core"); }

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0: Env::DocAddText("method", "Create"); break;
	case 1: Env::DocAddText("method", "Destroy"); break;
	case DaoCore::GetPreallocated::s_iMethod: Env::DocAddText("method", "Get Preallocated"); break;
	case DaoCore::UpdPosFarming::s_iMethod:   Env::DocAddText("method", "Farming Upd"); break;

	case DaoCore2::Method::AdminWithdraw::s_iMethod:
		Env::DocAddText("method", "Admin Withdraw");
		if (nArg >= sizeof(DaoCore2::Method::AdminWithdraw))
		{
			auto* p = (const DaoCore2::Method::AdminWithdraw*) pArg;
			Env::DocGroup gr("params");
			WriteUpgradeAdminsMask(p->m_ApproveMask);
		}
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
