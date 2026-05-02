#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static const ShaderID s_pSid[] = { Faucet2::s_SID };

static void OnKind() { Env::DocAddText("kind", "Faucet2"); }

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case Faucet2::Method::Create::s_iMethod:
		Env::DocAddText("method", "Create");
		break;

	case 1: Env::DocAddText("method", "Destroy"); break;

	case Faucet2::Method::Deposit::s_iMethod:
		Env::DocAddText("method", "Deposit");
		break;

	case Faucet2::Method::Withdraw::s_iMethod:
		Env::DocAddText("method", "Withdraw");
		break;

	case Faucet2::Method::AdminCtl::s_iMethod:
		Env::DocAddText("method", "Admin-Ctl");
		if (nArg >= sizeof(Faucet2::Method::AdminCtl))
		{
			auto* p = (const Faucet2::Method::AdminCtl*) pArg;
			Env::DocGroup gr("params");
			Env::DocAddNum32("Enable", p->m_Enable);
		}
		break;

	case Faucet2::Method::AdminWithdraw::s_iMethod:
		Env::DocAddText("method", "Admin-Withdraw");
		break;
	}
}

static void OnState_Inner(const ContractID& cid)
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = cid;
	k.m_KeyInContract = Faucet2::State::s_Key;

	Faucet2::State s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	Env::DocAddNum("Enabled", (uint32_t) s.m_Enabled);
	DocAddHeight1("Last withdraw", s.m_Epoch.m_Height);
	DocAddAmount("Epoch withdraw remaining", s.m_Epoch.m_Amount);

	{
		Env::DocGroup gr("Settings");

		Env::DocAddNum("Epoch duration", s.m_Params.m_Limit.m_Height);
		DocAddAmount("Epoch Withdraw limit", s.m_Params.m_Limit.m_Amount);
		DocAddPk("Admin", s.m_Params.m_pkAdmin);
	}
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, s_pSid, _countof(s_pSid));
}

BEAM_EXPORT void Method_0(const ShaderID& /*sid*/, const ContractID& /*cid*/, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr("");
	OnKind();
	OnMethod_Inner(iMethod, pArg, nArg);
}

BEAM_EXPORT void Method_1(const ShaderID& /*sid*/, const ContractID& /*cid*/)
{
	Env::DocGroup gr("");
	OnKind();
}

BEAM_EXPORT void Method_2(const ShaderID& /*sid*/, const ContractID& cid)
{
	Env::DocGroup gr("");
	OnKind();
	{
		Env::DocGroup grSt("State");
		OnState_Inner(cid);
	}
}
