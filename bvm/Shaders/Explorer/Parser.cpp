#include "../common.h"
#include "../upgradable/contract.h"
#include "../vault/contract.h"
#include "../faucet/contract.h"

void SetType(const char* sz)
{
	Env::DocAddText("type", sz);
}

void SetMethod(const char* sz)
{
	Env::DocAddText("method", sz);
}

void OnType_Upgradable(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	// TODO
}

void OnType_Vault(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0: SetMethod("create"); break;
	case 1: SetMethod("destroy"); break;
	case Vault::Deposit::s_iMethod: SetMethod("deposit"); break;
	case Vault::Withdraw::s_iMethod: SetMethod("withdraw"); break;
	}
}

void OnType_Faucet(uint32_t iMethod, const void* pArg, uint32_t nArg)
{
	switch (iMethod)
	{
	case 0:
		SetMethod("create");
		if (nArg >= sizeof(Faucet::Params))
		{
			auto& pars = *(Faucet::Params*) pArg;
			Env::DocAddNum("Backlog period", pars.m_BacklogPeriod);
			Env::DocAddNum("Max withdraw", pars.m_MaxWithdraw);
		}

		break;
	case 1: SetMethod("destroy"); break;
	case Faucet::Deposit::s_iMethod: SetMethod("deposit"); break;
	case Faucet::Withdraw::s_iMethod: SetMethod("withdraw"); break;
	}
}

export void Method_0(const ShaderID& sid, uint32_t iMethod, const void* pArg, uint32_t nArg)
{
#define HandleContract(name) \
	if (_POD_(sid) == name::s_SID) { \
		SetType(#name); \
		OnType_##name(iMethod, pArg, nArg); \
	} else
	
	HandleContract(Upgradable)
	HandleContract(Vault)
	HandleContract(Faucet)

	{
		// unknown, ignore
	}
}

export void Method_1(const ShaderID& sid, const ContractID& cid)
{
	// TODO
}

