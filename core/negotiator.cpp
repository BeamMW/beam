// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "negotiator.h"
#include "ecc_native.h"

namespace beam {
namespace Negotiator {

namespace Gateway
{
	void Direct::Send(uint32_t code, ByteBuffer&& buf)
	{
		const uint32_t msk = (uint32_t(1) << 16) - 1;
		if ((code & msk) >= Codes::Private)
		{
			assert(false);
			return;
		}

		Blob blob;
		if (m_Peer.m_pStorage->Read(code, blob))
		{
			assert(false);
			return;
		}

		m_Peer.m_pStorage->Write(code, std::move(buf));
	}

} // namespace Gateway

namespace Storage
{
	void Map::Write(uint32_t code, ByteBuffer&& buf)
	{
		operator [](code) = std::move(buf);
	}

	bool Map::Read(uint32_t code, Blob& blob)
	{
		const_iterator it = find(code);
		if (end() == it)
			return false;

		blob = Blob(it->second);
		return true;
	}

} // namespace Storage

/////////////////////
// IBase::Router

IBase::Router::Router(Gateway::IBase* pG, Storage::IBase* pS, uint32_t iChannel, Negotiator::IBase& n)
	:m_iChannel(iChannel)
	,m_pG(pG)
	,m_pS(pS)
{
	n.m_pGateway = this;
	n.m_pStorage = this;
}

uint32_t IBase::Router::Remap(uint32_t code) const
{
	return code + (m_iChannel << 16);
}

void IBase::Router::Send(uint32_t code, ByteBuffer&& buf)
{
	m_pG->Send(Remap(code), std::move(buf));
}

void IBase::Router::Write(uint32_t code, ByteBuffer&& buf)
{
	m_pS->Write(Remap(code), std::move(buf));
}

bool IBase::Router::Read(uint32_t code, Blob& blob)
{
	return m_pS->Read(Remap(code), blob);
}

/////////////////////
// IBase

void IBase::OnFail()
{
	uint32_t val = Status::Error;
	Set(val, Codes::Status);
}

void IBase::OnDone()
{
	uint32_t val = Status::Success;
	Set(val, Codes::Status);
}

bool IBase::RaiseTo(uint32_t pos)
{
	if (m_Pos >= pos)
		return false;

	m_Pos = pos;
	return true;
}

uint32_t IBase::Update()
{
	uint32_t nStatus = Status::Pending;
	if (Get(nStatus, Codes::Status))
		return nStatus;

	uint32_t nPos = 0;
	Get(nPos, Codes::Position);
	m_Pos = nPos;

	Update2();

	if (Get(nStatus, Codes::Status) && (nStatus > Status::Success))
		return nStatus;

	assert(m_Pos >= nPos);
	if (m_Pos > nPos)
		Set(m_Pos, Codes::Position);

	return nStatus;
}

/////////////////////
// Multisig

struct Multisig::Impl
{
	struct PubKeyPlus
	{
		ECC::Point m_PubKey;
		ECC::Signature m_Sig;

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_PubKey
				& m_Sig;
		}

		void Set(const ECC::Scalar::Native& sk)
		{
			ECC::Hash::Value hv;
			get_Hash(hv);
			m_Sig.Sign(hv, sk);
		}

		bool IsValid(ECC::Point::Native& pkOut) const
		{
			if (!pkOut.Import(m_PubKey))
				return false;

			ECC::Hash::Value hv;
			get_Hash(hv);

			return m_Sig.IsValid(hv, pkOut);
		}

		void get_Hash(ECC::Hash::Value& hv) const
		{
			ECC::Hash::Processor() << m_PubKey >> hv;
		}
	};

	struct Part2Plus
		:public ECC::RangeProof::Confidential::Part2
	{
		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_T1
				& m_T2;
		}
	};

	struct MSigPlus
		:public ECC::RangeProof::Confidential::MultiSig
	{
		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_Part1.m_A
				& m_Part1.m_S
				& Cast::Up<Part2Plus>(m_Part2);
		}
	};
};


void Multisig::Update2()
{
	const Height hVer = MaxHeight;

	ECC::Key::IDV kidv;
	if (!Get(kidv, Codes::Kidv))
		return;

	ECC::Scalar::Native sk;
	m_pKdf->DeriveKey(sk, kidv);

	Output outp;
	if (!Get(outp.m_Commitment, Codes::Commitment))
	{
		ECC::Point::Native comm = ECC::Context::get().G * sk;

		Impl::PubKeyPlus pkp;

		if (RaiseTo(1))
		{
			pkp.m_PubKey = comm;
			pkp.Set(sk);
			Send(pkp, Codes::PubKeyPlus);
		}

		if (!Get(pkp, Codes::PubKeyPlus))
			return;

		ECC::Point::Native commPeer;
		if (!pkp.IsValid(commPeer))
		{
			OnFail();
			return;
		}

		comm += commPeer;
		comm += ECC::Context::get().H * kidv.m_Value;

		outp.m_Commitment = comm;
		Set(outp.m_Commitment, Codes::Commitment);
	}

	uint32_t iRole = 0;
	Get(iRole, Codes::Role);

	ECC::NoLeak<ECC::uintBig> seedSk;
	if (!Get(seedSk.V, Codes::Nonce))
	{
		ECC::GenRandom(seedSk.V);
		Set(seedSk.V, Codes::Nonce);
	}

	outp.m_pConfidential.reset(new ECC::RangeProof::Confidential);
	ECC::RangeProof::Confidential& bp = *outp.m_pConfidential;

	ECC::Oracle oracle, o2;
	outp.Prepare(oracle, hVer);

	uint32_t nShareRes = 0;
	Get(nShareRes, Codes::ShareResult);

	if (!iRole)
	{
		if (!Get(Cast::Up<Impl::Part2Plus>(bp.m_Part2), Codes::BpPart2))
			return;

		ECC::RangeProof::CreatorParams cp;
		cp.m_Kidv = kidv;
		outp.get_SeedKid(cp.m_Seed.V, *m_pKdf);

		o2 = oracle;
		if (!bp.CoSign(seedSk.V, sk, cp, o2, ECC::RangeProof::Confidential::Phase::Step2)) // add self p2, produce msig
		{
			OnFail();
			return;
		}

		if (RaiseTo(2))
		{
			Impl::MSigPlus msig;
			msig.m_Part1 = bp.m_Part1;
			msig.m_Part2 = bp.m_Part2;
			Send(msig, Codes::BpBothPart);
		}

		if (!Get(bp.m_Part3.m_TauX, Codes::BpPart3))
			return;

		o2 = oracle;
		if (!bp.CoSign(seedSk.V, sk, cp, o2, ECC::RangeProof::Confidential::Phase::Finalize))
		{
			OnFail();
			return;
		}

		if (nShareRes && RaiseTo(3))
			Send(bp, Codes::BpFull);
	}
	else
	{
		if (RaiseTo(2))
		{
			Impl::Part2Plus p2;
			ZeroObject(p2);
			if (!Impl::MSigPlus::CoSignPart(seedSk.V, p2))
			{
				OnFail();
				return;
			}

			Send(p2, Codes::BpPart2);
		}

		Impl::MSigPlus msig;
		if (!Get(msig, Codes::BpBothPart))
			return;

		if (RaiseTo(3))
		{
			ECC::RangeProof::Confidential::Part3 p3;
			ZeroObject(p3);

			o2 = oracle;
			msig.CoSignPart(seedSk.V, sk, o2, p3);

			Send(p3.m_TauX, Codes::BpPart3);
		}

		if (!nShareRes)
		{
			OnDone();
			return;
		}

		if (!Get(bp, Codes::BpFull))
			return;
	}

	ECC::Point::Native pt;
	if (!outp.IsValid(hVer, pt))
	{
		OnFail();
		return;
	}

	Set(outp, Codes::OutputTxo);
	OnDone();
}

/////////////////////
// MultiTx

void MultiTx::CalcInput(const Key::IDV& kidv, ECC::Scalar::Native& offs, ECC::Point& comm)
{
	SwitchCommitment sc;
	ECC::Scalar::Native sk;
	sc.Create(sk, comm, *m_pKdf, kidv);
	offs += sk;
}

void MultiTx::CalcMSig(const Key::IDV& kidv, ECC::Scalar::Native& offs)
{
	ECC::Scalar::Native sk;
	m_pKdf->DeriveKey(sk, kidv);
	offs += sk;
}

ECC::Point& MultiTx::PushInput(Transaction& tx)
{
	tx.m_vInputs.emplace_back(new Input);
	return tx.m_vInputs.back()->m_Commitment;
}

bool MultiTx::BuildTxPart(Transaction& tx, bool bIsSender, ECC::Scalar::Native& offs)
{
	const Height hVer = MaxHeight;

	offs = -offs; // initially contains kernel offset
	ECC::Scalar::Native sk;

	// inputs
	std::vector<Key::IDV> vec;
	if (Get(vec, Codes::InpKidvs))
	{
		for (size_t i = 0; i < vec.size(); i++)
			CalcInput(vec[i], offs, PushInput(tx));
		vec.clear();
	}

	Key::IDV kidvMsig;
	if (Get(kidvMsig, Codes::InpMsKidv))
	{
		CalcMSig(kidvMsig, offs);

		if (bIsSender)
		{
			ECC::Point comm;
			if (!Get(comm, Codes::InpMsCommitment))
				return false;
			PushInput(tx) = comm;
		}
	}

	// outputs
	offs = -offs;
	if (Get(vec, Codes::OutpKidvs))
	{
		for (size_t i = 0; i < vec.size(); i++)
		{
			tx.m_vOutputs.emplace_back(new Output);
			tx.m_vOutputs.back()->Create(hVer, sk, *m_pKdf, vec[i], *m_pKdf);
			offs += sk;
		}
		vec.clear();
	}

	if (Get(kidvMsig, Codes::OutpMsKidv))
	{
		CalcMSig(kidvMsig, offs);

		if (bIsSender)
		{
			tx.m_vOutputs.emplace_back(new Output);
			if (!Get(*tx.m_vOutputs.back(), Codes::OutpMsTxo))
				return false;
		}
	}

	offs = -offs;
	tx.m_Offset = offs;

	return true; // ok
}

bool MultiTx::ReadKrn(TxKernel& krn, ECC::Hash::Value& hv)
{
	Get(krn.m_Height.m_Min, Codes::KrnH0);
	Get(krn.m_Height.m_Max, Codes::KrnH1);
	Get(krn.m_Fee, Codes::KrnFee);

	Height hLock = 0;
	Get(hLock, Codes::KrnLockHeight);
	if (hLock)
	{
		krn.m_pRelativeLock.reset(new TxKernel::RelativeLock);
		krn.m_pRelativeLock->m_LockHeight = hLock;

		if (!Get(krn.m_pRelativeLock->m_ID, Codes::KrnLockID))
			return false;
	}

	krn.get_Hash(hv);
	return true;
}

void MultiTx::Update2()
{
	const Height hVer = MaxHeight;

	ECC::Oracle oracle;
	ECC::Scalar::Native skKrn;

	{
		ECC::NoLeak<ECC::uintBig> nnc;
		if (!Get(nnc.V, Codes::Nonce))
		{
			ECC::GenRandom(nnc.V);
			Set(nnc.V, Codes::Nonce);
		}

		m_pKdf->DeriveKey(skKrn, nnc.V);
		oracle << skKrn;
	}


	ECC::Signature::MultiSig msigKrn;

	oracle >> skKrn;
	oracle >> msigKrn.m_Nonce;

	uint32_t iRole = 0;
	Get(iRole, Codes::Role);

	uint32_t nShareRes = 0;
	Get(nShareRes, Codes::ShareResult);

	Transaction tx;

	if (!iRole)
	{
		TxKernel krn;

		if (RaiseTo(1))
		{
			krn.m_Commitment = ECC::Context::get().G * skKrn;
			Send(krn.m_Commitment, Codes::KrnCommitment);

			krn.m_Signature.m_NoncePub = ECC::Context::get().G * msigKrn.m_Nonce;
			Send(krn.m_Signature.m_NoncePub, Codes::KrnNonce);
		}

		if (!Get(krn.m_Commitment, Codes::KrnCommitment) ||
			!Get(krn.m_Signature.m_NoncePub, Codes::KrnNonce) ||
			!Get(krn.m_Signature.m_k, Codes::KrnSig))
			return;

		// finalize signature
		ECC::Scalar::Native k;
		if (!msigKrn.m_NoncePub.Import(krn.m_Signature.m_NoncePub))
		{
			OnFail();
			return;
		}

		ECC::Hash::Value hv;
		if (!ReadKrn(krn, hv))
			return;

		msigKrn.SignPartial(k, hv, skKrn);

		k += ECC::Scalar::Native(krn.m_Signature.m_k);
		krn.m_Signature.m_k = k;

		ECC::Point::Native comm;
		AmountBig::Type fee(Zero);
		if (!krn.IsValid(hVer, fee, comm))
		{
			OnFail();
			return;
		}

		assert(fee == AmountBig::Type(krn.m_Fee));

		tx.m_vKernels.emplace_back(new TxKernel);
		*tx.m_vKernels.back() = krn;

		if (RaiseTo(2))
			Set(hv, Codes::KernelID);

	}
	else
	{
		if (m_Pos < 1)
		{
			TxKernel krn;

			if (!Get(krn.m_Commitment, Codes::KrnCommitment) ||
				!Get(krn.m_Signature.m_NoncePub, Codes::KrnNonce))
			{
				return;
			}
						
			ECC::Point::Native comm;
			if (!comm.Import(krn.m_Signature.m_NoncePub))
			{
				OnFail();
				return;
			}

			comm += ECC::Context::get().G * msigKrn.m_Nonce;
			msigKrn.m_NoncePub = comm;
			krn.m_Signature.m_NoncePub = msigKrn.m_NoncePub;

			if (!comm.Import(krn.m_Commitment))
			{
				OnFail();
				return;
			}

			comm += ECC::Context::get().G * skKrn;
			krn.m_Commitment = comm;

			ECC::Hash::Value hv;
			if (!ReadKrn(krn, hv))
				return;

			ECC::Scalar::Native k;
			msigKrn.SignPartial(k, hv, skKrn);

			krn.m_Signature.m_k = k; // incomplete yet

			verify(RaiseTo(1));

			Send(krn.m_Commitment, Codes::KrnCommitment);
			Send(krn.m_Signature.m_NoncePub, Codes::KrnNonce);
			Send(krn.m_Signature.m_k, Codes::KrnSig);

			Set(hv, Codes::KernelID);
		}
	}

	if (!BuildTxPart(tx, !iRole, skKrn))
		return;

	if (iRole || nShareRes)
	{
		uint32_t nBlock = 0;
		Get(nBlock, Codes::Block);

		if (nBlock)
			return; // oops

		if (RaiseTo(3))
		{
			tx.Normalize();
			Send(tx, Codes::TxPartial);
		}
	}

	if (iRole && !nShareRes)
	{
		OnDone();
		return;
	}


	Transaction txPeer;
	if (!Get(txPeer, Codes::TxPartial))
		return;

	uint32_t nRestrict = 0;
	Get(nRestrict, Codes::RestrictInputs);
	if (nRestrict)
	{
		Key::IDV kidvMsig;
		if ((iRole > 0) && Get(kidvMsig, Codes::InpMsKidv))
		{
			if (txPeer.m_vInputs.size() != 1)
			{
				OnFail();
				return;
			}

			ECC::Point comm;
			if (!Get(comm, Codes::InpMsCommitment))
				return;

			if (comm != txPeer.m_vInputs.front()->m_Commitment)
			{
				OnFail();
				return;
			}
		}
		else
		{
			if (!txPeer.m_vInputs.empty())
			{
				OnFail();
				return;
			}
		}
	}

	nRestrict = 0;
	Get(nRestrict, Codes::RestrictOutputs);
	if (nRestrict)
	{
		uint32_t nMaxPeerOutputs = 1;

		Key::IDV kidvMsig;
		if ((iRole > 0) && Get(kidvMsig, Codes::OutpMsKidv))
			nMaxPeerOutputs++; // the peer is supposed to add it

		if (txPeer.m_vOutputs.size() > nMaxPeerOutputs)
		{
			OnFail();
			return;
		}

/*

		TODO: either remove the m_CanDuplicate flag at all from the protocol (i.e. always allow duplication), or make sure we create Outputs with it set to true.

		for (size_t i = 0; i < txPeer.m_vOutputs.size(); i++)
		{
			if (!txPeer.m_vOutputs[i]->m_CanDuplicate)
			{
				OnFail();
				return;
			}
		}
*/
	}

	Transaction txFull;
	TxVectors::Writer wtx(txFull, txFull);

	volatile bool bStop = false;
	wtx.Combine(tx.get_Reader(), txPeer.get_Reader(), bStop);

	txFull.m_Offset = ECC::Scalar::Native(tx.m_Offset) + ECC::Scalar::Native(txPeer.m_Offset);
	txFull.Normalize();

	TxBase::Context::Params pars;
	TxBase::Context ctx(pars);
	ctx.m_Height.m_Min = Rules::get().get_LastFork().m_Height;
	if (!txFull.IsValid(ctx))
	{
		OnFail();
		return;
	}

	Set(txFull, Codes::TxFinal);
	OnDone();
}


/////////////////////
// WithdrawTx
WithdrawTx::Worker::Worker(WithdrawTx& x)
	:m_s0(x.m_pGateway, x.m_pStorage, 1, x.m_MSig)
	,m_s1(x.m_pGateway, x.m_pStorage, 2, x.m_Tx1)
	,m_s2(x.m_pGateway, x.m_pStorage, 3, x.m_Tx2)
{
}

bool WithdrawTx::Worker::get_One(Blob& blob)
{
	return m_s0.get_S()->ReadConst(Codes::One, blob, uint32_t(1));
}

bool WithdrawTx::Worker::S0::Read(uint32_t code, Blob& blob)
{
	switch (code)
	{
	case Codes::Role:
		return m_pS->Read(Codes::Role, blob);
	}

	return Router::Read(code, blob);
}

bool WithdrawTx::Worker::S1::Read(uint32_t code, Blob& blob)
{
	switch (code)
	{
	case Codes::Role:
		return m_pS->Read(Codes::Role, blob);

	case MultiTx::Codes::OutpMsKidv:
		return get_ParentObj().m_s0.Read(Multisig::Codes::Kidv, blob);

	case MultiTx::Codes::OutpMsTxo:
		return get_ParentObj().m_s0.Read(Multisig::Codes::OutputTxo, blob);

	case MultiTx::Codes::Block:
		{
			// block it until Tx2 is ready. Should be invoked only for Role==1
			uint32_t status = 0;
			get_ParentObj().m_s2.Get(status, Codes::Status);

			if (Status::Success == status)
				return false; // i.e. not blocked
		}

		// no break;

	case MultiTx::Codes::RestrictInputs:
	case MultiTx::Codes::RestrictOutputs:
		return get_ParentObj().get_One(blob);
	}

	return Router::Read(code, blob);
}

bool WithdrawTx::Worker::S2::Read(uint32_t code, Blob& blob)
{
	switch (code)
	{
	case Codes::Role:
		return m_pS->Read(Codes::Role, blob);

	case MultiTx::Codes::InpMsKidv:
		return get_ParentObj().m_s0.Read(Multisig::Codes::Kidv, blob);

	case MultiTx::Codes::InpMsCommitment:
		return get_ParentObj().m_s0.Read(Multisig::Codes::Commitment, blob);

	case MultiTx::Codes::KrnLockID:
		return get_ParentObj().m_s1.Read(MultiTx::Codes::KernelID, blob);

	case MultiTx::Codes::ShareResult: // both peers must have valid (msig1 -> outs)
	case MultiTx::Codes::RestrictInputs:
	case MultiTx::Codes::RestrictOutputs:
		return get_ParentObj().get_One(blob);
	}

	return Router::Read(code, blob);
}


void WithdrawTx::Update2()
{
	Worker wrk(*this);

	// update all internal txs. They have dependencies, hence the order is important (to save p2p roundtrips).
	//
	// MSig is not dependent on anything. Hence it's always the 1st
	// Tx1: msig0 -> msig1
	//		Blocked at "point of no return" until Tx2 is completed. Applicable for Role==1 only
	// Tx2: msig1 -> outs, timelocked
	//		Needs KernelID of Tx1
	//
	// Current order of update:
	//		MSig
	//		Tx1
	//		Tx2
	//		Tx1 iff Tx2 completed


	uint32_t status0 = m_MSig.Update();
	if (status0 > Status::Success)
	{
		OnFail();
		return;
	}

	uint32_t status1 = m_Tx1.Update();
	if (status1 > Status::Success)
	{
		OnFail();
		return;
	}

	uint32_t status2 = m_Tx2.Update();
	if (status2 > Status::Success)
	{
		OnFail();
		return;
	}

	if (status2 == Status::Success)
	{
		status1 = m_Tx1.Update();
		if (status1 > Status::Success)
		{
			OnFail();
			return;
		}
	}

	if (status0 && status1 && status2)
		OnDone();
}

} // namespace Negotiator
} // namespace beam
