#include "../common.h"
#include "contract.h"

export void Ctor(const Pipe::Create& r)
{
	Pipe::StateOut so;
	Utils::ZeroObject(so);
	Utils::Copy(so.m_Cfg, r.m_Cfg.m_Out);
	
	Pipe::StateOut::Key ko;
	Env::SaveVar_T(ko, so);

	Pipe::StateIn si;
	Utils::ZeroObject(si);
	Utils::Copy(si.m_Cfg, r.m_Cfg.m_In);

	Pipe::StateOut::Key ki;
	Env::SaveVar_T(ki, si);
}

export void Dtor(void*)
{
	Env::Halt(); // not supported
}

export void Method_2(const Pipe::SetRemote& r)
{
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Env::Halt_if(!Utils::IsZero(si.m_cidRemote) || Utils::IsZero(r.m_cid));

	Utils::Copy(si.m_cidRemote, r.m_cid);
	Env::SaveVar_T(ki, si);

}

void get_MsgHash(HashValue& res, const Pipe::MsgHdr* pMsg, uint32_t nMsg)
{
	HashProcessor hp;
	hp.m_p = Env::HashCreateSha256();
	hp << "b.msg";
	hp.Write(pMsg, nMsg);
	hp >> res;
}

void UpdateState(HashValue& res, const HashValue& hvMsg)
{
	HashProcessor hp;
	hp.m_p = Env::HashCreateSha256();
	hp
		<< "b.pipe"
		<< res
		<< hvMsg
		>> res;
}

void UpdateState(HashValue& res, const Pipe::MsgHdr* pMsg, uint32_t nMsg)
{
	HashValue hvMsg;
	get_MsgHash(hvMsg, pMsg, nMsg);
	UpdateState(res, hvMsg);
}

export void Method_3(const Pipe::PushLocal0& r)
{
	Height h = Env::get_Height();

	uint32_t nSize = r.m_MsgSize + sizeof(Pipe::MsgHdr);
	auto* pMsg = (Pipe::MsgHdr*)Env::StackAlloc(nSize);

	Env::get_CallerCid(1, pMsg->m_Sender);
	Utils::Copy(pMsg->m_Receiver, r.m_Receiver);
	pMsg->m_Height = h;
	Env::Memcpy(pMsg + 1, &r + 1, r.m_MsgSize);

	Pipe::StateOut so;
	Pipe::StateOut::Key ko;
	Env::LoadVar_T(ko, so);

	Pipe::MsgHdr::Key km;
	km.m_iCheckpoint_BE = Utils::FromBE(so.m_iCheckpoint);
	km.m_iMsg_BE = Utils::FromBE(so.m_Checkpoint.m_iMsg);
	Env::SaveVar(&km, sizeof(km), pMsg, nSize);

	UpdateState(so.m_Checkpoint.m_hv, pMsg, nSize);
	so.m_Checkpoint.m_iMsg++;

	if (1 == so.m_Checkpoint.m_iMsg)
		so.m_Checkpoint.m_h0 = h;
	else
	{
		if ((so.m_Checkpoint.m_iMsg >= so.m_Cfg.m_CheckpointMaxMsgs) || (h - so.m_Checkpoint.m_h0 >= so.m_Cfg.m_CheckpointMaxDH))
		{
			Pipe::OutCheckpoint::Key cpk;
			cpk.m_iCheckpoint_BE = Utils::FromBE(so.m_iCheckpoint);

			Env::SaveVar_T(cpk, so.m_Checkpoint.m_hv);

			Utils::ZeroObject(so.m_Checkpoint);
			so.m_iCheckpoint++;
		}
	}

	Env::SaveVar_T(ko, so);
}

export void Method_4(const Pipe::PushRemote0& r)
{
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	// TODO
}
