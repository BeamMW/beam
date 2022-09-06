#include "../common.h"
#include "../app_common_impl.h"
#include "../upgradable/contract.h"
#include "../upgradable2/contract.h"
#include "../vault/contract.h"
#include "../faucet/contract.h"
#include "../dao-core/contract.h"
#include "../gallery/contract.h"


#define HandleContractsAll(macro) \
	macro(Upgradable, Upgradable::s_SID) \
	macro(Upgradable2, Upgradable2::s_SID) \
	macro(Vault, Vault::s_SID) \
	macro(Faucet, Faucet::s_SID) \
	macro(DaoCore, DaoCore::s_SID) \
	macro(Gallery_0, Gallery::s_pSID[0]) \
	macro(Gallery_1, Gallery::s_pSID[1]) \
	macro(Gallery_2, Gallery::s_pSID[2])


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

	void OnUpgradableSubtype(const ShaderID& sid) { Env::DocAddBlob_T("subtype", sid); }

#define THE_MACRO(name, sid) void On_##name();
	HandleContractsAll(THE_MACRO)
#undef THE_MACRO

	bool Parse();

	void WriteUpgradeParams(const Upgradable::Next&);
	void WriteUpgradeParams(const Upgradable2::Next&);
	void WriteUpgradeParams(const ContractID&, Height);
	void WriteUpgradeAdminsMask(uint32_t nApproveMask);
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
			Env::DocAddBlob_T("owner", us.m_Pk);
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
			Env::DocAddNum("Num approvers ", stg.m_MinApprovers);
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

				Env::DocAddBlob_T("iAdmin", arg.m_iAdmin);
				Env::DocAddBlob_T("pk", arg.m_Pk);
			}
			break;

		case Upgradable2::Control::SetApprovers::s_Type:
			if (m_nArg >= sizeof(Upgradable2::Control::SetApprovers))
			{
				OnMethod("set min approvers");

				GroupArgs gr;
				const auto& arg = Cast::Up<Upgradable2::Control::SetApprovers>(ctl);

				WriteUpgradeAdminsMask(arg.m_ApproveMask);

				Env::DocAddBlob_T("num", arg.m_NewVal);
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

		Env::DocAddNum("height", h);

		ParserContext pc2(sid, cid);
		if (!pc2.Parse())
			OnUpgradableSubtype(sid);
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
				Env::DocAddBlob_T("User", arg.m_Account);
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
				Env::DocAddNum("Max withdraw", pars.m_MaxWithdraw);
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
		Env::DocAddNum("aid", x.m_Aid);
	Env::DocAddNum("amount", x.m_Amount);
}

template <uint32_t nMaxLen>
void DocAddTextLen(const char* szID, const void* szValue, uint32_t nLen)
{
	char szBuf[nMaxLen + 1];
	nLen = std::min(nLen, nMaxLen);

	Env::Memcpy(szBuf, szValue, nLen);
	szBuf[nLen] = 0;

	Env::DocAddText(szID, szBuf);
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
				Env::DocAddBlob_T("pkUser", arg.m_pkArtist);
				Env::DocAddNum("size", arg.m_Size);
			}
			break;

		case Gallery::Method::ManageArtist::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::ManageArtist))
			{
				OnMethod("ManageArtist");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::ManageArtist*>(m_pArg);
				Env::DocAddBlob_T("pkUser", arg.m_pkArtist);
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
				
	            Env::DocAddBlob_T("pkUser", arg.m_pkUser);
	            Env::DocAddNum32("hasAid", arg.m_HasAid);
				Env::DocAddNum64("payMax", arg.m_PayMax);
			}
			break;
		
		case Gallery::Method::Transfer::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::Transfer))
			{
				OnMethod("Transfer");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::Transfer*>(m_pArg);

				WriteGalleryAdrID(arg.m_ID);
			
	            Env::DocAddBlob_T("newPkUser", arg.m_pkNewOwner);
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
	            Env::DocAddNum64("value", arg.m_Value);
			}
			break;
		
		case Gallery::Method::AddVoteRewards::s_iMethod:
			if (m_nArg >= sizeof(Gallery::Method::AddVoteRewards))
			{
				OnMethod("AddVoteRewards");
				GroupArgs gr;

				const auto& arg = *reinterpret_cast<const Gallery::Method::AddVoteRewards*>(m_pArg);

	            Env::DocAddNum("amount", arg.m_Amount);
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
