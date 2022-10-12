#include "../common.h"
#include "../app_common_impl.h"
#include "../upgradable/contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable3/contract.h"
#include "../vault/contract.h"
#include "../vault_anon/contract.h"
#include "../faucet/contract.h"
#include "../dao-core/contract.h"
#include "../gallery/contract.h"
#include "../nephrite/contract.h"
#include "../oracle2/contract.h"
#include "../dao-vault/contract.h"
#include "../bans/contract.h"
#include "../dao-core/contract.h"
namespace Masternet {
#include "../dao-core-masternet/contract.h"
}
namespace Testnet {
#include "../dao-core-testnet/contract.h"
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

void DocAddTableHeader(const char* sz)
{
	Env::DocGroup gr("");
	DocSetType("th");
	Env::DocAddText("value", sz);
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

void DocAddAidAmount(const char* sz, AssetID aid, Amount amount)
{
	Env::DocArray gr(sz);
	DocAddAid("", aid);
	DocAddAmount("", amount);
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


#define HandleContractsStd(macro) \
	macro(Vault, Vault::s_SID) \
	macro(VaultAnon, VaultAnon::s_SID) \
	macro(Faucet, Faucet::s_SID) \

#define HandleContractsVer(macro) \
	macro(Oracle2, Oracle2::s_pSID) \
	macro(Nephrite, Nephrite::s_pSID) \
	macro(DaoVault, DaoVault::s_pSID) \
	macro(Bans, NameService::s_pSID) \

#define HandleContractsWrappers(macro) \
	macro(Upgradable, Upgradable::s_SID) \
	macro(Upgradable2, Upgradable2::s_SID) \

#define HandleContractsWrapped(macro) \
	macro(DaoCore, DaoCore::s_SID) \
	macro(DaoCore_Masternet, Masternet::DaoCore::s_SID) \
	macro(DaoCore_Testnet, Testnet::DaoCore::s_SID) \
	macro(Gallery_0, Gallery::s_pSID[0]) \
	macro(Gallery_1, Gallery::s_pSID[1]) \
	macro(Gallery_2, Gallery::s_pSID[2]) \

struct ParserContext
{
	const ShaderID& m_Sid;
	const ContractID& m_Cid;
	uint32_t m_iMethod;
	const void* m_pArg;
	uint32_t m_nArg;

	bool m_Method = false;
	bool m_State = false;

	template <typename T>
	const T* get_ArgsAs() const
	{
		return (m_nArg < sizeof(T)) ? nullptr : (const T*) m_pArg;
	}

	ParserContext(const ShaderID& sid, const ContractID& cid)
		:m_Sid(sid)
		,m_Cid(cid)
	{
	}

	static void OnName(const char* sz)
	{
		Env::DocAddText("kind", sz);
	}

#define VER_TXT " v"

	static void OnNameVer2(const char* sz, uint32_t iVer, char* szBuf, uint32_t nNameLen)
	{
		Env::Memcpy(szBuf, sz, nNameLen);
		Env::Memcpy(szBuf + nNameLen, VER_TXT, sizeof(VER_TXT) - 1);
		Utils::String::Decimal::Print(szBuf + nNameLen + _countof(VER_TXT) - 1, iVer);

		OnName(szBuf);
	}

	template <uint32_t nNameLen>
	static void OnNameVer(const char* sz, uint32_t iVer)
	{
		char szBuf[nNameLen + Utils::String::Decimal::DigitsMax<uint32_t>::N + _countof(VER_TXT)];
		OnNameVer2(sz, iVer, szBuf, nNameLen);
	}

	static void OnNameUpgradable(const char* sz, const ShaderID& sid)
	{
		Env::DocGroup gr("kind");
		if (sz)
			Env::DocAddText("Wrapper", sz);
		Env::DocAddBlob_T("subtype", sid);
	}

	void OnMethod(const char* sz)
	{
		assert(m_Method);
		Env::DocAddText("method", sz);
	}

	struct GroupArgs :public Env::DocGroup {
		GroupArgs() :Env::DocGroup("params") {}
	};

#define THE_MACRO(name, sid) \
	void OnMethod_##name(); \
	void OnState_##name();
	HandleContractsStd(THE_MACRO)
		HandleContractsWrapped(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(name, psid) \
	void OnMethod_##name(uint32_t iVer); \
	void OnState_##name(uint32_t iVer);
		HandleContractsVer(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(name, sid) void On_##name();
		HandleContractsWrappers(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(name, sid) \
	void On_##name() \
	{ \
		OnName(#name); \
		if (m_Method) \
			OnMethod_##name(); \
		if (m_State) \
		{ \
			Env::DocGroup gr("State"); \
			OnState_##name(); \
		} \
	}

		HandleContractsStd(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(name, psid) \
	void On_##name(uint32_t iVer) \
	{ \
		OnNameVer<_countof(#name) - 1>(#name, iVer); \
		if (m_Method) \
			OnMethod_##name(iVer); \
		if (m_State) \
		{ \
			Env::DocGroup gr("State"); \
			OnState_##name(iVer); \
		} \
	}

	HandleContractsVer(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(name, sid) \
	void On_##name() \
	{ \
		OnName("Impl-" #name); \
		if (m_Method) \
			OnMethod_##name(); \
	}

	HandleContractsWrapped(THE_MACRO)
#undef THE_MACRO

	bool Parse();

	struct Wrapped
	{
		enum Enum {
#define THE_MACRO(name, sid) name,
			HandleContractsWrapped(THE_MACRO)
#undef THE_MACRO
			count
		};

		static Enum Recognize(const ShaderID&);
	};

	void get_WrappedState(Wrapped::Enum);

	static void WriteUpgradeParams(const Upgradable::Next&);
	static void WriteUpgradeParams(const Upgradable2::Next&);
	void WriteUpgradeParams(const Upgradable3::NextVersion&, uint32_t nSizeShader);
	static void WriteUpgradeParams(const ContractID&, Height);
	void WriteUpgradeParams(Height, const ShaderID&);
	static void WriteUpgradeSettings(const Upgradable2::Settings&);
	static void WriteUpgradeSettings(const Upgradable3::Settings&);
	static void WriteUpgradeSettingsInternal(const Upgradable3::Settings&);
	void WriteUpgrade3State();
	void OnUpgrade3Method();
	static void WriteUpgradeAdminsMask(uint32_t nApproveMask);
	static void WriteNephriteSettings(const Nephrite::Settings&);
	static void WriteOracle2Settings(const Oracle2::Settings&);
	static bool get_Oracle2Median(MultiPrecision::Float&, const ContractID& cid);
	static void WriteBansSettings(const NameService::Settings&);
	template <typename T>
	void DocSetBansName_T(const T& x) {
		DocSetBansNameEx(&x + 1, x.m_NameLen);
	}
	void DocSetBansNameEx(const void* p, uint32_t nLen);
};

bool ParserContext::Parse()
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

#define THE_MACRO(name, sid) \
	if (_POD_(m_Sid) == sid) \
	{ \
		On_##name(); \
		return true; \
	}

	HandleContractsStd(THE_MACRO)
	HandleContractsWrappers(THE_MACRO)
	HandleContractsWrapped(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(name, psid) \
	for (uint32_t i = 0; i < _countof(psid); i++) \
		if (_POD_(m_Sid) == psid[i]) \
		{ \
			On_##name(i); \
			return true; \
		}

	HandleContractsVer(THE_MACRO)
#undef THE_MACRO

	return false;
}

ParserContext::Wrapped::Enum ParserContext::Wrapped::Recognize(const ShaderID& sidArg)
{
#define THE_MACRO(name, sid) \
	if (_POD_(sidArg) == sid) \
	{ \
		OnName(#name); \
		return Wrapped::name; \
	}

	HandleContractsWrapped(THE_MACRO)
#undef THE_MACRO

	return Wrapped::count;
}

void ParserContext::get_WrappedState(Wrapped::Enum e)
{
	switch (e)
	{
#define THE_MACRO(name, sid) \
	case Wrapped::name: \
		OnState_##name(); \
		return;

		HandleContractsWrapped(THE_MACRO)
#undef THE_MACRO

	default:
		return; // suppress warning
	}
}


void ParserContext::On_Upgradable()
{
	// Get state, discover which cid actually operates the contract
	Upgradable::State us;

	if (m_Method && !m_iMethod)
	{
		// c'tor, the state doesn't exist yet. Initial cid should be in the args
		auto pArg = get_ArgsAs<Upgradable::Create>();
		if (!pArg)
			return;

		_POD_(Cast::Down<Upgradable::Current>(us)) = *pArg;
		_POD_(Cast::Down<Upgradable::Next>(us)).SetZero();

		GroupArgs gr;
		DocAddPk("owner", us.m_Pk);
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

	auto eType = Wrapped::Recognize(sid);
	if (Wrapped::count == eType)
		OnNameUpgradable("upgradable", sid);

	if (m_State)
	{
		Env::DocGroup gr("State");

		get_WrappedState(eType);

		{
			Env::DocGroup gr("upgradable");
			DocAddPk("owner", us.m_Pk);
			WriteUpgradeParams(us);
		}

	}

	if (m_Method)
	{
		switch (m_iMethod)
		{
		case Upgradable::ScheduleUpgrade::s_iMethod:
			{
				auto pArg = get_ArgsAs<Upgradable::ScheduleUpgrade>();
				if (!pArg)
					return; // don't care of partial result

				OnMethod("Schedule upgrade");

				GroupArgs gr;
				WriteUpgradeParams(*pArg);
			}
			break;

		default:
			OnMethod("Passthrough");
		}
	}
}

void ParserContext::On_Upgradable2()
{
	// Get state, discover which cid actually operates the contract

	Upgradable2::State us;
	Upgradable2::Settings stg;
	if (m_Method && !m_iMethod)
	{
		// c'tor, the state doesn't exist yet. Initial cid should be in the args
		auto pArg = get_ArgsAs<Upgradable2::Create>();
		if (!pArg)
			return;

		_POD_(us.m_Active) = pArg->m_Active;
		_POD_(us.m_Next).SetZero();
		_POD_(stg) = pArg->m_Settings;

		GroupArgs gr;
		WriteUpgradeSettings(stg);
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

	auto eType = Wrapped::Recognize(sid);
	if (Wrapped::count == eType)
		OnNameUpgradable("upgradable2", sid);

	if (m_State)
	{
		Env::DocGroup gr("State");

		get_WrappedState(eType);

		{
			Env::DocGroup gr("upgradable2");
			WriteUpgradeSettings(stg);
			WriteUpgradeParams(us.m_Next);
		}

	}

	if (m_Method)
	{
		if (Upgradable2::Control::s_iMethod != m_iMethod)
		{
			OnMethod("Passthrough");
			return;
		}

		auto pCtl = get_ArgsAs<Upgradable2::Control::Base>();
		if (!pCtl)
			return; // don't care of partial result

		switch (pCtl->m_Type)
		{
		case Upgradable2::Control::ExplicitUpgrade::s_Type:
			OnMethod("explicit upgrade");
			break;

		case Upgradable2::Control::ScheduleUpgrade::s_Type:
			{
				auto pArg = get_ArgsAs<Upgradable2::Control::ScheduleUpgrade>();
				if (pArg)
				{
					OnMethod("Schedule upgrade");

					GroupArgs gr;

					WriteUpgradeAdminsMask(pArg->m_ApproveMask);
					WriteUpgradeParams(pArg->m_Next);
				}
			}
			break;

		case Upgradable2::Control::ReplaceAdmin::s_Type:
			{
				auto pArg = get_ArgsAs<Upgradable2::Control::ReplaceAdmin>();
				if (pArg)
				{
					OnMethod("replace admin");

					GroupArgs gr;

					WriteUpgradeAdminsMask(pArg->m_ApproveMask);

					Env::DocAddNum("iAdmin", pArg->m_iAdmin);
					DocAddPk("pk", pArg->m_Pk);
				}
			}
			break;

		case Upgradable2::Control::SetApprovers::s_Type:
			{
				auto pArg = get_ArgsAs<Upgradable2::Control::SetApprovers>();
				if (pArg)
				{
					OnMethod("set min approvers");

					GroupArgs gr;

					WriteUpgradeAdminsMask(pArg->m_ApproveMask);

					Env::DocAddNum("num", pArg->m_NewVal);
				}
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
	//const uint32_t nDigs = Utils::String::Hex::DigitsMax<uint32_t>::N;
	//char szBuf[nDigs + 1];
	//Utils::String::Hex::Print(szBuf, nApproveMask, nDigs);

	//Env::DocAddText("approve-mask", szBuf);

	Env::DocArray gr("Approvers");

	for (uint32_t i = 0; i < (sizeof(nApproveMask) << 3); i++)
	{
		uint32_t msk = 1u << i;
		if (!(nApproveMask & msk))
			continue;

		Env::DocAddNum("", i + 1);
	}
}

void ParserContext::WriteUpgradeParams(const ContractID& cid, Height h)
{
	if (!_POD_(cid).IsZero())
	{
		ShaderID sid;
		if (!Utils::get_ShaderID_FromContract(sid, cid))
			return;

		ParserContext pc2(sid, sid); // use sid as dummy cid. It won't be used anyway
		pc2.WriteUpgradeParams(h, sid);
	}
}

void ParserContext::WriteUpgradeParams(Height h, const ShaderID& sid)
{
	Env::DocGroup gr("Next upgrade");

	DocAddHeight("Height", h);

	ParserContext pc2(sid, m_Cid);
	if (!pc2.Parse())
		pc2.OnNameUpgradable(nullptr, sid);
}

void ParserContext::WriteUpgradeParams(const Upgradable3::NextVersion& x, uint32_t nSizeShader)
{
	ShaderID sid;
	Utils::get_ShaderID(sid, &x + 1, nSizeShader);

	WriteUpgradeParams(x.m_hTarget, sid);
}

void ParserContext::WriteUpgradeSettings(const Upgradable2::Settings& stg)
{
	WriteUpgradeSettingsInternal(Cast::Reinterpret<Upgradable3::Settings>(stg));
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
		Env::DocArray gr2("value");

		{
			Env::DocArray gr3("");
			DocAddTableHeader("Index");
			DocAddTableHeader("Key");
		}

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

			WriteUpgradeParams(*pVal, nVal - sizeof(Upgradable3::NextVersion));

			Env::Heap_Free(pVal);
		}
	}
}

void ParserContext::OnUpgrade3Method()
{
	using namespace Upgradable3;

	assert(m_Method == (Method::Control::s_iMethod == m_iMethod));

	auto pCtl = get_ArgsAs<Method::Control::Base>();
	if (!pCtl)
		return;

	switch (pCtl->m_Type)
	{
	case Method::Control::ExplicitUpgrade::s_Type:
		OnMethod("explicit upgrade");
		break;

	case Method::Control::OnUpgraded::s_Type:
		{
			auto pArg = get_ArgsAs<Method::Control::OnUpgraded>();
			if (pArg)
			{
				OnMethod("On Upgraded");
				GroupArgs gr;

				Env::DocAddNum("Prev version", pArg->m_PrevVersion);
			}
		}
		break;

	case Method::Control::ScheduleUpgrade::s_Type:
		{
			auto pArg = get_ArgsAs<Method::Control::ScheduleUpgrade>();
			if (pArg)
			{
				OnMethod("Schedule upgrade");
				GroupArgs gr;

				WriteUpgradeAdminsMask(pArg->m_ApproveMask);

				if (pArg->m_SizeShader <= m_nArg - sizeof(*pArg))
					WriteUpgradeParams(pArg->m_Next, pArg->m_SizeShader);
			}
		}
		break;

	case Method::Control::ReplaceAdmin::s_Type:
		{
			auto pArg = get_ArgsAs<Method::Control::ReplaceAdmin>();
			if (pArg)
			{
				OnMethod("replace admin");
				GroupArgs gr;

				WriteUpgradeAdminsMask(pArg->m_ApproveMask);

				Env::DocAddNum("iAdmin", pArg->m_iAdmin);
				DocAddPk("pk", pArg->m_Pk);
			}
		}
		break;

	case Method::Control::SetApprovers::s_Type:
		{
			auto pArg = get_ArgsAs<Upgradable2::Control::SetApprovers>();
			if (pArg)
			{
				OnMethod("set min approvers");
				GroupArgs gr;

				WriteUpgradeAdminsMask(pArg->m_ApproveMask);
				Env::DocAddNum("num", pArg->m_NewVal);
			}
		}
		break;
	}

}

void ParserContext::OnMethod_Vault()
{
	switch (m_iMethod)
	{
	case Vault::Deposit::s_iMethod:
		{
			auto pArg = get_ArgsAs<Vault::Deposit>();
			if (pArg)
			{
				OnMethod("Deposit");
				GroupArgs gr;
				DocAddPk("User", pArg->m_Account);
			}
		}
		break;

	case Vault::Withdraw::s_iMethod:
		OnMethod("Withdraw");
		// no need to include the account, it's visible in the sigs list
		break;
	}
}

void ParserContext::OnState_Vault()
{
}

void ParserContext::OnMethod_VaultAnon()
{
	switch (m_iMethod)
	{
	case VaultAnon::Method::Deposit::s_iMethod:
	{
		auto pArg = get_ArgsAs<VaultAnon::Method::Deposit>();
		if (pArg)
		{
			OnMethod("Deposit");
			GroupArgs gr;

			DocAddPk("User", pArg->m_Key.m_pkOwner);
		}
	}
	break;

	case VaultAnon::Method::Withdraw::s_iMethod:
		OnMethod("Withdraw");
		// no need to include the account, it's visible in the sigs list
		break;
	}
}

void ParserContext::OnState_VaultAnon()
{
}

void ParserContext::OnMethod_Faucet()
{
	switch (m_iMethod)
	{
	case 0:
		{
			auto pArg = get_ArgsAs<Faucet::Params>();
			if (pArg)
			{
				GroupArgs gr;

				Env::DocAddNum("Backlog period", pArg->m_BacklogPeriod);
				DocAddAmount("Max withdraw", pArg->m_MaxWithdraw);
			}
		}
		break;

	case Faucet::Deposit::s_iMethod: OnMethod("deposit"); break;
	case Faucet::Withdraw::s_iMethod: OnMethod("withdraw"); break;
	}
}

void ParserContext::OnState_Faucet()
{
}

void ParserContext::OnMethod_DaoCore()
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
}

void ParserContext::OnMethod_DaoCore_Masternet() {
	OnMethod_DaoCore();
}
void ParserContext::OnMethod_DaoCore_Testnet() {
	OnMethod_DaoCore();
}


void ParserContext::OnState_DaoCore()
{
}

void ParserContext::OnState_DaoCore_Masternet() {
	OnState_DaoCore();
}
void ParserContext::OnState_DaoCore_Testnet() {
	OnState_DaoCore();
}

void WriteGalleryAdrID(Gallery::Masterpiece::ID id)
{
	Env::DocAddNum("art_id", Utils::FromBE(id));
}

void WriteGalleryPrice(const Gallery::AmountWithAsset& x)
{
	DocAddAidAmount("Price", x.m_Aid, x.m_Amount);
}

void ParserContext::OnMethod_Gallery_0()
{
	On_Gallery_2(); // same, we only added methods
}

void ParserContext::OnMethod_Gallery_1()
{
	On_Gallery_2();
}

void ParserContext::OnMethod_Gallery_2()
{
	switch (m_iMethod)
	{
	case Gallery::Method::AddExhibit::s_iMethod:
		{
			auto pArg = get_ArgsAs<Gallery::Method::AddExhibit>();
			if (pArg)
			{
				OnMethod("AddExhibit");
				GroupArgs gr;

				DocAddPk("pkUser", pArg->m_pkArtist);
				Env::DocAddNum("size", pArg->m_Size);
			}
		}
		break;

	case Gallery::Method::ManageArtist::s_iMethod:
		{
			auto pArg = get_ArgsAs<Gallery::Method::ManageArtist>();
			if (pArg)
			{
				OnMethod("ManageArtist");
				GroupArgs gr;

				DocAddPk("pkUser", pArg->m_pkArtist);
				DocAddTextLen<Gallery::Artist::s_LabelMaxLen>("name", pArg + 1, pArg->m_LabelLen);
			}
		}
		break;
		
		
	case Gallery::Method::SetPrice::s_iMethod:
		{
			auto pArg = get_ArgsAs<Gallery::Method::SetPrice>();
			if (pArg)
			{
				OnMethod("SetPrice");
				GroupArgs gr;

				WriteGalleryAdrID(pArg->m_ID);
				WriteGalleryPrice(pArg->m_Price);
			}
		}
		break;

	case Gallery::Method::Buy::s_iMethod:
		{
			auto pArg = get_ArgsAs<Gallery::Method::Buy>();
			if (pArg)
			{
				OnMethod("Buy");
				GroupArgs gr;

				WriteGalleryAdrID(pArg->m_ID);

				DocAddPk("pkUser", pArg->m_pkUser);
				Env::DocAddNum32("hasAid", pArg->m_HasAid);
				DocAddAmount("payMax", pArg->m_PayMax);
			}
		}
		break;
		
	case Gallery::Method::Transfer::s_iMethod:
		{
			auto pArg = get_ArgsAs<Gallery::Method::Transfer>();
			if (pArg)
			{
				OnMethod("Transfer");
				GroupArgs gr;

				WriteGalleryAdrID(pArg->m_ID);
				DocAddPk("newPkUser", pArg->m_pkNewOwner);
			}
		}
		break;
		
			
	case Gallery::Method::Withdraw::s_iMethod:
		{
			auto pArg = get_ArgsAs<Gallery::Method::Withdraw>();
			if (pArg)
			{
				OnMethod("Withdraw");
				GroupArgs gr;

				// TODO roman
				Env::DocAddBlob_T("key", pArg->m_Key);
				DocAddAidAmount("Value", pArg->m_Key.m_Aid, pArg->m_Value);
			}
		}
		break;
		
	case Gallery::Method::AddVoteRewards::s_iMethod:
		{
			auto pArg = get_ArgsAs<Gallery::Method::AddVoteRewards>();
			if (pArg)
			{
				OnMethod("AddVoteRewards");
				GroupArgs gr;

				DocAddAmount("amount", pArg->m_Amount);
			}
		}
		break;
		
	case Gallery::Method::Vote::s_iMethod:
		{
			auto pArg = get_ArgsAs<Gallery::Method::Vote>();
			if (pArg)
			{
				OnMethod("Vote");
				GroupArgs gr;

				WriteGalleryAdrID(pArg->m_ID.m_MasterpieceID);
				Env::DocAddNum("impression", pArg->m_Impression.m_Value);
			}
		}
		break;

	case Gallery::Method::AdminDelete::s_iMethod:
		{
			auto pArg = get_ArgsAs<Gallery::Method::AdminDelete>();
			if (pArg)
			{
				OnMethod("AdminDelete");
				GroupArgs gr;

				WriteGalleryAdrID(pArg->m_ID);
			}
		}
		break;
	}
}

void ParserContext::OnState_Gallery_0()
{
	OnState_Gallery_2();
}
void ParserContext::OnState_Gallery_1()
{
	OnState_Gallery_2();
}
void ParserContext::OnState_Gallery_2()
{
	OnState_Gallery_2();
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

void ParserContext::OnMethod_Nephrite(uint32_t /* iVer */)
{
	switch (m_iMethod)
	{
	case Nephrite::Method::Create::s_iMethod:
		{
			auto pArg = get_ArgsAs<Nephrite::Method::Create>();
			if (pArg)
			{
				GroupArgs gr;

				WriteNephriteSettings(pArg->m_Settings);
				WriteUpgradeSettings(pArg->m_Upgradable);
			}
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		OnUpgrade3Method();
		break;

	case Nephrite::Method::TroveOpen::s_iMethod:
		{
			auto pArg = get_ArgsAs<Nephrite::Method::TroveOpen>();
			if (pArg)
			{
				OnMethod("Trove Open");
				GroupArgs gr;

				DocAddPk("User", pArg->m_pkUser);
				DocAddAmount("Col", pArg->m_Amounts.Col);
				DocAddAmount("Tok", pArg->m_Amounts.Tok);
			}
		}
		break;
		
	case Nephrite::Method::TroveClose::s_iMethod:
		{
			auto pArg = get_ArgsAs<Nephrite::Method::TroveClose>();
			if (pArg)
			{
				OnMethod("Trove Close");
				GroupArgs gr;
			}
		}
		break;

	case Nephrite::Method::TroveModify::s_iMethod:
		{
			auto pArg = get_ArgsAs<Nephrite::Method::TroveModify>();
			if (pArg)
			{
				OnMethod("Trove Modify");
				GroupArgs gr;

				DocAddAmount("Col", pArg->m_Amounts.Col);
				DocAddAmount("Tok", pArg->m_Amounts.Tok);
			}
		}
		break;

	case Nephrite::Method::FundsAccess::s_iMethod:
		{
			auto pArg = get_ArgsAs<Nephrite::Method::FundsAccess>();
			if (pArg)
			{
				OnMethod("Funds Access");
				GroupArgs gr;
			}
		}
		break;

	case Nephrite::Method::UpdStabPool::s_iMethod:
		{
			auto pArg = get_ArgsAs<Nephrite::Method::UpdStabPool>();
			if (pArg)
			{
				OnMethod("Stability Pool update");
				GroupArgs gr;

				DocAddAmount("New Amount", pArg->m_NewAmount);
			}
		}
		break;

	case Nephrite::Method::Liquidate::s_iMethod:
		{
			auto pArg = get_ArgsAs<Nephrite::Method::Liquidate>();
			if (pArg)
			{
				OnMethod("Liquidate troves");
				GroupArgs gr;

				DocAddAmount("Count", pArg->m_Count);
			}
		}
		break;

	case Nephrite::Method::Redeem::s_iMethod:
		{
			auto pArg = get_ArgsAs<Nephrite::Method::Redeem>();
			if (pArg)
			{
				OnMethod("Redeem");
				GroupArgs gr;

				DocAddAmount("Amount", pArg->m_Amount);
			}
		}
		break;

	case Nephrite::Method::AddStabPoolReward::s_iMethod:
		{
			auto pArg = get_ArgsAs<Nephrite::Method::AddStabPoolReward>();
			if (pArg)
			{
				OnMethod("Add Stability Pool Reward");
				GroupArgs gr;

				DocAddAmount("Amount", pArg->m_Amount);
			}
		}
		break;
	}
}

void ParserContext::OnState_Nephrite(uint32_t /* iVer */)
{
	WriteUpgrade3State();

	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = m_Cid;
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

		for (auto iTrove = g.m_Troves.m_iHead; iTrove; )
		{
			Nephrite::Trove& t = vec.m_p[iTrove - 1];
			g.m_RedistPool.Remove(t);

			Env::DocArray gr4("");

			Env::DocAddNum("", iTrove);
			DocAddPk("", t.m_pkOwner);
			DocAddAmount("", t.m_Amounts.Col);
			DocAddAmount("", t.m_Amounts.Tok);

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

void ParserContext::OnMethod_Oracle2(uint32_t /* iVer */)
{
	switch (m_iMethod)
	{
	case Oracle2::Method::Create::s_iMethod:
		{
			auto pArg = get_ArgsAs<Oracle2::Method::Create>();
			if (pArg)
			{
				GroupArgs gr;

				WriteOracle2Settings(pArg->m_Settings);
				WriteUpgradeSettings(pArg->m_Upgradable);
			}
		}
		break;

	case Oracle2::Method::Get::s_iMethod:
		{
			auto pArg = get_ArgsAs<Oracle2::Method::Get>();
			if (pArg)
			{
				OnMethod("Get");

				GroupArgs gr;

				MultiPrecision::Float val;
				if (get_Oracle2Median(val, m_Cid))
					DocAddFloat("Result", val);
				else
					Env::DocAddText("Result", "NaN");
			}
		}
		break;
		
	case Oracle2::Method::FeedData::s_iMethod:
		{
			auto pArg = get_ArgsAs<Oracle2::Method::FeedData>();
			if (pArg)
			{
				OnMethod("FeedData");
				GroupArgs gr;

				Env::DocAddNum("iProvider", pArg->m_iProvider);
				DocAddFloat("Value", pArg->m_Value);
			}
		}
		break;

	case Oracle2::Method::SetSettings::s_iMethod:
		{
			auto pArg = get_ArgsAs<Oracle2::Method::SetSettings>();
			if (pArg)
			{
				OnMethod("SetSettings");
				GroupArgs gr;

				WriteOracle2Settings(pArg->m_Settings);
				WriteUpgradeAdminsMask(pArg->m_ApproveMask);
			}
		}
		break;

	case Oracle2::Method::ProviderAdd::s_iMethod:
		{
			auto pArg = get_ArgsAs<Oracle2::Method::ProviderAdd>();
			if (pArg)
			{
				OnMethod("ProviderAdd");
				GroupArgs gr;

				DocAddPk("pk", pArg->m_pk);
				WriteUpgradeAdminsMask(pArg->m_ApproveMask);
			}
		}
		break;

	case Oracle2::Method::ProviderDel::s_iMethod:
		{
			auto pArg = get_ArgsAs<Oracle2::Method::ProviderDel>();
			if (pArg)
			{
				OnMethod("ProviderDel");
				GroupArgs gr;

				Env::DocAddNum("iProvider", pArg->m_iProvider);
				WriteUpgradeAdminsMask(pArg->m_ApproveMask);
			}
		}
		break;

	}
}

void ParserContext::OnState_Oracle2(uint32_t /* iVer */)
{
	Oracle2::StateMax g;
	uint32_t nProvs = 0;

	Env::Key_T<uint8_t> key;
	_POD_(key.m_Prefix.m_Cid) = m_Cid;
	key.m_KeyInContract = Oracle2::Tags::s_StateFull;

	{
		Env::VarReader r(key, key);
		uint32_t nKey = 0, nVal = sizeof(g);
		if (!r.MoveNext(nullptr, nKey, &g, nVal, 0))
			return;

		nProvs = (nVal - sizeof(Oracle2::State0)) / sizeof(Oracle2::State0::Entry); // don't care about overflow
		if (nProvs > g.s_ProvsMax)
			return;
	}

	{
		Env::DocGroup gr1("Settings");

		WriteUpgrade3State();
		WriteOracle2Settings(g.m_Settings);
	}

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
		}

		for (uint32_t i = 0; i < nProvs; i++)
		{
			const auto& x = g.m_pE[i];
			if (_POD_(x.m_Pk).IsZero())
				continue;

			Env::DocArray gr3("");

			Env::DocAddNum("", i);
			DocAddPk("", x.m_Pk);
			DocAddFloat("", x.m_Val);
			DocAddHeight("", x.m_hUpdated);
		}
	}

	{
		key.m_KeyInContract = Oracle2::Tags::s_Median;
		Oracle2::Median med;
		if (Env::VarReader::Read_T(key, med))
		{
			bool bValid = (med.m_hEnd >= Env::get_Height());
			if (bValid)
				DocAddFloat("Median", med.m_Res);
			else
				Env::DocAddText("Median", "");
		}
	}
}

void ParserContext::OnMethod_DaoVault(uint32_t /* iVer */)
{
	switch (m_iMethod)
	{
	case DaoVault::Method::Create::s_iMethod:
		{
			auto pArg = get_ArgsAs<DaoVault::Method::Create>();
			if (pArg)
			{
				GroupArgs gr;

				WriteUpgradeSettings(pArg->m_Upgradable);
			}
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		OnUpgrade3Method();
		break;

	case DaoVault::Method::Deposit::s_iMethod:
		{
			auto pArg = get_ArgsAs<DaoVault::Method::Deposit>();
			if (pArg)
			{
				OnMethod("Deposit");
				//GroupArgs gr;

				//DocAddAidAmount("Value", pArg->m_Aid, pArg->m_Amount);
			}
		}
		break;
		
	case DaoVault::Method::Withdraw::s_iMethod:
		{
			auto pArg = get_ArgsAs<DaoVault::Method::Withdraw>();
			if (pArg)
			{
				OnMethod("Withdraw");
				//GroupArgs gr;

				//DocAddAidAmount("Value", pArg->m_Aid, pArg->m_Amount);
				WriteUpgradeAdminsMask(pArg->m_ApproveMask);
			}
		}
		break;
	}
}

void ParserContext::OnState_DaoVault(uint32_t /* iVer */)
{
	WriteUpgrade3State();
}

void ParserContext::WriteBansSettings(const NameService::Settings& stg)
{
	DocAddCid("Price oracle", stg.m_cidOracle);
	DocAddCid("Dao-Vault", stg.m_cidDaoVault);
	DocAddCid("Anon-Vault", stg.m_cidVault);
	DocAddHeight("Activation height", stg.m_h0);
}

void ParserContext::DocSetBansNameEx(const void* p, uint32_t nLen)
{
	if (nLen > NameService::Domain::s_MaxLen)
		return;

	uint32_t n0 = ((const uint8_t*) p) - ((const uint8_t*) m_pArg);
	if (n0 + nLen > m_nArg)
		return;

	char sz[NameService::Domain::s_MaxLen + 1];
	Env::Memcpy(sz, p, nLen);
	sz[nLen] = 0;

	Env::DocAddText("name", sz);
}

void ParserContext::OnMethod_Bans(uint32_t /* iVer */)
{
	switch (m_iMethod)
	{
	case NameService::Method::Create::s_iMethod:
		{
			auto pArg = get_ArgsAs<NameService::Method::Create>();
			if (pArg)
			{
				GroupArgs gr;

				WriteBansSettings(pArg->m_Settings);
				WriteUpgradeSettings(pArg->m_Upgradable);
			}
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		OnUpgrade3Method();
		break;

	case NameService::Method::SetOwner::s_iMethod:
		{
			auto pArg = get_ArgsAs<NameService::Method::SetOwner>();
			if (pArg)
			{
				OnMethod("Set Owner");
				GroupArgs gr;

				DocSetBansName_T(*pArg);
				DocAddPk("New owner", pArg->m_pkNewOwner);
			}
		}
		break;
		
	case NameService::Method::Extend::s_iMethod:
		{
			auto pArg = get_ArgsAs<NameService::Method::Extend>();
			if (pArg)
			{
				OnMethod("Extend period");
				GroupArgs gr;

				DocSetBansName_T(*pArg);
				Env::DocAddNum32("Periods", pArg->m_Periods);
			}
		}
		break;

	case NameService::Method::SetPrice::s_iMethod:
		{
			auto pArg = get_ArgsAs<NameService::Method::SetPrice>();
			if (pArg)
			{
				OnMethod(pArg->m_Price.m_Amount ? "Set Price" : "Remove Price");
				GroupArgs gr;

				DocSetBansName_T(*pArg);

				if (pArg->m_Price.m_Amount)
					DocAddAidAmount("Price", pArg->m_Price.m_Aid, pArg->m_Price.m_Amount);
			}
		}
		break;

	case NameService::Method::Buy::s_iMethod:
		{
			auto pArg = get_ArgsAs<NameService::Method::Buy>();
			if (pArg)
			{
				OnMethod("Buy");
				GroupArgs gr;

				DocSetBansName_T(*pArg);
				DocAddPk("New owner", pArg->m_pkNewOwner);

			}
		}
		break;

	case NameService::Method::Register::s_iMethod:
		{
			auto pArg = get_ArgsAs<NameService::Method::Register>();
			if (pArg)
			{
				OnMethod("Register");
				GroupArgs gr;

				DocSetBansName_T(*pArg);
				DocAddPk("Owner", pArg->m_pkOwner);
				Env::DocAddNum32("Periods", pArg->m_Periods);
			}
		}
		break;

	}
}

void ParserContext::OnState_Bans(uint32_t /* iVer */)
{
	WriteUpgrade3State();

	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = m_Cid;
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
		_POD_(k0.m_Prefix.m_Cid) = m_Cid;
		_POD_(k0.m_KeyInContract.m_sz).SetZero();

#pragma pack (push, 1)
		struct KeyPlus {
			Env::Key_T<NameService::Domain::KeyMax> k;
			char m_chTerm; // 1 more byte, to place 0-terminator
		} k1;
#pragma pack (pop)

		_POD_(k1.k.m_Prefix.m_Cid) = m_Cid;
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
	//pc.m_Name = false;
	pc.m_State = true;
	pc.Parse();
}
