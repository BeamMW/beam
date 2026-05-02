// Parser module for Amm / DEX (versioned, behind Upgradable3).
#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "contract.h"

static void OnKind() { Env::DocAddText("kind", "DEX"); }

static void WriteAmmSettings(const Amm::Settings& stg)
{
	DocAddCid("Dao-Vault", stg.m_cidDaoVault);
}

static const char* get_AmmKind(const Amm::Pool::ID& pid)
{
	switch (pid.m_Fees.m_Kind)
	{
	case 0: return "Low";
	case 1: return "Medium";
	case 2: return "High";
	}
	return "";
}

static void DocSetAmmPool(const Amm::Pool::ID& pid)
{
	DocAddAid("Aid1", pid.m_Aid1);
	DocAddAid("Aid2", pid.m_Aid2);
	Env::DocAddText("Volatility", get_AmmKind(pid));
}

static void OnMethod_Inner(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 1: Env::DocAddText("method", "Destroy"); break;

	case Amm::Method::Create::s_iMethod:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(Amm::Method::Create))
		{
			auto* p = (const Amm::Method::Create*) pArg;
			Env::DocGroup gr("params");
			WriteAmmSettings(p->m_Settings);
			WriteUpgradeSettings(p->m_Upgradable);
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		// Control method handling intentionally minimal — host emits wrapper info.
		Env::DocAddText("method", "Upgradable3 Control");
		break;

	case Amm::Method::PoolCreate::s_iMethod:
		if (nArg >= sizeof(Amm::Method::PoolCreate))
		{
			auto* p = (const Amm::Method::PoolCreate*) pArg;
			Env::DocAddText("method", "Pool Create");
			Env::DocGroup gr("params");
			DocSetAmmPool(p->m_Pid);
			DocAddPk("Creator", p->m_pkCreator);
		}
		break;

	case Amm::Method::PoolDestroy::s_iMethod:
		if (nArg >= sizeof(Amm::Method::PoolDestroy))
		{
			auto* p = (const Amm::Method::PoolDestroy*) pArg;
			Env::DocAddText("method", "Pool Destroy");
			Env::DocGroup gr("params");
			DocSetAmmPool(p->m_Pid);
		}
		break;

	case Amm::Method::AddLiquidity::s_iMethod:
		if (nArg >= sizeof(Amm::Method::AddLiquidity))
		{
			auto* p = (const Amm::Method::AddLiquidity*) pArg;
			Env::DocAddText("method", "Liquidity Add");
			Env::DocGroup gr("params");
			DocSetAmmPool(p->m_Pid);
		}
		break;

	case Amm::Method::Withdraw::s_iMethod:
		if (nArg >= sizeof(Amm::Method::Withdraw))
		{
			auto* p = (const Amm::Method::Withdraw*) pArg;
			Env::DocAddText("method", "Liquidity Withdraw");
			Env::DocGroup gr("params");
			DocSetAmmPool(p->m_Pid);
		}
		break;

	case Amm::Method::Trade::s_iMethod:
		if (nArg >= sizeof(Amm::Method::Trade))
		{
			auto* p = (const Amm::Method::Trade*) pArg;
			Env::DocAddText("method", "Trade");
			Env::DocGroup gr("params");
			DocSetAmmPool(p->m_Pid);
		}
		break;
	}
}

static void OnState_Inner(const ContractID& cid)
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = cid;
	k.m_KeyInContract = Amm::Tags::s_Settings;

	Amm::Settings s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	{
		Env::DocGroup gr2("Settings");
		WriteAmmSettings(s);
	}

	{
		Env::DocGroup gr2("Pools");
		DocSetType("table");
		Env::DocArray gr3("value");

		{
			Env::DocArray gr4("");
			DocAddTableHeader("Aid1");
			DocAddTableHeader("Aid2");
			DocAddTableHeader("Volatility");
			DocAddTableHeader("LP-Token");
			DocAddTableHeader("Amount1");
			DocAddTableHeader("Amount2");
			DocAddTableHeader("Amount-LP-Token");
			DocAddTableHeader("Rate 1:2");
			DocAddTableHeader("Rate 2:2");
		}

		Env::Key_T<Amm::Pool::Key> k0, k1;
		_POD_(k0.m_Prefix.m_Cid) = cid;
		_POD_(k1.m_Prefix.m_Cid) = cid;
		_POD_(k0.m_KeyInContract.m_ID).SetZero();
		_POD_(k1.m_KeyInContract.m_ID).SetObject(0xff);

		for (Env::VarReader r(k0, k1); ; )
		{
			Amm::Pool p;
			if (!r.MoveNext_T(k0, p))
				break;

			Env::DocArray gr4("");
			DocAddAid("", k0.m_KeyInContract.m_ID.m_Aid1);
			DocAddAid("", k0.m_KeyInContract.m_ID.m_Aid2);
			Env::DocAddText("", get_AmmKind(k0.m_KeyInContract.m_ID));
			DocAddAid("", p.m_aidCtl);
			DocAddAmount("", p.m_Totals.m_Tok1);
			DocAddAmount("", p.m_Totals.m_Tok2);
			DocAddAmount("", p.m_Totals.m_Ctl);

			if (p.m_Totals.m_Tok1 && p.m_Totals.m_Tok2)
			{
				char szBuf[MultiPrecision::Float::DecimalForm::s_LenScientificMax + 1];

				MultiPrecision::Float f1(p.m_Totals.m_Tok1);
				MultiPrecision::Float f2(p.m_Totals.m_Tok2);

				for (uint32_t i = 0; i < 2; i++)
				{
					auto kv = i ? (f2 / f1) : (f1 / f2);
					auto df = kv.get_Decimal();

					const uint32_t nPrecision = 8;
					df.LimitPrecision(nPrecision);

					MultiPrecision::Float::DecimalForm::PrintOptions po;

					if (df.get_TextLenStd(po) <= nPrecision + 2)
						df.PrintStd(szBuf, po);
					else
						df.PrintScientific(szBuf, po);

					Env::DocAddText("", szBuf);
				}
			}
			else
			{
				Env::DocAddText("", "");
				Env::DocAddText("", "");
			}
		}
	}
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, Amm::s_pSID, _countof(Amm::s_pSID));
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
