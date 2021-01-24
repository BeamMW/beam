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
	pMsg->get_Hash(hvMsg, nMsg);
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
		if (so.IsCheckpointClosed(h))
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

struct VariantWrap
{
	Pipe::Variant::Key m_Key;
	Pipe::Variant* m_pVar = nullptr;
	uint32_t m_VarSize;

	struct Hdr
	{
		Pipe::VariantHdr::Key m_Key;
		Pipe::VariantHdr m_Data;

	} m_Hdr;

	~VariantWrap()
	{
		if (m_pVar)
			Env::Heap_Free(m_pVar);
	}

	void Alloc()
	{
		Env::Halt_if(m_VarSize < sizeof(*m_pVar));
		m_pVar = (Pipe::Variant*) Env::Heap_Alloc(m_VarSize);
	}

	void Evaluate()
	{
		uint32_t nMsgs = (m_VarSize - sizeof(*m_pVar)) / sizeof(HashValue);
		auto* pMsgs = (const HashValue*) (m_pVar + 1);

		for (uint32_t i = 0; i < nMsgs; i++)
			UpdateState(m_Key.m_hvVariant, pMsgs[i]);
	}

	void Load()
	{
		m_VarSize = Env::LoadVar(&m_Key, sizeof(m_Key), nullptr, 0);
		Alloc();
		Env::LoadVar(&m_Key, sizeof(m_Key), m_pVar, m_VarSize);
	}

	void Save()
	{
		Env::SaveVar(&m_Key, sizeof(m_Key), m_pVar, m_VarSize);
	}

	void Del()
	{
		Env::DelVar_T(m_Key);
	}

	bool HasHdrs() const
	{
		return !!m_pVar->m_Begin.m_Height;
	}

	void OnHdr(const BlockHeader::Full& hdr, const Pipe::StateIn& si)
	{
		Env::Halt_if(!hdr.m_Height); // for more safety, we use m_Top.m_Height as indicator

		if (!si.m_Cfg.m_FakePoW)
			Env::Halt_if(!hdr.IsValid(&si.m_Cfg.m_RulesRemote));

		hdr.get_Hash(m_Hdr.m_Data.m_hvHeader, &si.m_Cfg.m_RulesRemote);
		m_Hdr.m_Key.m_Height = hdr.m_Height;

		Env::SaveVar_T(m_Hdr.m_Key, m_Hdr.m_Data);
	}

	void SetBegin(const BlockHeader::Full& hdr, const BeamDifficulty::Raw& wrk)
	{
		auto& x = m_pVar->m_Begin;
		x.m_Height = m_Hdr.m_Key.m_Height;
		Utils::Copy(x.m_hvPrev, hdr.m_Prev);

		BeamDifficulty::Raw d;
		BeamDifficulty::Unpack(d, hdr.m_PoW.m_Difficulty);
		x.m_Work = wrk - d;
	}

	void SetEnd()
	{
		auto& x = m_pVar->m_End;
		x.m_Height = m_Hdr.m_Key.m_Height + 1;
		Utils::Copy(x.m_hvPrev, m_Hdr.m_Data.m_hvHeader);
	}

	void SetEnd(const BeamDifficulty::Raw& wrk)
	{
		SetEnd();
		Utils::Copy(m_pVar->m_End.m_Work, wrk);
	}

	void LoadHdr(Height h)
	{
		m_Hdr.m_Key.m_Height = h;
		Env::LoadVar_T(m_Hdr.m_Key, m_Hdr.m_Data);
	}

	void EnsureBetter(VariantWrap& vw2)
	{
		const auto& v = *m_pVar;
		const auto& v2 = *vw2.m_pVar;

		bool bIntersect =
			(v.m_End.m_Height > v2.m_Begin.m_Height) &&
			(v2.m_End.m_Height > v.m_Begin.m_Height);

		if (bIntersect)
		{
			// Is there a collusion on top?
			Height h = std::min(v.m_End.m_Height, v.m_End.m_Height) - 1;

			LoadHdr(h);
			vw2.LoadHdr(h);

			if (!Utils::Cmp(m_Hdr.m_Data.m_hvHeader, vw2.m_Hdr.m_Data.m_hvHeader))
			{
				// yes! No need to compare work. The winner is the bigger variant.
				Env::Halt_if(m_VarSize <= vw2.m_VarSize);
				return;
			}

			// Is there a collusion on bottom?
			h = std::max(v.m_Begin.m_Height, v2.m_Begin.m_Height);

			LoadHdr(h);
			vw2.LoadHdr(h);

			if (!Utils::Cmp(m_Hdr.m_Data.m_hvHeader, vw2.m_Hdr.m_Data.m_hvHeader))
			{
				// yes! Compare only the top chainwork
				Env::Halt_if(v.m_End.m_Work <= v2.m_End.m_Work);
				return;
			}
		}

		// standard case. Compare the net proven work
		Env::Halt_if(v.m_End.m_Work - v.m_Begin.m_Work <= v2.m_End.m_Work - v2.m_Begin.m_Work);
	}
};

export void Method_4(const Pipe::PushRemote0& r)
{
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Height h = Env::get_Height();
	MyParser p(&r + 1);

	VariantWrap vw;

	bool bNewVariant = !!(Pipe::PushRemote0::Flags::Msgs & r.m_Flags);
	if (bNewVariant)
	{
		// new variant
		auto nMsgs = p.Read_As<uint32_t>();
		Env::Halt_if(!nMsgs);

		uint32_t nSizeMsgs = sizeof(HashValue) * nMsgs;
		vw.m_VarSize = sizeof(*vw.m_pVar) + nSizeMsgs;

		vw.Alloc();
		Utils::ZeroObject(*vw.m_pVar);

		vw.m_pVar->m_iDispute = si.m_Dispute.m_iIdx;
		Utils::Copy(vw.m_pVar->m_Cp.m_User, r.m_User);

		Env::Memcpy(vw.m_pVar + 1, p.Read(nSizeMsgs), nSizeMsgs);
	}
	else
	{
		// load
		Utils::Copy(vw.m_Key.m_hvVariant, p.Read_As<HashValue>());
		vw.Load();

		Env::Halt_if(vw.m_pVar->m_iDispute != si.m_Dispute.m_iIdx);

		if (Utils::Cmp(vw.m_pVar->m_Cp.m_User, r.m_User))
		{
			Env::Halt_if(h - vw.m_pVar->m_hLastLoose <= si.m_Cfg.m_hContenderWaitPeriod);
			// replace the contender user for this variant
			Utils::Copy(vw.m_pVar->m_Cp.m_User, r.m_User);
		}
	}

	if (!si.m_Dispute.m_Variants)
	{
		// I'm the 1st
		Utils::ZeroObject(vw.m_Key.m_hvVariant);
	}
	else
	{
		// load the rival variant
		VariantWrap vwRival;
		Utils::Copy(vwRival.m_Key.m_hvVariant, si.m_Dispute.m_hvBestVariant);
		vwRival.Load();

		if (1 == si.m_Dispute.m_Variants)
		{
			vwRival.Del();
			vwRival.Evaluate();
		}

		if (bNewVariant)
			vw.Evaluate();
		Utils::Copy(vw.m_Hdr.m_Key.m_hvVariant, vw.m_Key.m_hvVariant);

		Env::Halt_if(!Utils::Cmp(vw.m_Key.m_hvVariant, vwRival.m_Key.m_hvVariant));

		if (Pipe::PushRemote0::Flags::Reset & r.m_Flags)
			vw.m_pVar->m_Begin.m_Height = 0;

		bool bMustBringHdr0 = !vw.HasHdrs();
		Env::Halt_if(bMustBringHdr0 == !(Pipe::PushRemote0::Flags::Hdr0 & r.m_Flags));

		if (bMustBringHdr0)
		{
			const auto& hdr = p.Read_As<BlockHeader::Full>();
			vw.OnHdr(hdr, si);

			BeamDifficulty::Raw wrk;
			wrk.FromBE_T(hdr.m_ChainWork);

			vw.SetBegin(hdr, wrk);
			vw.SetEnd(wrk);

			Pipe::OutCheckpoint::Key cpk;
			cpk.m_iCheckpoint_BE = Utils::FromBE(si.m_Dispute.m_iIdx);

			HashValue hv;
			Merkle::get_ContractVarHash(hv, si.m_cidRemote, KeyTag::Internal, &cpk, sizeof(cpk), &vw.m_Key.m_hvVariant, sizeof(vw.m_Key.m_hvVariant));

			auto n = p.Read_As<uint32_t>();
			while (n--)
				Merkle::Interpret(hv, p.Read_As<Merkle::Node>());

			Env::Halt_if(Utils::Cmp(hv, hdr.m_Definition));
		}

		if (Pipe::PushRemote0::Flags::HdrsDown & r.m_Flags)
		{
			BlockHeader::Full hdr;
			Utils::Copy(hdr, p.Read_As<BlockHeader::Full>());

			BeamDifficulty::Raw wrk;
			wrk.FromBE_T(hdr.m_ChainWork);

			Pipe::Variant::Ending vb;
			Utils::Copy(vb, vw.m_pVar->m_Begin);
			Env::Halt_if(hdr.m_Height >= vb.m_Height);

			vw.SetBegin(hdr, wrk);

			while (true)
			{
				vw.OnHdr(hdr, si);

				if (++hdr.m_Height == vb.m_Height)
				{
					Env::Halt_if(
						Utils::Cmp(vw.m_Hdr.m_Data.m_hvHeader, vb.m_hvPrev) ||
						Utils::Cmp(wrk, vb.m_Work));

					break;
				}

				Utils::Copy(hdr.m_Prev, vw.m_Hdr.m_Data.m_hvHeader);

				BeamDifficulty::Add(wrk, hdr.m_PoW.m_Difficulty);
				wrk.ToBE_T(hdr.m_ChainWork);

				Utils::Copy(Cast::Down<BlockHeader::Element>(hdr), p.Read_As<BlockHeader::Element>());
			}
		}

		if (Pipe::PushRemote0::Flags::HdrsUp & r.m_Flags)
		{
			BlockHeader::Full hdr;
			hdr.m_Height = vw.m_pVar->m_End.m_Height;
			Utils::Copy(hdr.m_Prev, vw.m_pVar->m_End.m_hvPrev);

			for (auto n = p.Read_As<uint32_t>(); ; )
			{
				Utils::Copy(Cast::Down<BlockHeader::Element>(hdr), p.Read_As<BlockHeader::Element>());

				BeamDifficulty::Add(vw.m_pVar->m_End.m_Work, hdr.m_PoW.m_Difficulty);
				vw.m_pVar->m_End.m_Work.ToBE_T(hdr.m_ChainWork);

				vw.OnHdr(hdr, si);

				if (! --n)
					break;

				Utils::Copy(hdr.m_Prev, vw.m_Hdr.m_Data.m_hvHeader);
				hdr.m_Height++;
			}

			vw.SetEnd();
		}

		if (vwRival.HasHdrs())
		{
			// Compare the variants.
			Utils::Copy(vwRival.m_Hdr.m_Key.m_hvVariant, vwRival.m_Key.m_hvVariant);

			vw.EnsureBetter(vwRival);
		}

		vwRival.m_pVar->m_hLastLoose = h;
		vwRival.Save();

		Utils::Copy(si.m_Dispute.m_hvBestVariant, vw.m_Key.m_hvVariant);
	}

	vw.Save();

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

void HandleUserAccount(const PubKey& pk, Amount val, bool bAdd)
{
	Pipe::UserInfo ui;
	Pipe::UserInfo::Key kui;
	Utils::Copy(kui.m_Pk, pk);

	if (!Env::LoadVar_T(kui, ui))
		ui.m_Balance = 0;

	if (bAdd)
		Strict::Add(ui.m_Balance, val);
	else
		Strict::Sub(ui.m_Balance, val);

	if (ui.m_Balance)
		Env::SaveVar_T(kui, ui);
	else
		Env::DelVar_T(kui);
}

export void Method_5(const Pipe::FinalyzeRemote& r)
{
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Env::Halt_if(!si.m_Dispute.m_Variants || (si.m_Dispute.m_Height - Env::get_Height() < si.m_Cfg.m_hDisputePeriod));

	VariantWrap vw;
	Utils::Copy(vw.m_Key.m_hvVariant, si.m_Dispute.m_hvBestVariant);
	vw.Load();
	vw.Del();

	if (r.m_DepositStake)
		HandleUserAccount(vw.m_pVar->m_Cp.m_User, si.m_Dispute.m_Stake, true);
	else
	{
		Env::FundsUnlock(0, si.m_Dispute.m_Stake);
		Env::AddSig(vw.m_pVar->m_Cp.m_User);
	}

	Pipe::InpCheckpointHdr::Key cpk;
	cpk.m_iCheckpoint = si.m_Dispute.m_iIdx;

	const uint32_t nSizeDelta = sizeof(Pipe::Variant) - sizeof(Pipe::InpCheckpointHdr);
	Env::SaveVar(&cpk, sizeof(cpk), &vw.m_pVar->m_Cp, vw.m_VarSize - nSizeDelta);

	Utils::ZeroObject(si.m_Dispute);
	si.m_Dispute.m_iIdx = cpk.m_iCheckpoint + 1;
	Env::SaveVar_T(ki, si);
}

export void Method_6(const Pipe::VerifyRemote0& r)
{
	uint32_t nSize = r.m_MsgSize + sizeof(Pipe::MsgHdr);
	auto* pMsg = (Pipe::MsgHdr*) Env::StackAlloc(nSize);

	if (r.m_Public)
	{
		Env::Halt_if(r.m_Wipe); // only designated messages can be wiped
		Utils::ZeroObject(pMsg->m_Receiver);
	}
	else
		Env::get_CallerCid(1, pMsg->m_Receiver);

	Utils::Copy(pMsg->m_Sender, r.m_Sender);
	pMsg->m_Height = r.m_Height;

	Env::Memcpy(pMsg + 1, &r + 1, r.m_MsgSize);

	HashValue hv;
	pMsg->get_Hash(hv, nSize);

	Env::StackFree(nSize);

	Pipe::InpCheckpointHdr::Key cpk;
	cpk.m_iCheckpoint = r.m_iCheckpoint;

	if (r.m_Wipe)
		// must read all the messages anyway
		nSize = Env::LoadVar(&cpk, sizeof(cpk), nullptr, 0);
	else
		nSize = sizeof(Pipe::InpCheckpointHdr) + sizeof(HashValue) * (r.m_iMsg + 1); // read up to including the message

	auto pInpCp = (Pipe::InpCheckpointHdr*) Env::StackAlloc(nSize);

	auto nSizeActual = Env::LoadVar(&cpk, sizeof(cpk), pInpCp, nSize);
	Env::Halt_if(nSizeActual < nSize);

	auto& hv2 = ((HashValue*) (pInpCp + 1))[r.m_iMsg];
	Env::Halt_if(Utils::Cmp(hv, hv2));

	if (r.m_Wipe)
	{
		Utils::ZeroObject(hv2);
		Env::SaveVar(&cpk, sizeof(cpk), pInpCp, nSize);
	}

	// message verified
	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Env::FundsLock(0, si.m_Cfg.m_ComissionPerMsg);

	HandleUserAccount(pInpCp->m_User, si.m_Cfg.m_ComissionPerMsg, true);
}

export void Method_7(const Pipe::Withdraw& r)
{
	HandleUserAccount(r.m_User, r.m_Amount, false);
	Env::FundsUnlock(0, r.m_Amount);
	Env::AddSig(r.m_User);
}
