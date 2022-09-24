#include "../common.h"
#include "../app_common_impl.h"
#include "../upgradable/contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable3/contract.h"
#include "../vault/contract.h"
#include "../faucet/contract.h"
#include "../dao-core/contract.h"
#include "../gallery/contract.h"
#include "../nephrite/contract.h"
#include "../oracle2/contract.h"

template <uint32_t nMaxLen>
void DocAddTextLen(const char* szID, const void* szValue, uint32_t nLen)
{
	char szBuf[nMaxLen + 1];
	nLen = std::min(nLen, nMaxLen);

	Env::Memcpy(szBuf, szValue, nLen);
	szBuf[nLen] = 0;

	Env::DocAddText(szID, szBuf);
}

void DocAddPk(const char* sz, const PubKey& pk)
{
	Env::DocAddBlob_T(sz, pk);
}

void DocAddHeight(const char* sz, Height h)
{
	Env::DocAddNum(sz, h);
}

void DocSetType(const char* sz)
{
	Env::DocAddText("type", sz);
}

void DocAddTableField(const char* szName, const char* szType)
{
	Env::DocGroup gr("");
	Env::DocAddText("name", szName);
	if (szType)
		DocSetType(szType);
}

void DocAddAmount(const char* sz, Amount x)
{
	Env::DocGroup gr(sz);
	DocSetType("amount");
	Env::DocAddNum("value", x);
}

void DocAddAid(const char* sz, AssetID aid)
{
	Env::DocGroup gr(sz);
	DocSetType("aid");
	Env::DocAddNum("value", aid);
}

void DocAddCid(const char* sz, const ContractID& cid)
{
	Env::DocGroup gr(sz);
	DocSetType("cid");
	Env::DocAddBlob_T("value", cid);
}

void DocAddFloat(const char* sz, MultiPrecision::Float x, uint32_t nDigsAfterDot = 5)
{
	uint64_t norm = 1;
	for (uint32_t i = 0; i < nDigsAfterDot; i++)
		norm *= 10;

	uint64_t val = x * MultiPrecision::Float(norm * 2);
	val = (val + 1) / 2;

	char szBuf[Utils::String::Decimal::DigitsMax<uint64_t>::N + 2]; // dot + 0-term
	uint32_t nPos = Utils::String::Decimal::Print(szBuf, val / norm);
	szBuf[nPos++] = '.';
	Utils::String::Decimal::Print(szBuf + nPos, val % norm, nDigsAfterDot);

	Env::DocAddText(sz, szBuf);
}

void DocAddPerc(const char* sz, MultiPrecision::Float x, uint32_t nDigsAfterDot = 3)
{
	DocAddFloat(sz, x * MultiPrecision::Float(100), 3);
}


#define HandleContractsAll(macro) \
	macro(Upgradable, Upgradable::s_SID) \
	macro(Upgradable2, Upgradable2::s_SID) \
	macro(Vault, Vault::s_SID) \
	macro(Faucet, Faucet::s_SID) \
	macro(DaoCore, DaoCore::s_SID) \
	macro(Gallery_0, Gallery::s_pSID[0]) \
	macro(Gallery_1, Gallery::s_pSID[1]) \
	macro(Gallery_2, Gallery::s_pSID[2]) \
	macro(Oracle2, Oracle2::s_pSID[0]) \
	macro(Nephrite, Nephrite::s_pSID[0]) \


struct ParserContext
{
	const ShaderID& m_Sid;
	const ContractID& m_Cid;
	uint32_t m_iMethod;
	const void* m_pArg;
	uint32_t m_nArg;

	bool m_Name = true;
	bool m_Method = false;
	bool m_State = false;

	ParserContext(const ShaderID& sid, const ContractID& cid)
		:m_Sid(sid)
		,m_Cid(cid)
	{
	}

	void OnName(const char* sz)
	{
		if (m_Name)
			Env::DocAddText("name", sz);
	}

	void OnStdMethod()
	{
		if (m_Method)
		{
			switch (m_iMethod)
			{
			case 0:
				OnMethod("Create");
				break;

			case 1:
				OnMethod("Destroy");
			}
		}
	}

	void OnMethod(const char* sz)
	{
		assert(m_Method);
		Env::DocAddText("method", sz);
	}

	struct GroupArgs :public Env::DocGroup {
		GroupArgs() :Env::DocGroup("params") {}
	};

	static void OnUpgradableSubtype(const ShaderID& sid) { Env::DocAddBlob_T("subtype", sid); }

#define THE_MACRO(name, sid) void On_##name();
	HandleContractsAll(THE_MACRO)
#undef THE_MACRO

	bool Parse();

	static void WriteUpgradeParams(const Upgradable::Next&);
	static void WriteUpgradeParams(const Upgradable2::Next&);
	static void WriteUpgradeParams(const ContractID&, Height);
	static void WriteUpgradeSettings(const Upgradable3::Settings&);
	static void WriteUpgradeSettingsInternal(const Upgradable3::Settings&);
	void WriteUpgrade3State();
	static void WriteUpgradeAdminsMask(uint32_t nApproveMask);
	static 	void WriteNephriteSettings(const Nephrite::Settings&);
	static void WriteOracle2Settings(const Oracle2::Settings&);
	static bool get_Oracle2Median(MultiPrecision::Float&, const ContractID& cid);
};

bool ParserContext::Parse()
{
#define THE_MACRO(name, sid) \
	if (_POD_(m_Sid) == sid) \
	{ \
		OnStdMethod(); \
		On_##name(); \
		return true; \
	}

	HandleContractsAll(THE_MACRO)
#undef THE_MACRO

	return false;
}

void ParserContext::On_Upgradable()
{
	// Get state, discover which cid actually operates the contract
	if (m_Name || m_State)
	{
		Upgradable::State us;

		if (m_Method && !m_iMethod)
		{
			// c'tor, the state doesn't exist yet. Initial cid should be in the args
			if (m_nArg < sizeof(Upgradable::Create))
				return;

			const auto& arg = *(const Upgradable::Create*)m_pArg;

			_POD_(Cast::Down<Upgradable::Current>(us)) = arg;
			_POD_(Cast::Down<Upgradable::Next>(us)).SetZero();
		}
		else
		{
			Env::Key_T<uint8_t> uk;
			_POD_(uk.m_Prefix.m_Cid) = m_Cid;
			uk.m_KeyInContract = Upgradable::State::s_Key;

			if (!Env::VarReader::Read_T(uk, us))
				return;
		}

		ShaderID sid;
		if (!Utils::get_ShaderID_FromContract(sid, us.m_Cid))
			return;

		ParserContext pc2(sid, m_Cid);
		pc2.m_Name = m_Name;
		pc2.m_State = m_State;

		bool bParsed = pc2.Parse();
		if (m_Name && !bParsed)
		{
			OnName("upgradable");
			OnUpgradableSubtype(sid);
		}

		if (m_State)
		{
			Env::DocGroup gr("upgradable");
			DocAddPk("owner", us.m_Pk);
			WriteUpgradeParams(us);
		}
	}

	if (m_Method && (Upgradable::ScheduleUpgrade::s_iMethod == m_iMethod))
	{
		if (m_nArg < sizeof(Upgradable::ScheduleUpgrade))
			return; // don't care of partial result

		OnMethod("Schedule upgrade");

		GroupArgs gr;
		WriteUpgradeParams(*(const Upgradable::ScheduleUpgrade*) m_pArg);
	}

}

void ParserContext::On_Upgradable2()
{
	// Get state, discover which cid actually operates the contract

	if (m_Name || m_State)
	{
		Upgradable2::State us;
		Upgradable2::Settings stg;
		if (m_Method && !m_iMethod)
		{
			// c'tor, the state doesn't exist yet. Initial cid should be in the args
			if (m_nArg < sizeof(Upgradable2::Create))
				return;

			const auto& arg = *(const Upgradable2::Create*)m_pArg;

			_POD_(us.m_Active) = arg.m_Active;
			_POD_(us.m_Next).SetZero();
			_POD_(stg) = arg.m_Settings;
		}
		else
		{
			Env::Key_T<uint16_t> uk;
			_POD_(uk.m_Prefix.m_Cid) = m_Cid;
			uk.m_KeyInContract = Upgradable2::State::s_Key;

			if (!Env::VarReader::Read_T(uk, us))
				return;

			uk.m_KeyInContract = Upgradable2::Settings::s_Key;
			if (!Env::VarReader::Read_T(uk, stg))
				return;
		}

		ShaderID sid;
		if (!Utils::get_ShaderID_FromContract(sid, us.m_Active.m_Cid))
			return;

		ParserContext pc2(sid, m_Cid);
		pc2.m_Name = m_Name;
		pc2.m_State = m_State;

		bool bParsed = pc2.Parse();
		if (m_Name && !bParsed)
		{
			OnName("upgradable2");
			OnUpgradableSubtype(sid);
		}

		if (m_State)
		{
			Env::DocGroup gr("upgradable2");
			Env::DocAddNum("Num approvers", stg.m_MinApprovers);
			WriteUpgradeParams(us.m_Next);
		}
	}

	if (m_Method && (Upgradable2::Control::s_iMethod == m_iMethod))
	{
		if (m_nArg < sizeof(Upgradable2::Control::Base))
			return; // don't care of partial result

		const auto& ctl = *(const Upgradable2::Control::Base*) m_pArg;

		switch (ctl.m_Type)
		{
		case Upgradable2::Control::ExplicitUpgrade::s_Type:
			OnMethod("explicit upgrade");
			break;

		case Upgradable2::Control::ScheduleUpgrade::s_Type:
			if (m_nArg >= sizeof(Upgradable2::Control::ScheduleUpgrade))
			{
				OnMethod("Schedule upgrade");

				GroupArgs gr;
				const auto& arg = Cast::Up<Upgradable2::Control::ScheduleUpgrade>(ctl);

				WriteUpgradeAdminsMask(arg.m_ApproveMask);
				WriteUpgradeParams(arg.m_Next);
			}
			break;

		case Upgradable2::Control::ReplaceAdmin::s_Type:
			if (m_nArg >= sizeof(Upgradable2::Control::ReplaceAdmin))
			{
				OnMethod("replace admin");

				GroupArgs gr;
				const auto& arg = Cast::Up<Upgradable2::Control::ReplaceAdmin>(ctl);

				WriteUpgradeAdminsMask(arg.m_ApproveMask);

				Env::DocAddNum("iAdmin", arg.m_iAdmin);
				DocAddPk("pk", arg.m_Pk);
			}
			break;

		case Upgradable2::Control::SetApprovers::s_Type:
			if (m_nArg >= sizeof(Upgradable2::Control::SetApprovers))
			{
				OnMethod("set min approvers");

				GroupArgs gr;
				const auto& arg = Cast::Up<Upgradable2::Control::SetApprovers>(ctl);

				WriteUpgradeAdminsMask(arg.m_ApproveMask);

				Env::DocAddNum("num", arg.m_NewVal);
			}
			break;
		}
	}
}

void ParserContext::WriteUpgradeParams(const Upgradable::Next& us)
{
	WriteUpgradeParams(us.m_cidNext, us.m_hNextActivate);
}

void ParserContext::WriteUpgradeParams(const Upgradable2::Next& us)
{
	WriteUpgradeParams(us.m_Cid, us.m_hTarget);
}

void ParserContext::WriteUpgradeAdminsMask(uint32_t nApproveMask)
{
	const uint32_t nDigs = Utils::String::Hex::DigitsMax<uint32_t>::N;
	char szBuf[nDigs + 1];
	Utils::String::Hex::Print(szBuf, nApproveMask, nDigs);

	Env::DocAddText("approve-mask", szBuf);
}

void ParserContext::WriteUpgradeParams(const ContractID& cid, Height h)
{
	if (!_POD_(cid).IsZero())
	{
		ShaderID sid;
		if (!Utils::get_ShaderID_FromContract(sid, cid))
			return;

		Env::DocGroup gr("next upgrade");

		DocAddHeight("height", h);

		ParserContext pc2(sid, cid);
		if (!pc2.Parse())
			OnUpgradableSubtype(sid);
	}
}

void ParserContext::WriteUpgradeSettings(const Upgradable3::Settings& stg)
{
	Env::DocGroup gr("Upgradable3");
	WriteUpgradeSettingsInternal(stg);
}

void ParserContext::WriteUpgradeSettingsInternal(const Upgradable3::Settings& stg)
{
	Env::DocAddNum("Delay", stg.m_hMinUpgradeDelay);
	Env::DocAddNum("Min approvers", stg.m_MinApprovers);

	{
		Env::DocGroup gr1("Admins");
		DocSetType("table");

		{
			Env::DocArray gr2("fields");
			DocAddTableField("Index", nullptr);
			DocAddTableField("Key", nullptr);
		}

		{
			Env::DocArray gr2("value");

			for (uint32_t i = 0; i < _countof(stg.m_pAdmin); i++)
			{
				const auto& pk = stg.m_pAdmin[i];
				if (_POD_(pk).IsZero())
					continue;

				Env::DocArray gr3("");

				Env::DocAddNum("", i);
				Env::DocAddBlob_T("", pk);
			}
		}

	}
}

void ParserContext::WriteUpgrade3State()
{
	Env::DocGroup gr("Upgradable3");

	{
		Env::Key_T<Upgradable3::Settings::Key> sk;
		_POD_(sk.m_Prefix.m_Cid) = m_Cid;

		Upgradable3::Settings stg;
		if (Env::VarReader::Read_T(sk, stg))
		{
			Env::DocGroup gr1("Settings");
			WriteUpgradeSettingsInternal(stg);
		}
	}

	{
		Env::Key_T<Upgradable3::NextVersion::Key> vk;
		_POD_(vk.m_Prefix.m_Cid) = m_Cid;

		Env::VarReader r(vk, vk);
		uint32_t nKey = 0, nVal = 0;
		if (r.MoveNext(nullptr, nKey, nullptr, nVal, 0) && (nVal >= sizeof(Upgradable3::NextVersion)))
		{
			auto* pVal = (Upgradable3::NextVersion*) Env::Heap_Alloc(nVal);

			nKey = 0;
			r.MoveNext(nullptr, nKey, pVal, nVal, 1);

			Env::DocGroup gr1("Schedule upgrade");
			DocAddHeight("Height", pVal->m_hTarget);

			ShaderID sid;
			Utils::get_ShaderID(sid, pVal + 1, nVal - sizeof(Upgradable3::NextVersion));

			ParserContext pc2(sid, m_Cid);
			if (!pc2.Parse())
				OnUpgradableSubtype(pc2.m_Sid);

			Env::Heap_Free(pVal);
		}
	}
}

void ParserContext::On_Vault()
{
	OnName("Vault");

	if (m_Method)
	{
		switch (m_iMethod)
		{
		case Vault::Deposit::s_iMethod:
			OnMethod("Deposit");
			if (m_nArg >= sizeof(Vault::Deposit))
			{
				GroupArgs gr;
				const auto& arg = *(const Vault::Deposit*) m_pArg;
				DocAddPk("User", arg.m_Account);
			}
			break;

		case Vault::Withdraw::s_iMethod:
			OnMethod("Withdraw");
			// no need to include the account, it's visible in the sigs list
			break;
		}
	}

	if (m_State)
	{
		// TODO: write all accounts
	}
}

void ParserContext::On_Faucet()
{
	OnName("Faucet");

	if (m_Method)
	{
		switch (m_iMethod)
		{
		case 0:
			if (m_nArg >= sizeof(Faucet::Params))
			{
				GroupArgs gr;

				auto& pars = *(Faucet::Params*) m_pArg;
				Env::DocAddNum("Backlog period", pars.m_BacklogPeriod);
				DocAddAmount("Max withdraw", pars.m_MaxWithdraw);
			}
			break;

		case Faucet::Deposit::s_iMethod: OnMethod("deposit"); break;
		case Faucet::Withdraw::s_iMethod: OnMethod("withdraw"); break;
		}
	}

	if (m_State)
	{
		// TODO: write all accounts
	}
}

void ParserContext::On_DaoCore()
{
	OnName("Dao-Core");

	if (m_Method)
	{
		switch (m_iMethod)
		{
		case DaoCore::GetPreallocated::s_iMethod: OnMethod("Get Preallocated"); break;
		case DaoCore::UpdPosFarming::s_iMethod: OnMethod("Farming Upd"); break;
		}
	}

	if (m_State)
	{
		// TODO:
	}
}

void WriteGalleryAdrID(Gallery::Masterpiece::ID id)
{
	Env::DocAddNum("art_id", Utils::FromBE(id));
}

void WriteGalleryPrice(const Gallery::AmountWithAsset& x)
{
	if (x.m_Aid)
		DocAddAid("aid", x.m_Aid);
	DocAddAmount("amount", x.m_Amount);
}

void ParserContext::On_Gallery_0()
{
	On_Gallery_2(); // same, we only added methods
}

void ParserContext::On_Gallery_1()
{
	On_Gallery_2();
}

void ParserContext::On_Gallery_2()
{
	OnName("Gallery");

	if (m_Method)
	{
		switch (m_iMethod)
		{
		case Gallery::Method::AddExhibit::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::AddExhibit))
			{
				OnMethod("AddExhibit");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::AddExhibit*>(m_pArg);
				DocAddPk("pkUser", arg.m_pkArtist);
				Env::DocAddNum("size", arg.m_Size);
			}
			break;

		case Gallery::Method::ManageArtist::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::ManageArtist))
			{
				OnMethod("ManageArtist");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::ManageArtist*>(m_pArg);
				DocAddPk("pkUser", arg.m_pkArtist);
				DocAddTextLen<Gallery::Artist::s_LabelMaxLen>("name", &arg + 1, arg.m_LabelLen);
			
			}
			break;
		
		
		case Gallery::Method::SetPrice::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::SetPrice))
			{
				OnMethod("SetPrice");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::SetPrice*>(m_pArg);

				WriteGalleryAdrID(arg.m_ID);
				WriteGalleryPrice(arg.m_Price);
			}
			break;

		case Gallery::Method::Buy::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::Buy))
			{
				OnMethod("Buy");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::Buy*>(m_pArg);

				WriteGalleryAdrID(arg.m_ID);
				
	            DocAddPk("pkUser", arg.m_pkUser);
	            Env::DocAddNum32("hasAid", arg.m_HasAid);
				DocAddAmount("payMax", arg.m_PayMax);
			}
			break;
		
		case Gallery::Method::Transfer::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::Transfer))
			{
				OnMethod("Transfer");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::Transfer*>(m_pArg);

				WriteGalleryAdrID(arg.m_ID);
			
	            DocAddPk("newPkUser", arg.m_pkNewOwner);
			}
			break;
		
			
		case Gallery::Method::Withdraw::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::Withdraw))
			{
				OnMethod("Withdraw");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::Withdraw*>(m_pArg);
				
				// TODO roman
	            Env::DocAddBlob_T("key", arg.m_Key);
				DocAddAid("aid", arg.m_Key.m_Aid);
	            DocAddAmount("value", arg.m_Value);
			}
			break;
		
		case Gallery::Method::AddVoteRewards::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::AddVoteRewards))
			{
				OnMethod("AddVoteRewards");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::AddVoteRewards*>(m_pArg);

	            DocAddAmount("amount", arg.m_Amount);
			}
			break;
		
		

		case Gallery::Method::Vote::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::Vote))
			{
				OnMethod("Vote");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::Vote*>(m_pArg);

				WriteGalleryAdrID(arg.m_ID.m_MasterpieceID);
				Env::DocAddNum("impression", arg.m_Impression.m_Value);
			}
			break;

		case Gallery::Method::AdminDelete::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::AdminDelete))
			{
				OnMethod("AdminDelete");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::AdminDelete*>(m_pArg);
				WriteGalleryAdrID(arg.m_ID);

			}
			break;
		}
	}

	if (m_State)
	{
		// TODO:
	}
}

void ParserContext::WriteNephriteSettings(const Nephrite::Settings& stg)
{
	DocAddCid("oracle", stg.m_cidOracle1);
	DocAddCid("oracle-backup", stg.m_cidOracle2);
	DocAddCid("Dao-Vault", stg.m_cidDaoVault);
	DocAddAmount("Liquidation Reserve", stg.m_TroveLiquidationReserve);
	DocAddHeight("Min Redemption Height", stg.m_hMinRedemptionHeight);
	DocAddAid("Gov Token", stg.m_AidGov);
}

void ParserContext::On_Nephrite()
{
	OnName("Nephrite");

	if (m_Method)
	{
		switch (m_iMethod)
		{
		case Nephrite::Method::Create::s_iMethod:
			if (m_nArg >= sizeof(Nephrite::Method::Create))
			{
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Nephrite::Method::Create*>(m_pArg);

				WriteNephriteSettings(arg.m_Settings);
				WriteUpgradeSettings(arg.m_Upgradable);
			}
			break;

		case Nephrite::Method::TroveOpen::s_iMethod:
			if (m_nArg >= sizeof(Nephrite::Method::TroveOpen))
			{
				OnMethod("TroveOpen");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Nephrite::Method::TroveOpen*>(m_pArg);

				DocAddPk("User", arg.m_pkUser);
				DocAddAmount("Col", arg.m_Amounts.Col);
				DocAddAmount("Tok", arg.m_Amounts.Tok);
		
			}
			break;
		
		case Nephrite::Method::TroveClose::s_iMethod:
			if (m_nArg >= sizeof(Nephrite::Method::TroveClose))
			{
				OnMethod("TroveClose");
				GroupArgs gr;

				// const auto& arg = *reinterpret_cast<const Nephrite::Method::TroveClose*>(m_pArg);
				// TODO
			}
			break;

		}
	}

	if (m_State)
	{
		Env::DocGroup gr("State");

		WriteUpgrade3State();

		Env::Key_T<uint8_t> k;
		_POD_(k.m_Prefix.m_Cid) = m_Cid;
		k.m_KeyInContract = Nephrite::Tags::s_State;

		Nephrite::Global g;
		if (Env::VarReader::Read_T(k, g))
		{
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
			else
			{
				if (get_Oracle2Median(price.m_Value, g.m_Settings.m_cidOracle2) && price.IsSane(price.m_Value))
					szPriceSource = "Backup Oracle";
				else
				{
					szPriceSource = "Unavailable";;
					bHavePrice = false;
				}
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

			{
				Env::DocGroup gr2("Troves");
				DocSetType("table");

				{
					Env::DocArray gr3("fields");
					DocAddTableField("Number", nullptr);
					DocAddTableField("Key", nullptr);
					DocAddTableField("Col", "amount");
					DocAddTableField("Tok", "amount");
					DocAddTableField("ICR", nullptr);
				}

				{
					Utils::Vector<Nephrite::Trove> vec;
					vec.Prepare(g.m_Troves.m_iLastCreated);

					{
						Env::Key_T<Nephrite::Trove::Key> tk0, tk1;
						_POD_(tk0.m_Prefix.m_Cid) = m_Cid;
						_POD_(tk1.m_Prefix.m_Cid) = m_Cid;
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



					Env::DocArray gr3("value");
					for (auto iTrove = g.m_Troves.m_iHead; iTrove; )
					{
						const Nephrite::Trove& t = vec.m_p[iTrove - 1];

						Env::DocArray gr4("");

						Env::DocAddNum("", iTrove);
						Env::DocAddBlob_T("", t.m_pkOwner);
						Env::DocAddNum("", t.m_Amounts.Col);
						Env::DocAddNum("", t.m_Amounts.Tok);

						if (bHavePrice)
							DocAddPerc("", price.ToCR(t.m_Amounts.get_Rcr()));
						else
							Env::DocAddText("", "");

						iTrove = t.m_iNext;
					}
				}

			}

		}
	}

}

void ParserContext::WriteOracle2Settings(const Oracle2::Settings& stg)
{
	Env::DocAddNum("Validity Period", stg.m_hValidity);
	Env::DocAddNum("Min Providers", stg.m_MinProviders);
}

bool ParserContext::get_Oracle2Median(MultiPrecision::Float& ret, const ContractID& cid)
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

void ParserContext::On_Oracle2()
{
	OnName("Oracle2");

	if (m_Method)
	{
		switch (m_iMethod)
		{
		case Oracle2::Method::Create::s_iMethod:
			if (m_nArg >= sizeof(Oracle2::Method::Create))
			{
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Oracle2::Method::Create*>(m_pArg);

				WriteOracle2Settings(arg.m_Settings);
				WriteUpgradeSettings(arg.m_Upgradable);
			}
			break;

		case Oracle2::Method::Get::s_iMethod:
			if (m_nArg >= sizeof(Oracle2::Method::Get))
			{
				OnMethod("Get");
				GroupArgs gr;
			}
			break;
		
		case Oracle2::Method::FeedData::s_iMethod:
			if (m_nArg >= sizeof(Oracle2::Method::FeedData))
			{
				OnMethod("FeedData");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Oracle2::Method::FeedData*>(m_pArg);
				Env::DocAddNum("iProvider", arg.m_iProvider);
				DocAddFloat("Value", arg.m_Value);
			}
			break;


		}
	}

	if (m_State)
	{
		Env::DocGroup gr("State");

		WriteUpgrade3State();

		// TODO
	}

}


BEAM_EXPORT void Method_0(const ShaderID& sid, const ContractID& cid, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::DocGroup gr("");

	ParserContext pc(sid, cid);
	pc.m_Method = true;
	pc.m_iMethod = iMethod;
	pc.m_pArg = pArg;
	pc.m_nArg = nArg;

	pc.Parse();
}

BEAM_EXPORT void Method_1(const ShaderID& sid, const ContractID& cid)
{
	Env::DocGroup gr("");

	ParserContext pc(sid, cid);
	pc.Parse();
}

BEAM_EXPORT void Method_2(const ShaderID& sid, const ContractID& cid)
{
	Env::DocGroup gr("");

	ParserContext pc(sid, cid);
	pc.m_Name = false;
	pc.m_State = true;
	pc.Parse();
}
