#include "../common.h"
#include "../upgradable/contract.h"
#include "../vault/contract.h"
#include "../faucet/contract.h"

void SetType(const char* sz)
{
	Env::DocAddText("", sz);
}

void SetMethod(const char* sz)
{
	Env::DocAddText("", sz);
}

bool ParseContract(const ShaderID& sid, const ContractID& cid, uint32_t iMethod, const void* pArg, uint32_t nArg, bool bMethod);

void get_ShaderID(ShaderID& sid, void* pBuf, uint32_t nBuf)
{
	static const char szName[] = "contract.shader";

	HashProcessor::Sha256 hp;
	hp
		<< "bvm.shader.id"
		<< nBuf;

	hp.Write(pBuf, nBuf);
	hp >> sid;
}

bool get_ShaderID(ShaderID& sid, const ContractID& cid)
{
	Env::VarReader r(sid, sid);

	uint32_t nKey = 0, nVal = 0;
	if (!r.MoveNext(nullptr, nKey, nullptr, nVal, 0))
		return false;

	void* pVal = Env::Heap_Alloc(nVal);

	nKey = 0;
	r.MoveNext(nullptr, nKey, pVal, nVal, 1);

	get_ShaderID(sid, pVal, nVal);

	Env::Heap_Free(pVal);
	return true;
}

void OnType_Upgradable(const ContractID& cid, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	Env::Key_T<uint8_t> uk;
	_POD_(uk.m_Prefix.m_Cid) = cid;
	uk.m_KeyInContract = Upgradable::State::s_Key;

	Upgradable::State us;
	if (!Env::VarReader::Read_T(uk, us))
		return;

	ShaderID sid;
	if (!get_ShaderID(sid, us.m_Cid))
		return;

	bool bUpgrade = (Upgradable::ScheduleUpgrade::s_iMethod == iMethod);
	bool bRes = ParseContract(sid, cid, iMethod, pArg, nArg, !bUpgrade);

	if (!bRes)
	{
		Env::DocAddBlob_T("", cid);
		if (!bUpgrade)
		{
			Env::DocAddText("", "/");
			Env::DocAddNum("", iMethod);
		}
	}
	else
	{
		if (bUpgrade)
		{
			Env::DocAddText("", "/Upgrade");
			// TODO: params
		}
	}
	
}

void OnType_Vault(const ContractID& cid, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0: SetMethod("create"); break;
	case 1: SetMethod("destroy"); break;
	case Vault::Deposit::s_iMethod: SetMethod("deposit"); break;
	case Vault::Withdraw::s_iMethod: SetMethod("withdraw"); break;
	}
}

void OnType_Faucet(const ContractID& cid, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0:
		SetMethod("create");
		if (nArg >= sizeof(Faucet::Params))
		{
			auto& pars = *(Faucet::Params*) pArg;
			Env::DocAddNum(", Backlog period: ", pars.m_BacklogPeriod);
			Env::DocAddNum(", Max withdraw: ", pars.m_MaxWithdraw);
		}

		break;
	case 1: SetMethod("destroy"); break;
	case Faucet::Deposit::s_iMethod: SetMethod("deposit"); break;
	case Faucet::Withdraw::s_iMethod: SetMethod("withdraw"); break;
	}
}

bool ParseContract(const ShaderID& sid, const ContractID& cid, uint32_t iMethod, const void* pArg, uint32_t nArg, bool bMethod)
{
#define HandleContract(name) \
	if (_POD_(sid) == name::s_SID) { \
		SetType(#name); \
		if (bMethod) \
		{ \
			Env::DocAddText("", "/"); \
			OnType_##name(cid, iMethod, pArg, nArg); \
		} \
		return true; \
	}
	
	HandleContract(Upgradable)
	HandleContract(Vault)
	HandleContract(Faucet)

	return false;
}

BEAM_EXPORT void Method_0(const ShaderID& sid, const ContractID& cid, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	ParseContract(sid, cid, iMethod, pArg, nArg, true);
}

BEAM_EXPORT void Method_1(const ShaderID& sid, const ContractID& cid)
{
	ParseContract(sid, cid, -1, nullptr, 0, false);
}

