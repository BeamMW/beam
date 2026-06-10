// Parser module for NameService / Bans (versioned, behind Upgradable3).
#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static void OnKind() { Env::DocAddText("kind", "Bans"); }

static void WriteBansSettings(const NameService::Settings& stg)
{
	DocAddCid("Price oracle", stg.m_cidOracle);
	DocAddCid("Dao-Vault", stg.m_cidDaoVault);
	DocAddCid("Anon-Vault", stg.m_cidVault);
	DocAddHeight("Activation height", stg.m_h0);
}

// emit "name" field from a name-prefixed method arg, bounded by the original arg buffer.
template <typename T>
static void DocSetBansName(const void* pArg, uint32_t nArg, const T& x)
{
	uint32_t nameLen = x.m_NameLen;
	if (nameLen > NameService::Domain::s_MaxLen)
		return;

	const uint8_t* p = (const uint8_t*) (&x + 1);
	uint32_t n0 = p - (const uint8_t*) pArg;
	if (n0 + nameLen > nArg)
		return;

	char sz[NameService::Domain::s_MaxLen + 1];
	Env::Memcpy(sz, p, nameLen);
	sz[nameLen] = 0;
	Env::DocAddText("name", sz);
}

static void OnMethod_Inner(const void* pArg, uint32_t nArg, uint32_t iMethod)
{
	switch (iMethod)
	{
	case 1: Env::DocAddText("method", "Destroy"); break;

	case NameService::Method::Create::s_iMethod:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(NameService::Method::Create))
		{
			auto* p = (const NameService::Method::Create*) pArg;
			Env::DocGroup gr("params");
			WriteBansSettings(p->m_Settings);
			WriteUpgradeSettings(p->m_Upgradable);
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		// Control method handling intentionally minimal — host emits wrapper info.
		Env::DocAddText("method", "Upgradable3 Control");
		break;

	case NameService::Method::SetOwner::s_iMethod:
		if (nArg >= sizeof(NameService::Method::SetOwner))
		{
			auto* p = (const NameService::Method::SetOwner*) pArg;
			Env::DocAddText("method", "Set Owner");
			Env::DocGroup gr("params");
			DocSetBansName(pArg, nArg, *p);
			DocAddPk("New owner", p->m_pkNewOwner);
		}
		break;

	case NameService::Method::Extend::s_iMethod:
		if (nArg >= sizeof(NameService::Method::Extend))
		{
			auto* p = (const NameService::Method::Extend*) pArg;
			Env::DocAddText("method", "Extend period");
			Env::DocGroup gr("params");
			DocSetBansName(pArg, nArg, *p);
			Env::DocAddNum32("Periods", p->m_Periods);
		}
		break;

	case NameService::Method::SetPrice::s_iMethod:
		if (nArg >= sizeof(NameService::Method::SetPrice))
		{
			auto* p = (const NameService::Method::SetPrice*) pArg;
			Env::DocAddText("method", p->m_Price.m_Amount ? "Set Price" : "Remove Price");
			Env::DocGroup gr("params");
			DocSetBansName(pArg, nArg, *p);
			if (p->m_Price.m_Amount)
				DocAddAidAmount("Price", p->m_Price.m_Aid, p->m_Price.m_Amount);
		}
		break;

	case NameService::Method::Buy::s_iMethod:
		if (nArg >= sizeof(NameService::Method::Buy))
		{
			auto* p = (const NameService::Method::Buy*) pArg;
			Env::DocAddText("method", "Buy");
			Env::DocGroup gr("params");
			DocSetBansName(pArg, nArg, *p);
			DocAddPk("New owner", p->m_pkNewOwner);
		}
		break;

	case NameService::Method::Register::s_iMethod:
		if (nArg >= sizeof(NameService::Method::Register))
		{
			auto* p = (const NameService::Method::Register*) pArg;
			Env::DocAddText("method", "Register");
			Env::DocGroup gr("params");
			DocSetBansName(pArg, nArg, *p);
			DocAddPk("Owner", p->m_pkOwner);
			Env::DocAddNum32("Periods", p->m_Periods);
		}
		break;
	}
}

static void OnState_Inner(const ContractID& cid)
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = cid;
	k.m_KeyInContract = NameService::Tags::s_Settings;

	NameService::Settings s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	{
		Env::DocGroup gr2("Settings");
		WriteBansSettings(s);
	}

	{
		Env::DocGroup gr2("Domains");
		DocSetType("table");
		Env::DocArray gr3("value");

		{
			Env::DocArray gr4("");
			DocAddTableHeader("Name");
			DocAddTableHeader("Owner");
			DocAddTableHeader("Expiration height");
			DocAddTableHeader("Status");
			DocAddTableHeader("Sell price");
		}

		Env::Key_T<NameService::Domain::Key0> k0;
		_POD_(k0.m_Prefix.m_Cid) = cid;
		_POD_(k0.m_KeyInContract.m_sz).SetZero();

#pragma pack (push, 1)
		struct KeyPlus {
			Env::Key_T<NameService::Domain::KeyMax> k;
			char m_chTerm;
		} k1;
#pragma pack (pop)

		_POD_(k1.k.m_Prefix.m_Cid) = cid;
		Env::Memset(k1.k.m_KeyInContract.m_sz, 0xff, NameService::Domain::s_MaxLen);

		Height h = Env::get_Height();
		for (Env::VarReader r(k0, k1.k); ; )
		{
			NameService::Domain d;
			uint32_t nKey = sizeof(k1.k), nVal = sizeof(d);
			if (!r.MoveNext(&k1.k, nKey, &d, nVal, 0))
				break;

			if (sizeof(d) != nVal)
				continue;

			nKey -= (sizeof(k0) - NameService::Domain::s_MinLen);
			if (nKey > NameService::Domain::s_MaxLen)
				continue;

			k1.k.m_KeyInContract.m_sz[nKey] = 0;

			Env::DocArray gr4("");
			Env::DocAddText("", k1.k.m_KeyInContract.m_sz);
			DocAddPk("", d.m_pkOwner);
			DocAddHeight("", d.m_hExpire);

			const char* szStatus =
				(d.m_hExpire > h) ? "" :
				(d.m_hExpire + NameService::Domain::s_PeriodHold > h) ? "On Hold" :
				"Expired";
			Env::DocAddText("", szStatus);

			if (d.m_Price.m_Amount)
				DocAddAidAmount("", d.m_Price.m_Aid, d.m_Price.m_Amount);
			else
				Env::DocAddText("", "");
		}
	}
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, NameService::s_pSID, _countof(NameService::s_pSID));
}
BEAM_EXPORT void Method_0(const ShaderID&, const ContractID&, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr(""); OnKind();
	OnMethod_Inner(pArg, nArg, iMethod);
}
BEAM_EXPORT void Method_1(const ShaderID&, const ContractID&) { Env::DocGroup gr(""); OnKind(); }
BEAM_EXPORT void Method_2(const ShaderID&, const ContractID& cid)
{
	Env::DocGroup gr(""); OnKind();
	{ Env::DocGroup grSt("State"); OnState_Inner(cid); }
}
