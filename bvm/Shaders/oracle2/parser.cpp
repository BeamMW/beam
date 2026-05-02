// Parser module for Oracle2 (versioned, behind Upgradable3).
#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static void OnKind() { Env::DocAddText("kind", "Oracle2"); }

static void WriteOracle2Settings(const Oracle2::Settings& stg)
{
	Env::DocAddNum("Validity Period", stg.m_hValidity);
	Env::DocAddNum("Min Providers", stg.m_MinProviders);
}

static bool get_Oracle2Median(MultiPrecision::Float& ret, const ContractID& cid)
{
	Env::Key_T<uint8_t> key;
	_POD_(key.m_Prefix.m_Cid) = cid;
	key.m_KeyInContract = Oracle2::Tags::s_Median;

	Oracle2::Median med;
	if (!Env::VarReader::Read_T(key, med))
		return false;
	if (med.m_hEnd < Env::get_Height())
		return false;

	ret = med.m_Res;
	return true;
}

static void OnMethod_Inner(const ContractID& cid, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 1: Env::DocAddText("method", "Destroy"); break;

	case Oracle2::Method::Create::s_iMethod:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(Oracle2::Method::Create))
		{
			auto* p = (const Oracle2::Method::Create*) pArg;
			Env::DocGroup gr("params");
			WriteOracle2Settings(p->m_Settings);
			WriteUpgradeSettings(p->m_Upgradable);
		}
		break;

	case Oracle2::Method::Get::s_iMethod:
		if (nArg >= sizeof(Oracle2::Method::Get))
		{
			Env::DocAddText("method", "Get");
			Env::DocGroup gr("params");
			MultiPrecision::Float val;
			if (get_Oracle2Median(val, cid))
				DocAddFloat("Result", val);
			else
				Env::DocAddText("Result", "NaN");
		}
		break;

	case Oracle2::Method::FeedData::s_iMethod:
		if (nArg >= sizeof(Oracle2::Method::FeedData))
		{
			auto* p = (const Oracle2::Method::FeedData*) pArg;
			Env::DocAddText("method", "FeedData");
			Env::DocGroup gr("params");
			Env::DocAddNum("iProvider", p->m_iProvider);
			DocAddFloat("Value", p->m_Value);
		}
		break;

	case Oracle2::Method::SetSettings::s_iMethod:
		if (nArg >= sizeof(Oracle2::Method::SetSettings))
		{
			auto* p = (const Oracle2::Method::SetSettings*) pArg;
			Env::DocAddText("method", "SetSettings");
			Env::DocGroup gr("params");
			WriteOracle2Settings(p->m_Settings);
			WriteUpgradeAdminsMask(p->m_ApproveMask);
		}
		break;

	case Oracle2::Method::ProviderAdd::s_iMethod:
		if (nArg >= sizeof(Oracle2::Method::ProviderAdd))
		{
			auto* p = (const Oracle2::Method::ProviderAdd*) pArg;
			Env::DocAddText("method", "ProviderAdd");
			Env::DocGroup gr("params");
			DocAddPk("pk", p->m_pk);
			WriteUpgradeAdminsMask(p->m_ApproveMask);
		}
		break;

	case Oracle2::Method::ProviderDel::s_iMethod:
		if (nArg >= sizeof(Oracle2::Method::ProviderDel))
		{
			auto* p = (const Oracle2::Method::ProviderDel*) pArg;
			Env::DocAddText("method", "ProviderDel");
			Env::DocGroup gr("params");
			Env::DocAddNum("iProvider", p->m_iProvider);
			WriteUpgradeAdminsMask(p->m_ApproveMask);
		}
		break;
	}
}

static void OnState_Inner(const ContractID& cid)
{
	Oracle2::StateMax g;
	uint32_t nProvs = 0;

	Env::Key_T<uint8_t> key;
	_POD_(key.m_Prefix.m_Cid) = cid;
	key.m_KeyInContract = Oracle2::Tags::s_StateFull;

	{
		Env::VarReader r(key, key);
		uint32_t nKey = 0, nVal = sizeof(g);
		if (!r.MoveNext(nullptr, nKey, &g, nVal, 0))
			return;
		nProvs = (nVal - sizeof(Oracle2::State0)) / sizeof(Oracle2::State0::Entry);
		if (nProvs > g.s_ProvsMax)
			return;
	}

	{
		Env::DocGroup gr1("Settings");
		WriteOracle2Settings(g.m_Settings);
	}

	Height h = Env::get_Height();

	{
		Env::DocGroup gr1("Feeds");
		DocSetType("table");
		Env::DocArray gr2("value");
		{
			Env::DocArray gr3("");
			DocAddTableHeader("Index");
			DocAddTableHeader("Key");
			DocAddTableHeader("Last Value");
			DocAddTableHeader("Last Height");
			DocAddTableHeader("Comment");
		}

		Height h1 = (h > g.m_Settings.m_hValidity) ? (h - g.m_Settings.m_hValidity) : 0;

		for (uint32_t i = 0; i < nProvs; i++)
		{
			const auto& x = g.m_pE[i];
			if (_POD_(x.m_Pk).IsZero())
				continue;

			Env::DocArray gr3("");
			Env::DocAddNum("", i);
			DocAddPk("", x.m_Pk);
			DocAddFloat("", x.m_Val);
			DocAddHeight1("", x.m_hUpdated);
			Env::DocAddText("", (x.m_hUpdated > h1) ? "" : "outdated");
		}
	}

	{
		key.m_KeyInContract = Oracle2::Tags::s_Median;
		Oracle2::Median med;
		if (Env::VarReader::Read_T(key, med))
		{
			bool bValid = (med.m_hEnd >= h);
			if (bValid)
				DocAddFloat("Median", med.m_Res);
			else
				Env::DocAddText("Median", "");
		}
	}
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, Oracle2::s_pSID, _countof(Oracle2::s_pSID));
}
BEAM_EXPORT void Method_0(const ShaderID&, const ContractID& cid, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr(""); OnKind();
	OnMethod_Inner(cid, iMethod, pArg, nArg);
}
BEAM_EXPORT void Method_1(const ShaderID&, const ContractID&) { Env::DocGroup gr(""); OnKind(); }
BEAM_EXPORT void Method_2(const ShaderID&, const ContractID& cid)
{
	Env::DocGroup gr(""); OnKind();
	{ Env::DocGroup grSt("State"); OnState_Inner(cid); }
}
