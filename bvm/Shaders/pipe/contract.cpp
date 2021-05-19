#include "../common.h"
#include "contract.h"
#include "../BeamHeader.h"

export void Ctor(const Pipe::Create& r)
{
	Pipe::StateOut so;
	_POD_(so).SetZero();
	_POD_(so.m_Cfg) = r.m_Cfg.m_Out;
	
	Pipe::StateOut::Key ko;
	Env::SaveVar_T(ko, so);

	Pipe::StateIn si;
	_POD_(si).SetZero();
	_POD_(si.m_Cfg) = r.m_Cfg.m_In;

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

	Env::Halt_if(!_POD_(si.m_cidRemote).IsZero() || _POD_(r.m_cid).IsZero());

	_POD_(si.m_cidRemote) = r.m_cid;
	Env::SaveVar_T(ki, si);

}

export void Method_3(const Pipe::PushLocal0& r)
{
	Height h = Env::get_Height();

	uint32_t nSize = r.m_MsgSize + sizeof(Pipe::MsgHdr);
	auto* pMsg = (Pipe::MsgHdr*) Env::StackAlloc(nSize);

	if (Env::get_CallDepth() > 1)
		Env::get_CallerCid(1, pMsg->m_Sender);
	else
		_POD_(pMsg->m_Sender).SetZero();

	_POD_(pMsg->m_Receiver) = r.m_Receiver;
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
		_POD_(hv).SetZero();
	}
	else
		Env::LoadVar_T(cpk, hv);

	Pipe::MsgHdr::KeyOut km;
	km.m_iCheckpoint_BE = cpk.m_iCheckpoint_BE;
	km.m_iMsg_BE = Utils::FromBE(so.m_Checkpoint.m_iMsg);
	Env::SaveVar(&km, sizeof(km), pMsg, nSize, KeyTag::Internal);

	pMsg->UpdateState(hv, nSize);
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

	struct MsgParser
	{
		MyParser m_Pos;
		const uint8_t* m_pEnd;

		const Pipe::MsgHdr* m_pMsg;
		uint32_t m_MsgSize;

		static void TestSizeIsSane(uint32_t nSize)
		{
			const uint32_t nThreshold = 0x10000000;
			Env::Halt_if(nSize >= nThreshold);
		}

		MsgParser(const VariantWrap& vw)
			:m_Pos(vw.m_pVar + 1)
		{
			m_pEnd = m_Pos.m_pPtr + (vw.m_VarSize - sizeof(*vw.m_pVar));
		}

		bool MoveNext()
		{
			if (m_Pos.m_pPtr == m_pEnd)
				return false;

			m_MsgSize = m_Pos.Read_As<uint32_t>();
			TestSizeIsSane(m_MsgSize);

			m_pMsg = &m_Pos.Read_As<Pipe::MsgHdr>();
			m_Pos.Read(m_MsgSize);

			Env::Halt_if(m_Pos.m_pPtr > m_pEnd);

			return true;
		}

		void Evaluate(HashValue& res)
		{
			while (MoveNext())
				m_pMsg->UpdateState(res, sizeof(*m_pMsg) + m_MsgSize);
		}
	};

	void Evaluate()
	{
		MsgParser p(*this);
		p.Evaluate(m_Key.m_hvVariant);
	}

	void Load()
	{
		m_VarSize = Env::LoadVar(&m_Key, sizeof(m_Key), nullptr, 0, KeyTag::Internal);
		Alloc();
		Env::LoadVar(&m_Key, sizeof(m_Key), m_pVar, m_VarSize, KeyTag::Internal);
	}

	void Save()
	{
		Env::SaveVar(&m_Key, sizeof(m_Key), m_pVar, m_VarSize, KeyTag::Internal);
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
		_POD_(x.m_hvPrev) = hdr.m_Prev;

		BeamDifficulty::Raw d;
		BeamDifficulty::Unpack(d, hdr.m_PoW.m_Difficulty);
		x.m_Work = wrk - d;
	}

	void SetEnd()
	{
		auto& x = m_pVar->m_End;
		x.m_Height = m_Hdr.m_Key.m_Height + 1;
		_POD_(x.m_hvPrev) = m_Hdr.m_Data.m_hvHeader;
	}

	void SetEnd(const BeamDifficulty::Raw& wrk)
	{
		SetEnd();
		_POD_(m_pVar->m_End.m_Work) = wrk;
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

			if (_POD_(m_Hdr.m_Data.m_hvHeader) == vw2.m_Hdr.m_Data.m_hvHeader)
			{
				// yes! No need to compare work. The winner is the bigger variant.
				Env::Halt_if(m_VarSize <= vw2.m_VarSize);
				return;
			}

			// Is there a collusion on bottom?
			h = std::max(v.m_Begin.m_Height, v2.m_Begin.m_Height);

			LoadHdr(h);
			vw2.LoadHdr(h);

			if (_POD_(m_Hdr.m_Data.m_hvHeader) == vw2.m_Hdr.m_Data.m_hvHeader)
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
		auto nSizeMsgs = p.Read_As<uint32_t>();
		VariantWrap::MsgParser::TestSizeIsSane(nSizeMsgs);

		vw.m_VarSize = sizeof(*vw.m_pVar) + nSizeMsgs;

		vw.Alloc();
		_POD_(*vw.m_pVar).SetZero();

		vw.m_pVar->m_iDispute = si.m_Dispute.m_iIdx;
		_POD_(vw.m_pVar->m_User) = r.m_User;

		Env::Memcpy(vw.m_pVar + 1, p.Read(nSizeMsgs), nSizeMsgs);

		_POD_(vw.m_Key.m_hvVariant).SetZero();

	}
	else
	{
		// load
		_POD_(vw.m_Key.m_hvVariant) = p.Read_As<HashValue>();
		vw.Load();

		Env::Halt_if(vw.m_pVar->m_iDispute != si.m_Dispute.m_iIdx);

		if (_POD_(vw.m_pVar->m_User) != r.m_User)
		{
			Env::Halt_if(h - vw.m_pVar->m_hLastLoose <= si.m_Cfg.m_hContenderWaitPeriod);
			// replace the contender user for this variant
			_POD_(vw.m_pVar->m_User) = r.m_User;
		}
	}

	if (!si.m_Dispute.m_Variants)
	{
		// I'm the 1st
		// no need to evaluate variant, just ensure sanity
		VariantWrap::MsgParser mp(vw);
		while (mp.MoveNext())
			;
	}
	else
	{
		// load the rival variant
		VariantWrap vwRival;
		_POD_(vwRival.m_Key.m_hvVariant) = si.m_Dispute.m_hvBestVariant;
		vwRival.Load();

		if (1 == si.m_Dispute.m_Variants)
		{
			vwRival.Del();
			vwRival.Evaluate();
		}

		if (bNewVariant)
			vw.Evaluate();
		Env::Halt_if(_POD_(vw.m_Key.m_hvVariant) == vwRival.m_Key.m_hvVariant);

		_POD_(vw.m_Hdr.m_Key.m_hvVariant) = vw.m_Key.m_hvVariant;

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

			Env::Halt_if(_POD_(hv) != hdr.m_Definition);
		}

		if (Pipe::PushRemote0::Flags::HdrsDown & r.m_Flags)
		{
			BlockHeader::Full hdr;
			_POD_(hdr) = p.Read_As<BlockHeader::Full>();

			BeamDifficulty::Raw wrk;
			wrk.FromBE_T(hdr.m_ChainWork);

			Pipe::Variant::Ending vb;
			_POD_(vb) = vw.m_pVar->m_Begin;
			Env::Halt_if(hdr.m_Height >= vb.m_Height);

			vw.SetBegin(hdr, wrk);

			while (true)
			{
				vw.OnHdr(hdr, si);

				if (++hdr.m_Height == vb.m_Height)
				{
					Env::Halt_if(
						(_POD_(vw.m_Hdr.m_Data.m_hvHeader) != vb.m_hvPrev) ||
						(_POD_(wrk) != vb.m_Work));

					break;
				}

				_POD_(hdr.m_Prev) = vw.m_Hdr.m_Data.m_hvHeader;

				BeamDifficulty::Add(wrk, hdr.m_PoW.m_Difficulty);
				wrk.ToBE_T(hdr.m_ChainWork);

				_POD_(Cast::Down<BlockHeader::Element>(hdr)) = p.Read_As<BlockHeader::Element>();
			}
		}

		if (Pipe::PushRemote0::Flags::HdrsUp & r.m_Flags)
		{
			BlockHeader::Full hdr;
			hdr.m_Height = vw.m_pVar->m_End.m_Height;
			_POD_(hdr.m_Prev) = vw.m_pVar->m_End.m_hvPrev;

			for (auto n = p.Read_As<uint32_t>(); ; )
			{
				_POD_(Cast::Down<BlockHeader::Element>(hdr)) = p.Read_As<BlockHeader::Element>();

				BeamDifficulty::Add(vw.m_pVar->m_End.m_Work, hdr.m_PoW.m_Difficulty);
				vw.m_pVar->m_End.m_Work.ToBE_T(hdr.m_ChainWork);

				vw.OnHdr(hdr, si);

				if (! --n)
					break;

				_POD_(hdr.m_Prev) = vw.m_Hdr.m_Data.m_hvHeader;
				hdr.m_Height++;
			}

			vw.SetEnd();
		}

		if (vwRival.HasHdrs())
		{
			// Compare the variants.
			_POD_(vwRival.m_Hdr.m_Key.m_hvVariant) = vwRival.m_Key.m_hvVariant;

			vw.EnsureBetter(vwRival);
		}

		vwRival.m_pVar->m_hLastLoose = h;
		vwRival.Save();

		_POD_(si.m_Dispute.m_hvBestVariant) = vw.m_Key.m_hvVariant;
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
	_POD_(kui.m_Pk) = pk;

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

	Env::Halt_if(!si.m_Dispute.m_Variants || !si.CanFinalyze(Env::get_Height()));

	VariantWrap vw;
	_POD_(vw.m_Key.m_hvVariant) = si.m_Dispute.m_hvBestVariant;
	vw.Load();
	vw.Del();

	Pipe::InpCheckpoint cp;
	_POD_(cp.m_User) = vw.m_pVar->m_User;

	if (r.m_DepositStake)
		HandleUserAccount(cp.m_User, si.m_Dispute.m_Stake, true);
	else
	{
		Env::FundsUnlock(0, si.m_Dispute.m_Stake);
		Env::AddSig(cp.m_User);
	}

	uint32_t iIdx = si.m_Dispute.m_iIdx;
	Pipe::InpCheckpoint::Key cpk;
	cpk.m_iCheckpoint_BE = Utils::FromBE(iIdx);
	Env::SaveVar_T(cpk, cp);

	Pipe::MsgHdr::KeyIn mki;
	mki.m_iCheckpoint_BE = cpk.m_iCheckpoint_BE;

	VariantWrap::MsgParser p(vw);
	for (uint32_t iMsg = 0; p.MoveNext(); iMsg++)
	{
		mki.m_iMsg_BE = Utils::FromBE(iMsg);
		Env::SaveVar(&mki, sizeof(mki), p.m_pMsg, sizeof(*p.m_pMsg) + p.m_MsgSize, KeyTag::Internal);
	}

	_POD_(si.m_Dispute).SetZero();
	si.m_Dispute.m_iIdx = iIdx + 1;
	Env::SaveVar_T(ki, si);
}

export void Method_6(Pipe::ReadRemote0& r)
{
	Pipe::MsgHdr::KeyIn mki;
	mki.m_iCheckpoint_BE = Utils::FromBE(r.m_iCheckpoint);
	mki.m_iMsg_BE = Utils::FromBE(r.m_iMsg);

	uint32_t nSize = Env::LoadVar(&mki, sizeof(mki), nullptr, 0, KeyTag::Internal);
	Env::Halt_if(nSize < sizeof(Pipe::MsgHdr));

	auto* pMsg = (Pipe::MsgHdr*) Env::StackAlloc(nSize);
	Env::LoadVar(&mki, sizeof(mki), pMsg, nSize, KeyTag::Internal);

	r.m_IsPrivate = !_POD_(pMsg->m_Receiver).IsZero();

	if (r.m_IsPrivate)
	{
		// check the recipient
		ContractID cid;
		Env::get_CallerCid(1, cid);
		Env::Halt_if(_POD_(cid) != pMsg->m_Receiver);

		if (r.m_Wipe)
			Env::DelVar_T(mki);
	}
	else
		Env::Halt_if(r.m_Wipe); // only designated messages can be wiped

	nSize -= sizeof(Pipe::MsgHdr);

	_POD_(r.m_Sender) = pMsg->m_Sender;
	Env::Memcpy(&r + 1, pMsg + 1, std::min(nSize, r.m_MsgSize));
	r.m_MsgSize = nSize;

	Pipe::InpCheckpoint::Key cpk;
	cpk.m_iCheckpoint_BE = mki.m_iCheckpoint_BE;

	Pipe::InpCheckpoint cp;
	Env::LoadVar_T(cpk, cp);

	Pipe::StateIn si;
	Pipe::StateIn::Key ki;
	Env::LoadVar_T(ki, si);

	Env::FundsLock(0, si.m_Cfg.m_ComissionPerMsg);

	HandleUserAccount(cp.m_User, si.m_Cfg.m_ComissionPerMsg, true);
}

export void Method_7(const Pipe::Withdraw& r)
{
	HandleUserAccount(r.m_User, r.m_Amount, false);
	Env::FundsUnlock(0, r.m_Amount);
	Env::AddSig(r.m_User);
}
