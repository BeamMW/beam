#include "../common.h"
#include "contract.h"
#include "../BeamHeader.h"

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

	Pipe::StateIn::Key ki;
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
	auto* pMsg = (Pipe::MsgHdr*) Env::StackAlloc(nSize);

	if (Env::get_CallDepth() > 1)
		Env::get_CallerCid(1, pMsg->m_Sender);
	else
		Utils::ZeroObject(pMsg->m_Sender);

	Utils::Copy(pMsg->m_Receiver, r.m_Receiver);
	pMsg->m_Height = h;
	Env::Memcpy(pMsg + 1, &r + 1, r.m_MsgSize);

	Pipe::StateOut so;
	Pipe::StateOut::Key ko;
	Env::LoadVar_T(ko, so);

	bool bNewCheckpoint = false;
	if (!so.m_Checkpoint.m_iMsg)
		bNewCheckpoint = true; // the very 1st message
	else
	{
		if ((so.m_Checkpoint.m_iMsg == so.m_Cfg.m_CheckpointMaxMsgs) || (h - so.m_Checkpoint.m_h0 >= so.m_Cfg.m_CheckpointMaxDH))
		{
			bNewCheckpoint = true;
			so.m_Checkpoint.m_iIdx++;
			so.m_Checkpoint.m_iMsg = 0;
		}
	}

	Pipe::OutCheckpoint::Key cpk;
	cpk.m_iCheckpoint_BE = Utils::FromBE(so.m_Checkpoint.m_iIdx);

	HashValue hv;
	if (bNewCheckpoint)
	{
		so.m_Checkpoint.m_h0 = h;
		Utils::ZeroObject(hv);
	}
	else
		Env::LoadVar_T(cpk, hv);

	Pipe::MsgHdr::Key km;
	km.m_iCheckpoint_BE = cpk.m_iCheckpoint_BE;
	km.m_iMsg_BE = Utils::FromBE(so.m_Checkpoint.m_iMsg);
	Env::SaveVar(&km, sizeof(km), pMsg, nSize);

	UpdateState(hv, pMsg, nSize);
	Env::SaveVar_T(cpk, hv);

	so.m_Checkpoint.m_iMsg++;
	Env::SaveVar_T(ko, so);
}

struct MyParser
{
	const uint8_t* m_pPtr;
	MyParser(const void* p) :m_pPtr((const uint8_t*) p) {}

	const uint8_t* Read(uint32_t n)
	{
		auto pVal = m_pPtr;
		m_pPtr += n;
		return pVal;
	}

	template <typename T>
	const T& Read_As()
	{
		return *reinterpret_cast<const T*>(Read(sizeof(T)));
	}
};

void OnHdr(Pipe::VariantHdr::Key& vhk, const BlockHeader::Full& hdr, const Pipe::StateIn& si)
{
	Env::Halt_if(!hdr.m_Height); // for more safety

	if (!si.m_Cfg.m_FakePoW)
		Env::Halt_if(!hdr.IsValid(&si.m_Cfg.m_RulesRemote));

	Pipe::VariantHdr vh;
	hdr.get_Hash(vh.m_hv, &si.m_Cfg.m_RulesRemote);
	vh.m_ChainWork.FromBE_T(hdr.m_ChainWork);

	vhk.m_Height = hdr.m_Height;

	Env::SaveVar_T(vhk, vh);
}

void EvaluateVariant(Pipe::Variant::Key& k, const Pipe::Variant& v, uint32_t nSize)
{
	uint32_t nMsgs = (nSize - sizeof(v)) / sizeof(HashValue);
	auto* pMsgs = (const HashValue*) (&v + 1);

	for (uint32_t i = 0; i < nMsgs; i++)
		UpdateState(k.m_hv, pMsgs[i]);

}

export void Method_4(const Pipe::PushRemote0& r)
{
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Height h = Env::get_Height();
	MyParser p(&r + 1);

	Pipe::Variant::Key kVar;
	Pipe::Variant* pVar;
	uint32_t nSizeVar;

	bool bNewVariant = !!(Pipe::PushRemote0::Flags::Msgs & r.m_Flags);

	if (bNewVariant)
	{
		// new variant
		auto nMsgs = p.Read_As<uint32_t>();
		Env::Halt_if(!nMsgs);

		uint32_t nSizeMsgs = sizeof(HashValue) * nMsgs;
		nSizeVar = sizeof(*pVar) + nSizeMsgs;

		pVar = (Pipe::Variant*) Env::StackAlloc(nSizeVar);
		Utils::ZeroObject(*pVar);

		pVar->m_iDispute = si.m_Dispute.m_iIdx;
		Utils::Copy(pVar->m_Cp.m_User, r.m_User);

		Env::Memcpy(pVar + 1, p.Read(nSizeMsgs), nSizeMsgs);

		Utils::ZeroObject(kVar.m_hv);
	}
	else
	{
		// load
		Utils::Copy(kVar.m_hv, p.Read_As<HashValue>());

		nSizeVar = Env::LoadVar(&kVar, sizeof(kVar), nullptr, 0);
		Env::Halt_if(nSizeVar < sizeof(*pVar));

		pVar = (Pipe::Variant*) Env::StackAlloc(nSizeVar);
		Env::LoadVar(&kVar, sizeof(kVar), pVar, nSizeVar);

		Env::Halt_if(pVar->m_iDispute != si.m_Dispute.m_iIdx);

		if (Utils::Cmp(pVar->m_Cp.m_User, r.m_User))
		{
			Env::Halt_if(h - pVar->m_hLastLoose <= si.m_Cfg.m_hContenderWaitPeriod);
			// replace the contender user for this variant
			Utils::Copy(pVar->m_Cp.m_User, r.m_User);
		}
	}

	if (!si.m_Dispute.m_Variants)
	{
		// I'm the 1st
		Utils::ZeroObject(kVar.m_hv);
	}
	else
	{
		// load the rival variant
		Pipe::Variant::Key kRival;
		Utils::Copy(kRival.m_hv, si.m_Dispute.m_hvBestVariant);

		uint32_t nSizeRival = Env::LoadVar(&kRival, sizeof(kRival), nullptr, 0);
		Pipe::Variant* pRival = (Pipe::Variant*) Env::StackAlloc(nSizeRival);
		Env::LoadVar(&kRival, sizeof(kRival), pRival, nSizeRival);

		if (1 == si.m_Dispute.m_Variants)
		{
			Env::DelVar_T(kRival);
			EvaluateVariant(kRival, *pRival, nSizeRival);
			Env::SaveVar(&kRival, sizeof(kRival), pRival, nSizeRival);
		}

		if (bNewVariant)
			EvaluateVariant(kVar, *pVar, nSizeVar);

		Env::Halt_if(!Utils::Cmp(kVar.m_hv, kRival.m_hv));

		Pipe::VariantHdr::Key vhk;
		Utils::Copy(vhk.m_hv, kVar.m_hv);
		

		bool bMustBringHdr0 = !pVar->m_hMax;
		Env::Halt_if(bMustBringHdr0 == !(Pipe::PushRemote0::Flags::Hdr0 & r.m_Flags));

		if (bMustBringHdr0)
		{
			const auto& hdr = p.Read_As<BlockHeader::Full>();
			OnHdr(vhk, hdr, si);

			pVar->m_hMin = hdr.m_Height;
			pVar->m_hMax = hdr.m_Height;
			BeamDifficulty::Unpack(pVar->m_Work, hdr.m_PoW.m_Difficulty);

			Pipe::OutCheckpoint::Key cpk;
			cpk.m_iCheckpoint_BE = Utils::FromBE(si.m_Dispute.m_iIdx);

			HashValue hv;
			Merkle::get_ContractVarHash(hv, si.m_cidRemote, KeyTag::Internal, &cpk, sizeof(cpk), &kVar.m_hv, sizeof(kVar.m_hv));

			auto n = p.Read_As<uint32_t>();
			while (n--)
				Merkle::Interpret(hv, p.Read_As<Merkle::Node>());

			Env::Halt_if(Utils::Cmp(hv, hdr.m_Definition));
		}

		if (Pipe::PushRemote0::Flags::HdrsDown & r.m_Flags)
		{
			// TODO
		}

		if (Pipe::PushRemote0::Flags::HdrsUp & r.m_Flags)
		{
			// TODO
		}

		Env::Halt_if(pRival->m_Work >= pVar->m_Work); // TODO: compensate for shared part

		pRival->m_hLastLoose = h;
		Env::SaveVar(&kRival, sizeof(kRival), pRival, nSizeRival);

		Utils::Copy(si.m_Dispute.m_hvBestVariant, kVar.m_hv);
	}

	Env::SaveVar(&kVar, sizeof(kVar), pVar, nSizeVar);

	si.m_Dispute.m_Height = h;

	if (bNewVariant)
	{
		si.m_Dispute.m_Variants++;
		si.m_Dispute.m_Stake += si.m_Cfg.m_StakeForRemote; // no need to Strict::Add
		Env::FundsLock(0, si.m_Cfg.m_StakeForRemote);
	}

	Env::SaveVar_T(ki, si);

	Env::AddSig(r.m_User);
}

export void Method_5(const Pipe::FinalyzeRemote& r)
{
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Env::Halt_if(!si.m_Dispute.m_Variants || (si.m_Dispute.m_Height - Env::get_Height() < si.m_Cfg.m_hDisputePeriod));

	Pipe::Variant::Key kVar;
	Utils::Copy(kVar.m_hv, si.m_Dispute.m_hvBestVariant);

	uint32_t nSizeVar = Env::LoadVar(&kVar, sizeof(kVar), nullptr, 0);
	Pipe::Variant* pVar = (Pipe::Variant*) Env::StackAlloc(nSizeVar);
	Env::LoadVar(&kVar, sizeof(kVar), pVar, nSizeVar);
	Env::DelVar_T(kVar);

	Env::FundsUnlock(0, si.m_Dispute.m_Stake);
	Env::AddSig(pVar->m_Cp.m_User);

	Pipe::InpCheckpointHdr::Key cpk;
	cpk.m_iCheckpoint = si.m_Dispute.m_iIdx;

	const uint32_t nSizeDelta = sizeof(Pipe::Variant) - sizeof(Pipe::InpCheckpointHdr);
	Env::SaveVar(&cpk, sizeof(cpk), &pVar->m_Cp, nSizeVar - nSizeDelta);

	Utils::ZeroObject(si.m_Dispute);
	si.m_Dispute.m_iIdx = cpk.m_iCheckpoint + 1;
	Env::SaveVar_T(ki, si);
}

export void Method_6(const Pipe::VerifyRemote0& r)
{
	uint32_t nSize = r.m_MsgSize + sizeof(Pipe::MsgHdr);
	auto* pMsg = (Pipe::MsgHdr*) Env::StackAlloc(nSize);

	if (r.m_Public)
		Utils::ZeroObject(pMsg->m_Receiver);
	else
		Env::get_CallerCid(1, pMsg->m_Receiver);

	Utils::Copy(pMsg->m_Sender, r.m_Sender);
	pMsg->m_Height = r.m_Height;

	Env::Memcpy(pMsg + 1, &r + 1, r.m_MsgSize);

	HashValue hv;
	get_MsgHash(hv, pMsg, nSize);

	Env::StackFree(nSize);

	nSize = sizeof(Pipe::InpCheckpointHdr) + sizeof(HashValue) * (r.m_iMsg + 1);
	auto pInpCp = (Pipe::InpCheckpointHdr*) Env::StackAlloc(nSize);

	Pipe::InpCheckpointHdr::Key cpk;
	cpk.m_iCheckpoint = r.m_iCheckpoint;

	auto nSizeActual = Env::LoadVar(&cpk, sizeof(cpk), pInpCp, nSize);
	Env::Halt_if(nSizeActual < nSize);

	auto* pHash = (const HashValue*) (pInpCp + 1);
	Env::Halt_if(Utils::Cmp(hv, pHash[r.m_iMsg]));

	// message verified
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Env::FundsLock(0, si.m_Cfg.m_ComissionPerMsg);

	Pipe::UserInfo ui;
	Pipe::UserInfo::Key kui;
	Utils::Copy(kui.m_Pk, pInpCp->m_User);

	if (!Env::LoadVar_T(kui, ui))
		Utils::ZeroObject(ui);

	Strict::Add(ui.m_Balance, si.m_Cfg.m_ComissionPerMsg);
	Env::SaveVar_T(kui, ui);
}
