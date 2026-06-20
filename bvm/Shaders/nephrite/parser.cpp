// Parser module for Nephrite (versioned, behind Upgradable3).
#include "../common.h"
#include "../Explorer/parser_module_abi.h"
#include "../oracle2/contract.h"
#include "contract.h"

template <uint8_t nTag>
struct NephriteEpochStorage
{
	const ContractID& m_Cid;
	NephriteEpochStorage(const ContractID& cid) :m_Cid(cid) {}

	template <uint32_t nDims>
	void Load(uint32_t iEpoch, HomogenousPool::Epoch<nDims>& e)
	{
		Env::Key_T<Nephrite::EpochKey> k;
		_POD_(k.m_Prefix.m_Cid) = m_Cid;
		k.m_KeyInContract.m_Tag = nTag;
		k.m_KeyInContract.m_iEpoch = iEpoch;
		Env::Halt_if(!Env::VarReader::Read_T(k, e));
	}

	template <uint32_t nDims>
	void Save(uint32_t, const HomogenousPool::Epoch<nDims>&) {}
	void Del(uint32_t) {}
};

static void OnKind() { Env::DocAddText("kind", "Nephrite"); }

static void WriteNephriteSettings(const Nephrite::Settings& stg)
{
	DocAddCid("oracle", stg.m_cidOracle1);
	DocAddCid("oracle-backup", stg.m_cidOracle2);
	DocAddCid("Dao-Vault", stg.m_cidDaoVault);
	DocAddAmount("Liquidation Reserve", stg.m_TroveLiquidationReserve);
	DocAddHeight("Min Redemption Height", stg.m_hMinRedemptionHeight);
	DocAddAid("Gov Token", stg.m_AidGov);
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

static void AddTroveNumber(const ContractID& cid, const Nephrite::Method::BaseTxTrove* pArg)
{
	uint32_t iTrove;

	if (pArg && pArg->m_iPrev0)
	{
		Env::Key_T<Nephrite::Trove::Key> tk;
		_POD_(tk.m_Prefix.m_Cid) = cid;
		tk.m_KeyInContract.m_iTrove = pArg->m_iPrev0;

		Nephrite::Trove t;
		if (!Env::VarReader::Read_T(tk, t))
			return;

		iTrove = t.m_iNext;
	}
	else
	{
		Env::Key_T<uint8_t> k;
		_POD_(k.m_Prefix.m_Cid) = cid;
		k.m_KeyInContract = Nephrite::Tags::s_State;

		Nephrite::Global g;
		if (!Env::VarReader::Read_T(k, g))
			return;

		iTrove = pArg ? g.m_Troves.m_iHead : (g.m_Troves.m_iLastCreated + 1);
	}

	Env::DocAddNum("Number", iTrove);
}

static void OnMethod_Inner(const ContractID& cid, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 1: Env::DocAddText("method", "Destroy"); break;

	case Nephrite::Method::Create::s_iMethod:
		Env::DocAddText("method", "Create");
		if (nArg >= sizeof(Nephrite::Method::Create))
		{
			auto* p = (const Nephrite::Method::Create*) pArg;
			Env::DocGroup gr("params");
			WriteNephriteSettings(p->m_Settings);
			WriteUpgradeSettings(p->m_Upgradable);
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		// Control method handling intentionally minimal — host emits wrapper info.
		Env::DocAddText("method", "Upgradable3 Control");
		break;

	case Nephrite::Method::TroveOpen::s_iMethod:
		if (nArg >= sizeof(Nephrite::Method::TroveOpen))
		{
			auto* p = (const Nephrite::Method::TroveOpen*) pArg;
			Env::DocAddText("method", "Trove Open");
			Env::DocGroup gr("params");
			AddTroveNumber(cid, nullptr);
			DocAddAmount("Col", p->m_Amounts.Col);
			DocAddAmount("Tok", p->m_Amounts.Tok);
		}
		break;

	case Nephrite::Method::TroveClose::s_iMethod:
		if (nArg >= sizeof(Nephrite::Method::TroveClose))
		{
			auto* p = (const Nephrite::Method::TroveClose*) pArg;
			Env::DocAddText("method", "Trove Close");
			Env::DocGroup gr("params");
			AddTroveNumber(cid, p);
		}
		break;

	case Nephrite::Method::TroveModify::s_iMethod:
		if (nArg >= sizeof(Nephrite::Method::TroveModify))
		{
			auto* p = (const Nephrite::Method::TroveModify*) pArg;
			Env::DocAddText("method", "Trove Modify");
			Env::DocGroup gr("params");
			AddTroveNumber(cid, p);
			DocAddAmount("Col", p->m_Amounts.Col);
			DocAddAmount("Tok", p->m_Amounts.Tok);
		}
		break;

	case Nephrite::Method::FundsAccess::s_iMethod:
		Env::DocAddText("method", "Funds Access");
		break;

	case Nephrite::Method::UpdStabPool::s_iMethod:
		if (nArg >= sizeof(Nephrite::Method::UpdStabPool))
		{
			auto* p = (const Nephrite::Method::UpdStabPool*) pArg;
			Env::DocAddText("method", "Stability Pool update");
			Env::DocGroup gr("params");
			DocAddAmount("New Amount", p->m_NewAmount);
		}
		break;

	case Nephrite::Method::Liquidate::s_iMethod:
		if (nArg >= sizeof(Nephrite::Method::Liquidate))
		{
			auto* p = (const Nephrite::Method::Liquidate*) pArg;
			Env::DocAddText("method", "Liquidate troves");
			Env::DocGroup gr("params");
			Env::DocAddNum("Count", p->m_Count);
		}
		break;

	case Nephrite::Method::Redeem::s_iMethod:
		if (nArg >= sizeof(Nephrite::Method::Redeem))
		{
			auto* p = (const Nephrite::Method::Redeem*) pArg;
			Env::DocAddText("method", "Redeem");
			Env::DocGroup gr("params");
			DocAddAmount("Amount", p->m_Amount);
		}
		break;

	case Nephrite::Method::AddStabPoolReward::s_iMethod:
		if (nArg >= sizeof(Nephrite::Method::AddStabPoolReward))
		{
			auto* p = (const Nephrite::Method::AddStabPoolReward*) pArg;
			Env::DocAddText("method", "Add Stability Pool Reward");
			Env::DocGroup gr("params");
			DocAddAmount("Amount", p->m_Amount);
		}
		break;
	}
}

static void OnState_Inner(const ContractID& cid)
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = cid;
	k.m_KeyInContract = Nephrite::Tags::s_State;

	Nephrite::Global g;
	if (!Env::VarReader::Read_T(k, g))
		return;

	{
		Env::DocGroup gr2("Settings");
		WriteNephriteSettings(g.m_Settings);
	}

	DocAddAid("Token", g.m_Aid);
	Env::DocAddNum("Troves created", g.m_Troves.m_iLastCreated);

	{
		Env::DocGroup gr2("Totals");
		DocAddAmount("Col", g.m_Troves.m_Totals.Col);
		DocAddAmount("Tok", g.m_Troves.m_Totals.Tok);
	}

	Nephrite::Global::Price price;
	const char* szPriceSource;
	bool bHavePrice = true;

	if (get_Oracle2Median(price.m_Value, g.m_Settings.m_cidOracle1) && price.IsSane(price.m_Value))
		szPriceSource = "Main Oracle";
	else if (get_Oracle2Median(price.m_Value, g.m_Settings.m_cidOracle2) && price.IsSane(price.m_Value))
		szPriceSource = "Backup Oracle";
	else
	{
		szPriceSource = "Unavailable";
		bHavePrice = false;
	}

	Env::DocAddText("Price feed", szPriceSource);
	if (bHavePrice)
	{
		DocAddFloat("Price", price.m_Value);
		if (g.m_Troves.m_iHead)
		{
			DocAddPerc("TCR", price.ToCR(g.m_Troves.m_Totals.get_Rcr()));
			Env::DocAddText("Recovery mode", g.IsRecovery(price) ? "Yes" : "No");
		}
	}

	g.m_BaseRate.Decay();
	DocAddPerc("Fee boost", g.m_BaseRate.m_k);

	{
		Env::DocGroup gr2("Troves");
		DocSetType("table");
		Env::DocArray gr3("value");

		{
			Env::DocArray gr4("");
			DocAddTableHeader("Number");
			DocAddTableHeader("Key");
			DocAddTableHeader("Col");
			DocAddTableHeader("Tok");
			DocAddTableHeader("ICR");
		}

		Utils::Vector<Nephrite::Trove> vec;
		vec.Prepare(g.m_Troves.m_iLastCreated);

		{
			Env::Key_T<Nephrite::Trove::Key> tk0, tk1;
			_POD_(tk0.m_Prefix.m_Cid) = cid;
			_POD_(tk1.m_Prefix.m_Cid) = cid;
			tk0.m_KeyInContract.m_iTrove = 0;
			tk1.m_KeyInContract.m_iTrove = (Nephrite::Trove::ID) -1;

			for (Env::VarReader r(tk0, tk1); ; )
			{
				Nephrite::Trove t;
				if (!r.MoveNext_T(tk0, t))
					break;
				vec.Prepare(tk0.m_KeyInContract.m_iTrove);
				_POD_(vec.m_p[tk0.m_KeyInContract.m_iTrove - 1]) = t;
			}
		}

		for (auto iTrove = g.m_Troves.m_iHead; iTrove; )
		{
			Nephrite::Trove& t = vec.m_p[iTrove - 1];
			NephriteEpochStorage<Nephrite::Tags::s_Epoch_Redist> storR(cid);
			auto vals = g.m_RedistPool.get_UpdatedAmounts(t, storR);

			Env::DocArray gr4("");
			Env::DocAddNum("", iTrove);
			DocAddPk("", t.m_pkOwner);
			DocAddAmount("", vals.Col);
			DocAddAmount("", vals.Tok);

			if (bHavePrice)
				DocAddPerc("", price.ToCR(t.m_Amounts.get_Rcr()));
			else
				Env::DocAddText("", "");

			iTrove = t.m_iNext;
		}
	}

	g.m_StabPool.AddReward(Env::get_Height());

	{
		Env::DocGroup gr2("Stability pool");
		DocAddAmount("Tok", g.m_StabPool.get_TotalSell());
		DocAddAmount("Col", g.m_StabPool.m_Active.m_pDim[0].m_Buy + g.m_StabPool.m_Draining.m_pDim[0].m_Buy);
		DocAddAmount("BeamX", g.m_StabPool.m_Active.m_pDim[1].m_Buy + g.m_StabPool.m_Draining.m_pDim[1].m_Buy);
	}

	DocAddAmount("BeamX reward remaining", g.m_StabPool.m_Reward.m_Remaining);
}

BEAM_EXPORT uint32_t Method_3(ShaderID* out_buf, uint32_t out_cap)
{
	return ParserModule_FillSids(out_buf, out_cap, Nephrite::s_pSID, _countof(Nephrite::s_pSID));
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
