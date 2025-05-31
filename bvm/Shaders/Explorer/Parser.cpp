#include "../common.h"
#include "../app_common_impl.h"
#include "../upgradable/contract.h"
#include "../upgradable2/contract.h"
#include "../upgradable3/contract.h"
#include "../vault/contract.h"
#include "../vault_anon/contract.h"
#include "../faucet/contract.h"
#include "../faucet2/contract.h"
#include "../dao-core/contract.h"
#include "../gallery/contract.h"
#include "../nephrite/contract.h"
#include "../oracle2/contract.h"
#include "../dao-vault/contract.h"
#include "../bans/contract.h"
#include "../amm/contract.h"
#include "../dao-core/contract.h"
#include "../dao-core2/contract.h"
#include "../dao-accumulator/contract.h"
#include "../dao-vote/contract.h"
namespace Masternet {
#include "../dao-core-masternet/contract.h"
}
namespace Testnet {
#include "../dao-core-testnet/contract.h"
}
#include "../minter/contract.h"
#include "../blackhole/contract.h"
#include "../l2tst1/contract_l1.h"
#include "../l2tst1/contract_l2.h"

template <uint32_t nMaxLen>
void DocAddTextLen(const char* szID, const void* szValue, uint32_t nLen)
{
	char szBuf[nMaxLen + 1];
	nLen = std::min(nLen, nMaxLen);

	Env::Memcpy(szBuf, szValue, nLen);
	szBuf[nLen] = 0;

	Env::DocAddText(szID, szBuf);
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

void DocAddHeight(const char* sz, Height h)
{
	Env::DocGroup gr(sz);
	DocSetType("height");
	Env::DocAddNum("value", h);
}

void DocAddHeight1(const char* sz, Height h)
{
	DocAddHeight(sz, h + 1);
}

template <typename T>
void DocAddMonoblob(const char* sz, const T& x)
{
	Env::DocGroup gr(sz);
	DocSetType("blob");
	Env::DocAddBlob_T("value", x);
}

void DocAddPk(const char* sz, const PubKey& pk)
{
	DocAddMonoblob(sz, pk);
}


void DocAddAmountBig(const char* sz, Amount valLo, Amount valHi)
{
	if (valHi)
	{
		MultiPrecision::UInt<4> val;
		val.Set<2>(valHi);
		val += MultiPrecision::UInt<2>(valLo);

		MultiPrecision::UInt<1> div1(1000000000ul);

		char szBuf[64]; // little extra
		char* szPos = szBuf + _countof(szBuf) - 1;
		szPos[0] = 0;

		while (true)
		{
			MultiPrecision::UInt<4> quot;
			quot.SetDivResid(val, div1);

			szPos -= 9;
			Utils::String::Decimal::PrintNoZTerm(szPos, val.get_Val<1>(), 9);

			if (quot.IsZero())
				break;
			val = quot;
		}

		while ('0' == *szPos)
			szPos++;

		Env::DocGroup gr(sz);
		DocSetType("amount");
		Env::DocAddText("value", szPos);

	}
	else
		DocAddAmount(sz, valLo);

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

void DocAddFloat(const char* sz, MultiPrecision::Float x)
{
	char szBuf[MultiPrecision::Float::DecimalForm::s_LenScientificMax + 1];
	x.get_Decimal().PrintAuto(szBuf);
	Env::DocAddText(sz, szBuf);
}

void DocAddFloatDbg(const char* sz, MultiPrecision::Float x)
{
	// convenient for debugging, to try the exact values on host
	char szBuf[Utils::String::Hex::DigitsMax<uint64_t>::N + Utils::String::Decimal::DigitsMax<uint32_t>::N + 10];
	Utils::String::Hex::Print(szBuf, x.m_Num, Utils::String::Hex::DigitsMax<uint64_t>::N);
	uint32_t n = Utils::String::Hex::DigitsMax<uint64_t>::N;
	szBuf[n++] = ' ';

	if (x.m_Order >= 0)
		szBuf[n++] = '+';
	else
	{
		szBuf[n++] = '-';
		x.m_Order = -x.m_Order;
	}

	n += Utils::String::Decimal::Print(szBuf + n, x.m_Order);
	szBuf[n] = 0;

	Env::DocAddText(sz, szBuf);
}

void DocAddPerc(const char* sz, MultiPrecision::Float x, uint32_t nDigsAfterDot = 3)
{
	MultiPrecision::Float::DecimalForm df;
	df.Assign(x);
	df.m_Order10 += 2; // to perc

	MultiPrecision::Float::DecimalForm::PrintOptions po;
	po.m_DigitsAfterDot = nDigsAfterDot;

	// remove unnecessary extra precision
	auto df2 = df;
	int32_t nExtra = -(po.m_DigitsAfterDot + df2.m_Order10);
	if (nExtra > 0)
	{
		// loose extra precision
		if (df2.m_NumDigits > (uint32_t)nExtra)
			df2.LimitPrecision(df2.m_NumDigits - nExtra);
		else
		{
			// loose all, make it 0
			df2.m_Num = 0;
			df2.m_Order10 = 0;
			df2.m_NumDigits = 1;
		}
	}

	char szBuf[MultiPrecision::Float::DecimalForm::s_LenScientificMax + 1];
	if (df2.get_TextLenStd(po) < _countof(szBuf))
		df2.PrintStd(szBuf, po);
	else
	{
		df.LimitPrecision(nDigsAfterDot + 2);
		po.m_DigitsAfterDot = -1;
		df.PrintScientific(szBuf, po);
	}

	Env::DocAddText(sz, szBuf);
}


#define HandleContractsStd(macro) \
	macro(Vault, Vault::s_SID) \
	macro(VaultAnon, VaultAnon::s_SID) \
	macro(Faucet, Faucet::s_SID) \
	macro(Faucet2, Faucet2::s_SID) \
	macro(Minter, Minter::s_SID) \
	macro(BlackHole, BlackHole::s_SID) \
	macro(L2Tst1_L2, L2Tst1_L2::s_SID) \

#define HandleContractsVer(macro) \
	macro(Oracle2, Oracle2::s_pSID) \
	macro(Nephrite, Nephrite::s_pSID) \
	macro(DaoVault, DaoVault::s_pSID) \
	macro(Bans, NameService::s_pSID) \
	macro(DEX, Amm::s_pSID) \
	macro(DaoCore2, DaoCore2::s_pSID) \
	macro(DaoAccumulator, DaoAccumulator::s_pSID) \
	macro(DaoVote, DaoVote::s_pSID) \
	macro(L2Tst1_L1, L2Tst1_L1::s_pSID) \

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
		DocAddMonoblob("subtype", sid);
	}

	void OnMethod(const char* sz)
	{
		assert(m_Method);
		Env::DocAddText("method", sz);
	}

	struct GroupArgs :public Env::DocGroup {
		GroupArgs() :Env::DocGroup("params") {}
	};

	struct GroupDbg :public Env::DocGroup {
		GroupDbg() :Env::DocGroup("dbg") {}
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
	void DumpNephriteDbgStatus();
	static void WriteL2Tst1_L1_Settings(const L2Tst1_L1::Settings&);
	static void WriteL2Tst1_L1_Validators(const L2Tst1_L1::Validator*, uint32_t);
	void OnL2tsts1_BridgeOp(const L2Tst1_L1::Method::BridgeOp&);
	static void WriteOracle2Settings(const Oracle2::Settings&);
	static bool get_Oracle2Median(MultiPrecision::Float&, const ContractID& cid);
	static void WriteBansSettings(const NameService::Settings&);
	static void WriteMinterSettings(const Minter::Settings&);
	template <typename T>
	void DocSetBansName_T(const T& x) {
		DocSetBansNameEx(&x + 1, x.m_NameLen);
	}
	void DocSetBansNameEx(const void* p, uint32_t nLen);

	static void WriteAmmSettings(const Amm::Settings&);
	static void DocSetAmmPool(const Amm::Pool::ID&);
	static const char* get_AmmKind(const Amm::Pool::ID&);

	void AddNephiriteTroveNumber(const Nephrite::Method::BaseTxTrove*);

	static void WriteDaoVoteCfg(const DaoVote::Cfg&);

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
		void Save(uint32_t iEpoch, const HomogenousPool::Epoch<nDims>& e) {
		}
		void Del(uint32_t iEpoch) {
		}
	};


	void OnDaoAccumulator_UserWithdraw(uint8_t nType);
	void OnState_DaoAccumulator_Pool(DaoAccumulator::Pool&, const char* szName);
	void OnState_DaoAccumulator_Users(DaoAccumulator::Pool&, uint8_t type, const char* szName);

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
	if (!Utils::Shader::get_Sid_FromContract(sid, us.m_Cid))
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
	if (!Utils::Shader::get_Sid_FromContract(sid, us.m_Active.m_Cid))
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
		if (!Utils::Shader::get_Sid_FromContract(sid, cid))
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
	Utils::Shader::get_Sid(sid, &x + 1, nSizeShader);

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
			DocAddPk("", pk);
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

void ParserContext::OnMethod_Faucet2()
{
	switch (m_iMethod)
	{
	case Faucet2::Method::Create::s_iMethod:
		OnMethod("Create");
		break;

	case Faucet2::Method::Deposit::s_iMethod:
		OnMethod("Deposit");
		break;

	case Faucet2::Method::Withdraw::s_iMethod:
		OnMethod("Withdraw");
		break;

	case Faucet2::Method::AdminCtl::s_iMethod:
	{
		OnMethod("Admin-Ctl");

		auto pArg = get_ArgsAs<Faucet2::Method::AdminCtl>();
		if (pArg)
		{
			GroupArgs gr;
			Env::DocAddNum32("Enable", pArg->m_Enable);

		}
	}
	break;

	case Faucet2::Method::AdminWithdraw::s_iMethod:
		OnMethod("Admin-Withdraw");
		break;
	}
}

void ParserContext::OnState_Faucet2()
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = m_Cid;
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

void ParserContext::OnMethod_DaoCore()
{
	OnName("Dao-Core");

	if (m_Method)
	{
		switch (m_iMethod)
		{
		case DaoCore::GetPreallocated::s_iMethod: OnMethod("Get Preallocated"); break;
		case DaoCore::UpdPosFarming::s_iMethod: OnMethod("Farming Upd"); break;

		case DaoCore2::Method::AdminWithdraw::s_iMethod:
			{
				OnMethod("Admin Withdraw");
				auto pArg = get_ArgsAs<DaoCore2::Method::AdminWithdraw>();
				if (pArg)
				{
					GroupArgs gr;
					WriteUpgradeAdminsMask(pArg->m_ApproveMask);
				}
			}
			break;
		}
	}
}

void ParserContext::OnMethod_DaoCore_Masternet() {
	OnMethod_DaoCore();
}
void ParserContext::OnMethod_DaoCore_Testnet() {
	OnMethod_DaoCore();
}
void ParserContext::OnMethod_DaoCore2(uint32_t /* iVer */) {
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
void ParserContext::OnState_DaoCore2(uint32_t /* iVer */) {
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
				DocAddPk("key", pArg->m_Key.m_pkUser);
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

void ParserContext::AddNephiriteTroveNumber(const Nephrite::Method::BaseTxTrove* pArg)
{
	uint32_t iTrove;

	if (pArg && pArg->m_iPrev0)
	{
		Env::Key_T<Nephrite::Trove::Key> tk;
		_POD_(tk.m_Prefix.m_Cid) = m_Cid;
		tk.m_KeyInContract.m_iTrove = pArg->m_iPrev0;

		Nephrite::Trove t;
		if (!Env::VarReader::Read_T(tk, t))
			return;

		iTrove = t.m_iNext;
	}
	else
	{
		Env::Key_T<uint8_t> k;
		_POD_(k.m_Prefix.m_Cid) = m_Cid;
		k.m_KeyInContract = Nephrite::Tags::s_State;

		Nephrite::Global g;
		if (!Env::VarReader::Read_T(k, g))
			return;

		iTrove = pArg ? g.m_Troves.m_iHead : (g.m_Troves.m_iLastCreated + 1);
	}

	Env::DocAddNum("Number", iTrove);
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

				AddNephiriteTroveNumber(nullptr);
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
				AddNephiriteTroveNumber(pArg);
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
				AddNephiriteTroveNumber(pArg);
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

				Env::DocAddNum("Count", pArg->m_Count);
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

	DumpNephriteDbgStatus();
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

			NephriteEpochStorage<Nephrite::Tags::s_Epoch_Redist> storR(m_Cid);
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

	DumpNephriteDbgStatus();
}

void TestEqual(Amount a, Amount b, const char* szErr)
{
	// allow for little error
	a = (a >= b) ? (a - b) : (b - a);
	if (a > 10)
		Env::DocAddNum(szErr, a);
}

void ParserContext::DumpNephriteDbgStatus()
{
/*
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = m_Cid;
	k.m_KeyInContract = Nephrite::Tags::s_State;

	Nephrite::Global g;
	if (!Env::VarReader::Read_T(k, g))
		return;

	GroupDbg gr0;

	{
		Env::DocGroup gr1("Totals");
		Env::DocAddNum("Tok", g.m_Troves.m_Totals.Tok);
		Env::DocAddNum("Col", g.m_Troves.m_Totals.Col);
	}

	{
		Env::DocGroup gr1("stab");
		Env::DocAddNum("Tok", g.m_StabPool.get_TotalSell());
		Env::DocAddNum("Col", g.m_StabPool.m_Active.m_pDim[0].m_Buy + g.m_StabPool.m_Active.m_pDim[1].m_Buy);
		
	}

	Nephrite::Pair trovesTotalsOrg, trovesTotalsAdj;
	_POD_(trovesTotalsOrg).SetZero();
	_POD_(trovesTotalsAdj).SetZero();

	MultiPrecision::Float weight(0u);

	{
		Env::DocGroup gr1("troves");

		Env::Key_T<Nephrite::Trove::Key> tk0, tk1;
		_POD_(tk0.m_Prefix.m_Cid) = m_Cid;
		_POD_(tk1.m_Prefix.m_Cid) = m_Cid;
		tk0.m_KeyInContract.m_iTrove = 0;
		tk1.m_KeyInContract.m_iTrove = (Nephrite::Trove::ID)-1;

		for (Env::VarReader r(tk0, tk1); ; )
		{
			Nephrite::Trove t;
			if (!r.MoveNext_T(tk0, t))
				break;

			char sz[Utils::String::Decimal::DigitsMax<Nephrite::Trove::ID>::N + 1];
			Utils::String::Decimal::Print(sz, tk0.m_KeyInContract.m_iTrove);
			Env::DocGroup gr2(sz);

			{
				Env::DocGroup gr2("org");
				Env::DocAddNum("Tok", t.m_Amounts.Tok);
				Env::DocAddNum("Col", t.m_Amounts.Col);
			}

			trovesTotalsOrg.Tok += t.m_Amounts.Tok;
			trovesTotalsOrg.Col += t.m_Amounts.Col;

			auto vals = g.m_RedistPool.get_UpdatedAmounts(t);

			{
				Env::DocGroup gr2("adj");
				Env::DocAddNum("Tok", vals.Tok);
				Env::DocAddNum("Col", vals.Col);
			}

			trovesTotalsAdj.Tok += vals.Tok;
			trovesTotalsAdj.Col += vals.Col;

			DocAddFloatSc("weight", t.m_RedistUser.m_Weight);
			DocAddFloatSc("Sigma", t.m_RedistUser.m_pSigma0[0]);

			weight += t.m_RedistUser.m_Weight;

		}
	}

	{
		Env::DocGroup gr1("redist");
		Env::DocAddNum("Tok", g.m_RedistPool.m_Active.m_Sell);
		Env::DocAddNum("Col", g.m_RedistPool.m_Active.m_pDim[0].m_Buy);
		DocAddFloatSc("Weight0", g.m_RedistPool.m_Active.m_Weight);
		DocAddFloatSc("Weight1", weight);
		DocAddFloatSc("Sigma", g.m_RedistPool.m_Active.m_pDim[0].m_Sigma);
	}

	// Totals must be equal to the sum of effective (adjusted) trove amounts
	TestEqual(g.m_Troves.m_Totals.Tok, trovesTotalsAdj.Tok, "errz-1-Tok");
	TestEqual(g.m_Troves.m_Totals.Col, trovesTotalsAdj.Col, "errz-1-Col");

	// All troves must be part of redist pool
	if (g.m_Troves.m_Totals.Tok != g.m_RedistPool.m_Active.m_Sell)
		Env::DocAddText("errz-2", "");

	// extra collateral in the redist pool must be equal to diff of ordinal vs adjusted trove values
	TestEqual(g.m_RedistPool.m_Active.m_pDim[0].m_Buy, g.m_Troves.m_Totals.Col - trovesTotalsOrg.Col, "errz-3");
*/
}

void ParserContext::WriteL2Tst1_L1_Settings(const L2Tst1_L1::Settings& stg)
{
	DocAddAid("Staking Token", stg.m_aidStaking);
	DocAddAid("Liquidity Token", stg.m_aidLiquidity);
	DocAddHeight("Per-phase End", stg.m_hPreEnd);
}

void ParserContext::WriteL2Tst1_L1_Validators(const L2Tst1_L1::Validator* pV, uint32_t nV)
{
	Env::DocGroup gr1("Validators");
	DocSetType("table");
	Env::DocArray gr2("value");

	{
		Env::DocArray gr3("");
		DocAddTableHeader("Index");
		DocAddTableHeader("Key");
	}

	for (uint32_t i = 0; i < nV; i++)
	{
		const auto& v = pV[i];

		Env::DocArray gr3("");
		Env::DocAddNum("", i);
		DocAddPk("", v.m_pk);
	}
}

void ParserContext::OnMethod_L2Tst1_L1(uint32_t /* iVer */)
{
	switch (m_iMethod)
	{
	case L2Tst1_L1::Method::Create::s_iMethod:
		{
			auto pArg = get_ArgsAs<L2Tst1_L1::Method::Create>();
			if (pArg)
			{
				GroupArgs gr;

				WriteL2Tst1_L1_Settings(pArg->m_Settings);
				WriteUpgradeSettings(pArg->m_Upgradable);

				if (m_nArg >= sizeof(*pArg) + sizeof(L2Tst1_L1::Validator) * pArg->m_Validators)
					WriteL2Tst1_L1_Validators((const L2Tst1_L1::Validator*) (pArg + 1), pArg->m_Validators);
			}
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		OnUpgrade3Method();
		break;

	case L2Tst1_L1::Method::UserStake::s_iMethod:
		{
			auto pArg = get_ArgsAs<L2Tst1_L1::Method::UserStake>();
			if (pArg)
			{
				OnMethod("User stake");
				GroupArgs gr;

				DocAddAmount("Amount", pArg->m_Amount);
				DocAddPk("Pk", pArg->m_pkUser);
			}
		}
		break;
		
	case L2Tst1_L1::Method::BridgeExport::s_iMethod:
		{
			auto pArg = get_ArgsAs<L2Tst1_L1::Method::BridgeExport>();
			if (pArg)
			{
				OnMethod("Bridge Export");
				GroupArgs gr;
				OnL2tsts1_BridgeOp(*pArg);
			}
		}
		break;

	case L2Tst1_L1::Method::BridgeImport::s_iMethod:
		{
			auto pArg = get_ArgsAs<L2Tst1_L1::Method::BridgeImport>();
			if (pArg)
			{
				OnMethod("Bridge Import");
				GroupArgs gr;
				OnL2tsts1_BridgeOp(*pArg);
				WriteUpgradeAdminsMask(pArg->m_ApproveMask);
			}
		}
		break;
	}

}

void ParserContext::OnL2tsts1_BridgeOp(const L2Tst1_L1::Method::BridgeOp& op)
{
	DocAddAidAmount("Value", op.m_Aid, op.m_Amount);
	DocAddMonoblob("cookie", op.m_Cookie);
	DocAddPk("pk", op.m_pk);
}

void ParserContext::OnState_L2Tst1_L1(uint32_t /* iVer */)
{
	WriteUpgrade3State();

	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = m_Cid;
	k.m_KeyInContract = L2Tst1_L1::Tags::s_State;

	L2Tst1_L1::State s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	{
		Env::DocGroup gr2("Settings");
		WriteL2Tst1_L1_Settings(s.m_Settings);
	}


	{
		k.m_KeyInContract = L2Tst1_L1::Tags::s_Validators;
		Env::VarReader r(k, k);

		L2Tst1_L1::Validator pV[L2Tst1_L1::Validator::s_Max];
		uint32_t nKey = 0, nVal = sizeof(pV);

		if (r.MoveNext(nullptr, nKey, pV, nVal, 0) && (nVal >= sizeof(L2Tst1_L1::Validator)) && (nVal <= sizeof(pV)))
			WriteL2Tst1_L1_Validators(pV, nVal / sizeof(L2Tst1_L1::Validator));

	}
}

void ParserContext::OnMethod_L2Tst1_L2()
{
	switch (m_iMethod)
	{
	case L2Tst1_L2::Method::BridgeEmit::s_iMethod:
		{
			auto pArg = get_ArgsAs<L2Tst1_L2::Method::BridgeEmit>();
			if (pArg)
			{
				OnMethod("Mint");
				GroupArgs gr;
				OnL2tsts1_BridgeOp(*pArg);
			}
		}
		break;

	case L2Tst1_L2::Method::BridgeBurn::s_iMethod:
		{
			auto pArg = get_ArgsAs<L2Tst1_L2::Method::BridgeBurn>();
			if (pArg)
			{
				OnMethod("Burn");
				GroupArgs gr;
				OnL2tsts1_BridgeOp(*pArg);
			}
		}
		break;
	}

}


void ParserContext::OnState_L2Tst1_L2()
{
	// no state
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

void ParserContext::WriteMinterSettings(const Minter::Settings& stg)
{
	DocAddCid("Dao-Vault", stg.m_cidDaoVault);
	DocAddAmount("Issuance fee", stg.m_IssueFee);
}

void ParserContext::OnMethod_Minter()
{
	switch (m_iMethod)
	{
	case Minter::Method::Init::s_iMethod:
		{
			auto pArg = get_ArgsAs<Minter::Method::Init>();
			if (pArg)
			{
				GroupArgs gr;
				WriteMinterSettings(pArg->m_Settings);
			}
		}
		break;

	case Minter::Method::View::s_iMethod:
		{
			auto pArg = get_ArgsAs<Minter::Method::View>();
			if (pArg)
			{
				OnMethod("View");
				GroupArgs gr;

				DocAddAid("aid", pArg->m_Aid);
			}
		}
		break;
		
	case Minter::Method::CreateToken::s_iMethod:
		{
			auto pArg = get_ArgsAs<Minter::Method::CreateToken>();
			if (pArg)
			{
				OnMethod("Create token");
				GroupArgs gr;

				DocAddAmountBig("Limit", pArg->m_Limit.m_Lo, pArg->m_Limit.m_Hi);

				if (Minter::PubKeyFlag::s_Cid == pArg->m_pkOwner.m_Y)
					DocAddCid("Owner", pArg->m_pkOwner.m_X);
				else
					DocAddPk("Owner", pArg->m_pkOwner);

				// TODO - aid
			}
		}
		break;

	case Minter::Method::Withdraw::s_iMethod:
		{
			auto pArg = get_ArgsAs<Minter::Method::Withdraw>();
			if (pArg)
			{
				OnMethod("Withdraw");
				GroupArgs gr;

				DocAddAmount("amount", pArg->m_Value);
				DocAddAid("aid", pArg->m_Aid);
			}
		}
		break;
	}
}

void ParserContext::OnState_Minter()
{
	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = m_Cid;
	k.m_KeyInContract = Minter::Tags::s_Settings;

	Minter::Settings s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	{
		Env::DocGroup gr2("Settings");
		WriteMinterSettings(s);
	}

	// TODO - tokens list
}

void ParserContext::OnMethod_BlackHole()
{
	switch (m_iMethod)
	{
	case BlackHole::Method::Deposit::s_iMethod:
		OnMethod("Deposit");
		break;
	}
}

void ParserContext::OnState_BlackHole()
{
}

void ParserContext::WriteAmmSettings(const Amm::Settings& stg)
{
	DocAddCid("Dao-Vault", stg.m_cidDaoVault);
}

void ParserContext::DocSetAmmPool(const Amm::Pool::ID& pid)
{
	DocAddAid("Aid1", pid.m_Aid1);
	DocAddAid("Aid2", pid.m_Aid2);
	Env::DocAddText("Volatility", get_AmmKind(pid));
}

const char* ParserContext::get_AmmKind(const Amm::Pool::ID& pid)
{
	switch (pid.m_Fees.m_Kind)
	{
	case 0: return "Low";
	case 1: return "Medium";
	case 2: return "High";
	}
	return "";
}


void ParserContext::OnMethod_DEX(uint32_t /* iVer */)
{
	switch (m_iMethod)
	{
	case Amm::Method::Create::s_iMethod:
		{
			auto pArg = get_ArgsAs<Amm::Method::Create>();
			if (pArg)
			{
				GroupArgs gr;

				WriteAmmSettings(pArg->m_Settings);
				WriteUpgradeSettings(pArg->m_Upgradable);
			}
		}
		break;

	case Upgradable3::Method::Control::s_iMethod:
		OnUpgrade3Method();
		break;

	case Amm::Method::PoolCreate::s_iMethod:
		{
			auto pArg = get_ArgsAs<Amm::Method::PoolCreate>();
			if (pArg)
			{
				OnMethod("Pool Create");
				GroupArgs gr;

				DocSetAmmPool(pArg->m_Pid);
				DocAddPk("Creator", pArg->m_pkCreator);
			}
		}
		break;

	case Amm::Method::PoolDestroy::s_iMethod:
		{
			auto pArg = get_ArgsAs<Amm::Method::PoolDestroy>();
			if (pArg)
			{
				OnMethod("Pool Destroy");
				GroupArgs gr;
				DocSetAmmPool(pArg->m_Pid);
			}
		}
		break;

	case Amm::Method::AddLiquidity::s_iMethod:
		{
			auto pArg = get_ArgsAs<Amm::Method::AddLiquidity>();
			if (pArg)
			{
				OnMethod("Liquidity Add");
				GroupArgs gr;
				DocSetAmmPool(pArg->m_Pid);
			}
		}
		break;

	case Amm::Method::Withdraw::s_iMethod:
		{
			auto pArg = get_ArgsAs<Amm::Method::Withdraw>();
			if (pArg)
			{
				OnMethod("Liquidity Withdraw");
				GroupArgs gr;
				DocSetAmmPool(pArg->m_Pid);
			}
		}
		break;

	case Amm::Method::Trade::s_iMethod:
		{
			auto pArg = get_ArgsAs<Amm::Method::Trade>();
			if (pArg)
			{
				OnMethod("Trade");
				GroupArgs gr;
				DocSetAmmPool(pArg->m_Pid);
			}
		}
		break;
	}
}

void ParserContext::OnState_DEX(uint32_t /* iVer */)
{
	WriteUpgrade3State();

	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = m_Cid;
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
		_POD_(k0.m_Prefix.m_Cid) = m_Cid;
		_POD_(k1.m_Prefix.m_Cid) = m_Cid;
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
					auto k = i ? (f2 / f1) : (f1 / f2);
					auto df = k.get_Decimal();

					// print the ratio nicely. Limit to 8 precision digits, prefer std notation
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

void OnDaoAccumulator_PoolType(uint8_t nType)
{
	static const char s_szName[] = "Pool";
	switch (nType)
	{
	case DaoAccumulator::Method::UserLock::Type::BeamX_PrePhase:
		Env::DocAddText(s_szName, "Beam-BeamX pre-phase");
		break;

	case DaoAccumulator::Method::UserLock::Type::BeamX:
		Env::DocAddText(s_szName, "Beam-BeamX");
		break;

	case DaoAccumulator::Method::UserLock::Type::Nph:
		Env::DocAddText(s_szName, "Beam-Nph");
		break;
	}
}

void ParserContext::OnDaoAccumulator_UserWithdraw(uint8_t nType)
{
	auto pArg = get_ArgsAs<DaoAccumulator::Method::UserWithdraw_Base>();
	if (pArg)
	{
		OnMethod("Withdraw");
		GroupArgs gr;

		OnDaoAccumulator_PoolType(nType);

		DocAddPk("pk", pArg->m_pkUser);
	}
}

void ParserContext::OnMethod_DaoAccumulator(uint32_t /* iVer */)
{
	switch (m_iMethod)
	{
	case DaoAccumulator::Method::Create::s_iMethod:
	{
		auto pArg = get_ArgsAs<DaoAccumulator::Method::Create>();
		if (pArg)
		{
			GroupArgs gr;
			WriteUpgradeSettings(pArg->m_Upgradable);
			DocAddAid("beamX", pArg->m_aidBeamX);
			DocAddHeight("Per-phase end", pArg->m_hPrePhaseEnd);
		}
	}
	break;

	case Upgradable3::Method::Control::s_iMethod:
		OnUpgrade3Method();
		break;

	case DaoAccumulator::Method::FarmStart::s_iMethod:
	{
		auto pArg = get_ArgsAs<DaoAccumulator::Method::FarmStart>();
		if (pArg)
		{
			OnMethod("Farm Start");
			GroupArgs gr;

			WriteUpgradeAdminsMask(pArg->m_ApproveMask);
			DocAddAid("LP-Token", pArg->m_aidLpToken);
			DocAddAmount("Total Reward", pArg->m_FarmBeamX);
			Env::DocAddNum("Total Duration", pArg->m_hFarmDuration);

		}
	}
	break;

	case DaoAccumulator::Method::UserLock::s_iMethod:
	{
		auto pArg = get_ArgsAs<DaoAccumulator::Method::UserLock>();
		if (pArg)
		{
			OnMethod("Lock");
			GroupArgs gr;
			OnDaoAccumulator_PoolType(pArg->m_PoolType);
			DocAddPk("pk", pArg->m_pkUser);
			DocAddHeight("hEnd", pArg->m_hEnd);
		}
	}
	break;

	case DaoAccumulator::Method::UserWithdraw_FromBeamNph::s_iMethod:
		OnDaoAccumulator_UserWithdraw(DaoAccumulator::Method::UserLock::Type::Nph);
		break;

	case DaoAccumulator::Method::UserWithdraw_FromBeamBeamX::s_iMethod:
		OnDaoAccumulator_UserWithdraw(DaoAccumulator::Method::UserLock::Type::BeamX);
		break;
	}
}

void ParserContext::OnState_DaoAccumulator_Pool(DaoAccumulator::Pool& p, const char* szName)
{
	Env::DocGroup gr(szName);

	p.Update(Env::get_Height());

	DocAddAmount("Reward remaining", p.m_AmountRemaining);
	Env::DocAddNum("Farming duration remaining", p.m_hRemaining);
}

void ParserContext::OnState_DaoAccumulator_Users(DaoAccumulator::Pool& p, uint8_t type, const char* szName)
{
	Env::DocGroup gr2(szName);
	DocSetType("table");
	Env::DocArray gr3("value");

	{
		Env::DocArray gr4("");
		DocAddTableHeader("LP-Tokens");
		DocAddTableHeader("Locked until");
		DocAddTableHeader("Reward");
		DocAddTableHeader("Key");
	}

	Env::Key_T<DaoAccumulator::User::KeyBase> k0, k1;
	k0.m_KeyInContract.m_Tag = type;
	k1.m_KeyInContract.m_Tag = type;
	_POD_(k0.m_Prefix.m_Cid) = m_Cid;
	_POD_(k1.m_Prefix.m_Cid) = m_Cid;
	_POD_(k0.m_KeyInContract.m_pk).SetZero();
	_POD_(k1.m_KeyInContract.m_pk).SetObject(0xff);

	for (Env::VarReader r(k0, k1); ; )
	{
		DaoAccumulator::User u;
		if (!r.MoveNext_T(k0, u))
			break;

		Env::DocArray gr4("");

		DocAddAmount("", u.m_LpToken);
		DocAddHeight("", u.m_hEnd);

		u.m_EarnedBeamX += p.Remove(u.m_PoolUser);
		DocAddAmount("", u.m_EarnedBeamX);

		DocAddPk("", k0.m_KeyInContract.m_pk);
	}
}

void ParserContext::OnState_DaoAccumulator(uint32_t /* iVer */)
{
	WriteUpgrade3State();

	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = m_Cid;
	k.m_KeyInContract = DaoAccumulator::Tags::s_State;

	DaoAccumulator::State s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	DocAddAid("BeamX", s.m_aidBeamX);
	DocAddHeight("Pre-phaseend height ", s.m_hPreEnd);

	if (s.m_aidLpToken)
	{
		DocAddAid("LP-token", s.m_aidLpToken);

		OnState_DaoAccumulator_Pool(s.m_Pool, "Pool Beam/BeamX");
	}

	DaoAccumulator::Pool p_Nph;
	k.m_KeyInContract = DaoAccumulator::Tags::s_PoolBeamNph;

	if (Env::VarReader::Read_T(k, p_Nph))
		OnState_DaoAccumulator_Pool(p_Nph, "Pool Beam/Nph");
	else
		_POD_(p_Nph).SetZero();

	OnState_DaoAccumulator_Users(s.m_Pool, DaoAccumulator::Tags::s_User, "Beam/BeamX users");
	OnState_DaoAccumulator_Users(p_Nph, DaoAccumulator::Tags::s_UserBeamNph, "Beam/Nph users");
}





void ParserContext::OnMethod_DaoVote(uint32_t /* iVer */)
{
	switch (m_iMethod)
	{
	case DaoVote::Method::Create::s_iMethod:
	{
		auto pArg = get_ArgsAs<DaoVote::Method::Create>();
		if (pArg)
		{
			GroupArgs gr;
			WriteUpgradeSettings(pArg->m_Upgradable);
			WriteDaoVoteCfg(pArg->m_Cfg);
		}
	}
	break;

	case Upgradable3::Method::Control::s_iMethod:
		OnUpgrade3Method();
		break;

	case DaoVote::Method::AddProposal::s_iMethod:
	{
		auto pArg = get_ArgsAs<DaoVote::Method::AddProposal>();
		if (pArg)
		{
			OnMethod("Add proposal");
			GroupArgs gr;

			DocAddPk("moderator", pArg->m_pkModerator);
		}
	}
	break;

	case DaoVote::Method::MoveFunds::s_iMethod:
	{
		auto pArg = get_ArgsAs<DaoVote::Method::MoveFunds>();
		if (pArg)
			OnMethod(pArg->m_Lock ? "Funs Lock" : "Funds Unlock");
	}
	break;

	case DaoVote::Method::Vote::s_iMethod:
	{
		auto pArg = get_ArgsAs<DaoVote::Method::Vote>();
		if (pArg)
		{
			OnMethod("Vote");
		}
	}
	break;

	case DaoVote::Method::AddDividend::s_iMethod:
	{
		auto pArg = get_ArgsAs<DaoVote::Method::AddDividend>();
		if (pArg)
			OnMethod("Add dividend");
	}
	break;

	case DaoVote::Method::GetResults::s_iMethod:
	{
		auto pArg = get_ArgsAs<DaoVote::Method::GetResults>();
		if (pArg)
			OnMethod("Get results");
	}
	break;

	case DaoVote::Method::SetModerator::s_iMethod:
	{
		auto pArg = get_ArgsAs<DaoVote::Method::SetModerator>();
		if (pArg)
		{
			OnMethod("Set moderator");
			GroupArgs gr;

			DocAddPk("moderator", pArg->m_pk);
			Env::DocAddNum32("Enable", pArg->m_Enable);
		}
	}
	break;
	}
}

void ParserContext::OnState_DaoVote(uint32_t /* iVer */)
{
	WriteUpgrade3State();

	Env::Key_T<uint8_t> k;
	_POD_(k.m_Prefix.m_Cid) = m_Cid;
	k.m_KeyInContract = DaoVote::Tags::s_State;

	DaoVote::State s;
	if (!Env::VarReader::Read_T(k, s))
		return;

	{
		Env::DocGroup gr("Settings");
		WriteDaoVoteCfg(s.m_Cfg);
	}

}

void ParserContext::WriteDaoVoteCfg(const DaoVote::Cfg& cfg)
{
	DocAddAid("Voting asset", cfg.m_Aid);
	Env::DocAddNum("Epoch duration", cfg.m_hEpochDuration);
	DocAddPk("Admin", cfg.m_pkAdmin);
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
