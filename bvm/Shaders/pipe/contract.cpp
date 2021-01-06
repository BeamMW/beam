#include "../common.h"
#include "contract.h"

export void Ctor(const Pipe::Create& r)
{
	Pipe::Global g;
	Utils::Copy(g.m_Cfg, r.m_Cfg);
	
	uint8_t key = 0;
	Env::SaveVar_T(key, g);
}

export void Dtor(void*)
{
}

void UpdateState(HashValue& res, const void* pMsg, uint32_t nMsg)
{
	HashProcessor hp;
	hp.m_p = Env::HashCreateSha256();

	static const char szSeed[] = "b.pipe";
	hp.Write(szSeed, sizeof(szSeed)); // including null-term
	hp << nMsg;
	hp.Write(pMsg, nMsg);
	hp
		<< res
		>> res;
}

export void Method_2(const Pipe::PushLocal0& r)
{
	Pipe::OutState os;
	uint8_t key = 1;

	if (!Env::LoadVar_T(key, os))
		Utils::ZeroObject(os);

	uint32_t nSize = r.m_MsgSize + sizeof(ContractID);
	ContractID* pMsg = (ContractID*) Env::StackAlloc(nSize);
	Env::get_CallerCid(1, *pMsg); // caller
	Env::Memcpy(pMsg + 1, &r + 1, r.m_MsgSize);

	Env::SaveVar(&os.m_Count, sizeof(os.m_Count), pMsg, nSize);

	UpdateState(os.m_Checksum, pMsg, nSize);
	os.m_Count++;

	Env::SaveVar_T(key, os);
}

