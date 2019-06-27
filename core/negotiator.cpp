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
		if (m_Trg.Read(code, blob))
		{
			assert(false);
			return;
		}

		m_Trg.Write(code, std::move(buf));
	}

} // namespace Gateway

namespace Storage
{
	bool IBase::ReadOneU(Blob& blob)
	{
		// helper
		static const uint8_t s_One = 0x81; // w.r.t. current encoding scheme for unsigned integer (variable length)
		blob.p = &s_One;
		blob.n = 1;
		return true;
	}

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

uint32_t IBase::Router::Offset(uint32_t iChannel)
{
	return iChannel << 16;
}

uint32_t IBase::Router::Offset() const
{
	return Offset(m_iChannel);
}

void IBase::Router::Send(uint32_t code, ByteBuffer&& buf)
{
	m_pG->Send(code + Offset(), std::move(buf));
}

void IBase::Router::Write(uint32_t code, ByteBuffer&& buf)
{
	m_pS->Write(code + Offset(), std::move(buf));
}

bool IBase::Router::Read(uint32_t code, Blob& blob)
{
	switch (code)
	{
	case Codes::Role:
	case Codes::Scheme:
		// common params, not offset, by default inherited from parent
		break;

	default:
		code += Offset();
	}

	return m_pS->Read(code, blob);
}

/////////////////////
// IBase

bool IBase::RaiseTo(uint32_t pos)
{
	if (m_Pos >= pos)
		return false;

	m_Pos = pos;
	return true;
}

uint32_t IBase::Update()
{
	static_assert(Status::Pending == 0, "");
	uint32_t nStatus = Status::Pending;

	Get(nStatus, Codes::Status);
	if (nStatus)
		return nStatus;

	uint32_t nPos = 0;
	Get(nPos, Codes::Position);
	m_Pos = nPos;

	nStatus = Update2();
	if (nStatus)
		Set(nStatus, Codes::Status);

	assert(m_Pos >= nPos);
	if (m_Pos > nPos)
		Set(m_Pos, Codes::Position);

	return nStatus;
}

bool IBase::SubQueryVar(std::string& s, uint32_t code, uint32_t i0, uint32_t i1, const char* szPrefix)
{
	i0 <<= 16;
	i1 <<= 16;

	if ((code < i0) || (code >= i1))
		return false;

	std::string sufix;
	QueryVar(sufix, code - i0);

	s = szPrefix;
	s += '.';
	s += sufix;

	return true;
}

/////////////////////
// Multisig

struct Multisig::Impl
{
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
};


uint32_t Multisig::Update2()
{
	ECC::NoLeak<ECC::uintBig> seedSk;
	if (!Get(seedSk.V, Codes::Nonce))
	{
		ECC::GenRandom(seedSk.V);
		Set(seedSk.V, Codes::Nonce);
	}

	Impl::Part2Plus p2;
	if (RaiseTo(1))
	{
		ZeroObject(p2);
		ECC::RangeProof::Confidential::MultiSig::CoSignPart(seedSk.V, p2);
		Send(p2, Codes::BpPart2);
	}

	ECC::Key::IDV kidv;
	if (!Get(kidv, Codes::Kidv))
		return 0;

	ECC::Scalar::Native sk;
	m_pKdf->DeriveKey(sk, kidv);

	ECC::Point::Native comm = ECC::Context::get().G * sk;

	ECC::Point pPk[2];
	pPk[0] = comm;

	if (RaiseTo(2))
		Send(pPk[0], Codes::PubKey);

	Height hScheme = MaxHeight;

	if (!Get(pPk[1], Codes::PubKey) ||
		!Get(p2, Codes::BpPart2) ||
		!Get(hScheme, Codes::Scheme))
		return 0;

	Output outp;
	if (Get(outp.m_Commitment, Codes::Commitment))
	{
		//comm = outp.m_Commitment;
	}
	else
	{
		ECC::Point::Native pt;
		if (!pt.Import(pPk[1]))
			return Status::Error;

		comm += pt;
		comm += ECC::Context::get().H * kidv.m_Value;
		outp.m_Commitment = comm;

		Set(outp.m_Commitment, Codes::Commitment);
	}

	ECC::RangeProof::CreatorParams cp;
	cp.m_Kidv = Zero;
	cp.m_Kidv.m_Value = kidv.m_Value;

	uint32_t iRole = 0;
	Get(iRole, Codes::Role);

	ECC::Hash::Processor()
		<< pPk[iRole]
		<< pPk[!iRole]
		>> cp.m_Seed.V;

	ECC::Oracle oracle, o2;
	outp.Prepare(oracle, hScheme);

	outp.m_pConfidential.reset(new ECC::RangeProof::Confidential);
	ECC::RangeProof::Confidential& bp = *outp.m_pConfidential;

	bp.m_Part2 = p2;

	o2 = oracle;
	if (!bp.CoSign(seedSk.V, sk, cp, o2, ECC::RangeProof::Confidential::Phase::Step2))
		return Status::Error;

	uint32_t nShareRes = 0;
	Get(nShareRes, Codes::ShareResult);

	bool bMustHaveResult = !iRole || nShareRes;

	if (!Get(bp.m_Part3.m_TauX, Codes::BpPart3))
	{
		if (RaiseTo(3))
		{
			ECC::RangeProof::Confidential::MultiSig msig;
			msig.m_Part1 = bp.m_Part1;
			msig.m_Part2 = bp.m_Part2;

			ZeroObject(bp.m_Part3);
			msig.CoSignPart(seedSk.V, sk, oracle, bp.m_Part3);

			Send(bp.m_Part3.m_TauX, Codes::BpPart3);
		}

		return bMustHaveResult ? 0 : Status::Success;
	}

	o2 = oracle;
	if (!bp.CoSign(seedSk.V, sk, cp, o2, ECC::RangeProof::Confidential::Phase::Finalize))
		return Status::Error;


	// test (TODO: should be done in Phase::Finalize)
	if (!outp.IsValid(hScheme, comm))
		return Status::Error;

	Set(outp, Codes::OutputTxo);


	if ((iRole || nShareRes) && RaiseTo(3))
	{
		sk = bp.m_Part3.m_TauX;

		ECC::RangeProof::Confidential::Part3 p3;
		Get(p3.m_TauX, Codes::BpPart3);

		ECC::Scalar::Native sk2(p3.m_TauX);
		sk2 = -sk2;
		sk2 += sk;
		p3.m_TauX = sk2;

		Send(p3.m_TauX, Codes::BpPart3);
	}

	return Status::Success;
}

void Multisig::QueryVar(std::string& s, uint32_t code)
{
	switch (code)
	{
	case Codes::PubKey: s = "Partial Commitment"; break;
	case Codes::BpPart2: s = "Bulletproof T1,T2"; break;
	case Codes::BpPart3: s = "Bulletproof TauX"; break;
	}
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
	Height hScheme = MaxHeight;
	if (!Get(hScheme, Codes::Scheme))
		return false;

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
			tx.m_vOutputs.back()->Create(hScheme, sk, *m_pKdf, vec[i], *m_pKdf);
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

uint32_t MultiTx::Update2()
{
	Height hScheme = MaxHeight;
	if (!Get(hScheme, Codes::Scheme))
		return 0;

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
			return 0;

		// finalize signature
		ECC::Scalar::Native k;
		if (!msigKrn.m_NoncePub.Import(krn.m_Signature.m_NoncePub))
			return Status::Error;

		ECC::Hash::Value hv;
		if (!ReadKrn(krn, hv))
			return 0;

		msigKrn.SignPartial(k, hv, skKrn);

		k += ECC::Scalar::Native(krn.m_Signature.m_k);
		krn.m_Signature.m_k = k;

		ECC::Point::Native comm;
		AmountBig::Type fee(Zero);
		if (!krn.IsValid(hScheme, fee, comm))
			return Status::Error;

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
				return 0;
			}
						
			ECC::Point::Native comm;
			if (!comm.Import(krn.m_Signature.m_NoncePub))
				return Status::Error;

			comm += ECC::Context::get().G * msigKrn.m_Nonce;
			msigKrn.m_NoncePub = comm;
			krn.m_Signature.m_NoncePub = msigKrn.m_NoncePub;

			if (!comm.Import(krn.m_Commitment))
				return Status::Error;

			comm += ECC::Context::get().G * skKrn;
			krn.m_Commitment = comm;

			ECC::Hash::Value hv;
			if (!ReadKrn(krn, hv))
				return 0;

			ECC::Scalar::Native k;
			msigKrn.SignPartial(k, hv, skKrn);

			krn.m_Signature.m_k = k; // incomplete yet

			BEAM_VERIFY(RaiseTo(1));

			Send(krn.m_Commitment, Codes::KrnCommitment);
			Send(krn.m_Signature.m_NoncePub, Codes::KrnNonce);
			Send(krn.m_Signature.m_k, Codes::KrnSig);

			Set(hv, Codes::KernelID);
		}
	}

	if (!BuildTxPart(tx, !iRole, skKrn))
		return 0;

	if (iRole || nShareRes)
	{
		uint32_t nBlock = 0;
		Get(nBlock, Codes::Barrier);

		if (nBlock)
			return 0; // oops

		if (RaiseTo(3))
		{
			tx.Normalize();
			Send(tx, Codes::TxPartial);

			Set(uint32_t(1), Codes::BarrierCrossed);
		}
	}

	if (iRole && !nShareRes)
		return Status::Success;

	Transaction txPeer;
	if (!Get(txPeer, Codes::TxPartial))
		return 0;

	uint32_t nRestrict = 0;
	Get(nRestrict, Codes::RestrictInputs);
	if (nRestrict)
	{
		// The only input should be the shared MuSig
		Key::IDV kidvMsig;
		if ((iRole > 0) && Get(kidvMsig, Codes::InpMsKidv))
		{
			if (txPeer.m_vInputs.size() != 1)
				return Status::Error;

			ECC::Point comm;
			if (!Get(comm, Codes::InpMsCommitment))
				return 0;

			if (comm != txPeer.m_vInputs.front()->m_Commitment)
				return Status::Error;
		}
		else
		{
			if (!txPeer.m_vInputs.empty())
				return Status::Error;
		}
	}

	nRestrict = 0;
	Get(nRestrict, Codes::RestrictOutputs);
	if (nRestrict)
	{
		uint32_t nMaxPeerOutputs = 1;
		uint32_t nMaxKernels = 0;

		if (iRole > 0)
		{
			nMaxKernels++; // peer should add it

			Key::IDV kidvMsig;
			if (Get(kidvMsig, Codes::OutpMsKidv))
				nMaxPeerOutputs++; // the peer is supposed to add it
		}

		if ((txPeer.m_vOutputs.size() > nMaxPeerOutputs) || (txPeer.m_vKernels.size() > nMaxKernels))
			return Status::Error;
	}

	Transaction txFull;
	TxVectors::Writer wtx(txFull, txFull);

	volatile bool bStop = false;
	wtx.Combine(tx.get_Reader(), txPeer.get_Reader(), bStop);

	txFull.m_Offset = ECC::Scalar::Native(tx.m_Offset) + ECC::Scalar::Native(txPeer.m_Offset);
	txFull.Normalize();

	TxBase::Context::Params pars;
	TxBase::Context ctx(pars);

	ctx.m_Height.m_Min = hScheme;
	if (!txFull.IsValid(ctx))
		return Status::Error;

	Set(txFull, Codes::TxFinal);
	return Status::Success;
}

void MultiTx::QueryVar(std::string& s, uint32_t code)
{
	switch (code)
	{
	case Codes::KrnCommitment: s = "Excess Commitment"; break;
	case Codes::KrnNonce: s = "Nonce Commitment"; break;
	case Codes::KrnSig: s = "Partial Kernel Signature"; break;
	case Codes::TxPartial: s = "Partial Transaction"; break;
	}
}


/////////////////////
// WithdrawTx
WithdrawTx::Worker::Worker(WithdrawTx& x)
	:m_s0(x.m_pGateway, x.m_pStorage, 1, x.m_MSig)
	,m_s1(x.m_pGateway, x.m_pStorage, 2, x.m_Tx1)
	,m_s2(x.m_pGateway, x.m_pStorage, 3, x.m_Tx2)
{
}

bool WithdrawTx::Worker::S1::Read(uint32_t code, Blob& blob)
{
	switch (code)
	{
	case MultiTx::Codes::OutpMsKidv:
		return m_pS->Read(Multisig::Codes::Kidv + Offset(m_iChannel - 1), blob);

	case MultiTx::Codes::OutpMsTxo:
		return m_pS->Read(Multisig::Codes::OutputTxo + Offset(m_iChannel - 1), blob);

	case MultiTx::Codes::Barrier:
		{
			// block it until Tx2 is ready. Should be invoked only for Role==1
			uint32_t status = 0;
			Get(status, Codes::Status + Offset(1));

			if (Status::Success == status)
				return false; // i.e. not blocked
		}

		// no break;

	case MultiTx::Codes::RestrictInputs:
	case MultiTx::Codes::RestrictOutputs:
		return ReadOneU(blob);
	}

	return Router::Read(code, blob);
}

bool WithdrawTx::Worker::S2::Read(uint32_t code, Blob& blob)
{
	switch (code)
	{
	case MultiTx::Codes::InpMsKidv:
		return m_pS->Read(Multisig::Codes::Kidv + Offset(m_iChannel - 2), blob);

	case MultiTx::Codes::InpMsCommitment:
		return m_pS->Read(Multisig::Codes::Commitment + Offset(m_iChannel - 2), blob);

	case MultiTx::Codes::KrnLockID:
		return m_pS->Read(MultiTx::Codes::KernelID + Offset(m_iChannel - 1), blob);

	case MultiTx::Codes::ShareResult: // both peers must have valid (msig1 -> outs)
	case MultiTx::Codes::RestrictInputs:
	case MultiTx::Codes::RestrictOutputs:
		return ReadOneU(blob);
	}

	return Router::Read(code, blob);
}

void WithdrawTx::Setup(bool bSet,
	Key::IDV* pMsig1,
	Key::IDV* pMsig0,
	ECC::Point* pComm0,
	std::vector<Key::IDV>* pOuts,
	const CommonParam& cp)
{
	m_MSig.m_pKdf = m_pKdf;
	m_Tx1.m_pKdf = m_pKdf;
	m_Tx2.m_pKdf = m_pKdf;

	m_MSig.SetGet(bSet, pMsig1, Multisig::Codes::Kidv);
	m_Tx1.SetGet(bSet, pMsig0, MultiTx::Codes::InpMsKidv);
	m_Tx1.SetGet(bSet, pComm0, MultiTx::Codes::InpMsCommitment);
	m_Tx1.SetGet(bSet, cp.m_Krn1.m_pH0, MultiTx::Codes::KrnH0);
	m_Tx1.SetGet(bSet, cp.m_Krn1.m_pH1, MultiTx::Codes::KrnH1);
	m_Tx1.SetGet(bSet, cp.m_Krn1.m_pFee, MultiTx::Codes::KrnFee);
	m_Tx2.SetGet(bSet, cp.m_Krn2.m_pFee, MultiTx::Codes::KrnFee);
	m_Tx2.SetGet(bSet, pOuts, MultiTx::Codes::OutpKidvs);
	m_Tx2.SetGet(bSet, cp.m_Krn2.m_pLock, MultiTx::Codes::KrnLockHeight);
}

void WithdrawTx::get_Result(Result& r)
{
	if (!m_MSig.Get(r.m_Comm1, Multisig::Codes::Commitment))
		r.m_Comm1 = Zero;

	m_Tx1.Get(r.m_tx1, MultiTx::Codes::TxFinal);
	m_Tx2.Get(r.m_tx2, MultiTx::Codes::TxFinal);
}

uint32_t WithdrawTx::Update2()
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
		return status0;

	uint32_t status1 = m_Tx1.Update();
	if (status1 > Status::Success)
		return status1;

	uint32_t status2 = m_Tx2.Update();
	if (status2 > Status::Success)
		return status2;

	if (status2 == Status::Success)
	{
		status1 = m_Tx1.Update();
		if (status1 > Status::Success)
			return status1;
	}

	if (status0 && status1 && status2)
		return Status::Success;

	return 0;
}

void WithdrawTx::QueryVar(std::string& s, uint32_t code)
{
	m_MSig.SubQueryVar(s, code, 1, 2, "MuSig");
	m_Tx1.SubQueryVar(s, code, 2, 3, "Tx-TLock");
	m_Tx2.SubQueryVar(s, code, 3, 4, "Tx-Final");
}

/////////////////////
// ChannelWithdrawal

void ChannelWithdrawal::get_Result(Result& r)
{
	uint32_t iRole = 0;
	Get(iRole, Codes::Role);

	{
		WithdrawTx& x = iRole ? m_WdB : m_WdA;
		WithdrawTx::Result rx;
		x.get_Result(rx);

		r.m_Comm1 = rx.m_Comm1;
		r.m_tx1 = std::move(rx.m_tx1);
		r.m_tx2 = std::move(rx.m_tx2);
	}

	{
		WithdrawTx& x = iRole ? m_WdA : m_WdB;
		WithdrawTx::Result rx;
		x.get_Result(rx);

		r.m_CommPeer1 = rx.m_Comm1;
		r.m_txPeer2 = std::move(rx.m_tx2);
	}
}

bool ChannelWithdrawal::SB::Read(uint32_t code, Blob& blob)
{
	switch (code)
	{
	case Codes::Role:
		{
			// role should be reversed
			uint32_t iRole = 0;
			m_pS->Get(iRole, Codes::Role);

			if (iRole)
				return false;
		}
		return ReadOneU(blob);

	case MultiTx::Codes::InpMsKidv + (2 << 16):
	case MultiTx::Codes::InpMsCommitment + (2 << 16) :
	case MultiTx::Codes::KrnFee + (2 << 16) :
	case MultiTx::Codes::KrnH0 + (2 << 16) :
	case MultiTx::Codes::KrnH1 + (2 << 16) :
	case MultiTx::Codes::OutpKidvs + (3 << 16) :
	case MultiTx::Codes::KrnFee + (3 << 16) :
	case MultiTx::Codes::KrnLockHeight + (3 << 16) :
		// use parameters from SA
		return m_pS->Read(code + Offset(m_iChannel - WithdrawTx::s_Channels), blob);
	}

	return Router::Read(code, blob);
}

/////////////////////
// ChannelOpen

ChannelOpen::Worker::Worker(ChannelOpen& x)
	:m_s0(x.m_pGateway, x.m_pStorage, 1, x.m_MSig)
	,m_s1(x.m_pGateway, x.m_pStorage, 2, x.m_Tx0)
	,m_sa(x.m_pGateway, x.m_pStorage, 3, x.m_WdA)
	,m_sb(x.m_pGateway, x.m_pStorage, 3 + WithdrawTx::s_Channels, x.m_WdB)
	,m_wrkA(x.m_WdA)
	,m_wrkB(x.m_WdB)
{
}

bool ChannelOpen::Worker::S1::Read(uint32_t code, Blob& blob)
{
	switch (code)
	{
	case MultiTx::Codes::OutpMsKidv:
		return m_pS->Read(Multisig::Codes::Kidv + Offset(m_iChannel - 1), blob);

	case MultiTx::Codes::OutpMsTxo:
		return m_pS->Read(Multisig::Codes::OutputTxo + Offset(m_iChannel - 1), blob);

	case MultiTx::Codes::Barrier:
		{
			// block it until our Withdrawal is fully prepared
			uint32_t iRole = 0;
			m_pS->Get(iRole, Codes::Role);

			uint32_t status = 0;
			Get(status, Codes::Status + Offset(1 + WithdrawTx::s_Channels * iRole));

			if (Status::Success == status)
				return false; // i.e. not blocked
		}
		return ReadOneU(blob);
	}

	return Router::Read(code, blob);
}

bool ChannelOpen::Worker::SA::Read(uint32_t code, Blob& blob)
{
	switch (code)
	{
	case MultiTx::Codes::InpMsKidv + (2 << 16):
		return m_pS->Read(Multisig::Codes::Kidv + Offset(m_iChannel - 2), blob);

	case MultiTx::Codes::InpMsCommitment + (2 << 16):
		return m_pS->Read(Multisig::Codes::Commitment + Offset(m_iChannel - 2), blob);
	}

	return Router::Read(code, blob);
}

bool ChannelOpen::Worker::SB::Read(uint32_t code, Blob& blob)
{
	switch (code)
	{
	case MultiTx::Codes::InpMsKidv + (2 << 16):
		return m_pS->Read(Multisig::Codes::Kidv + Offset(m_iChannel - 2 - WithdrawTx::s_Channels), blob);

	case MultiTx::Codes::InpMsCommitment + (2 << 16):
		return m_pS->Read(Multisig::Codes::Commitment + Offset(m_iChannel - 2 - WithdrawTx::s_Channels), blob);
	}

	return ChannelWithdrawal::SB::Read(code, blob);
}

void ChannelOpen::Setup(bool bSet,
	std::vector<Key::IDV>* pInps,
	std::vector<Key::IDV>* pOutsChange,
	Key::IDV* pMsig0,
	const MultiTx::KernelParam& krn1,
	Key::IDV* pMsig1A,
	Key::IDV* pMsig1B,
	std::vector<Key::IDV>* pOutsWd,
	const WithdrawTx::CommonParam& cp)
{
	m_MSig.m_pKdf = m_pKdf;
	m_Tx0.m_pKdf = m_pKdf;
	m_WdA.m_pKdf = m_pKdf;
	m_WdB.m_pKdf = m_pKdf;

	m_MSig.SetGet(bSet, pMsig0, Multisig::Codes::Kidv);
	m_Tx0.SetGet(bSet, pInps, MultiTx::Codes::InpKidvs);
	m_Tx0.SetGet(bSet, pOutsChange, MultiTx::Codes::OutpKidvs);
	m_Tx0.SetGet(bSet, krn1.m_pH0, MultiTx::Codes::KrnH0);
	m_Tx0.SetGet(bSet, krn1.m_pH1, MultiTx::Codes::KrnH1);
	m_Tx0.SetGet(bSet, krn1.m_pFee, MultiTx::Codes::KrnFee);

	m_WdA.Setup(bSet, pMsig1A, nullptr, nullptr, pOutsWd, cp);
	m_WdB.Setup(bSet, pMsig1B, nullptr, nullptr, nullptr, WithdrawTx::CommonParam());
}

void ChannelOpen::get_Result(Result& r)
{
	if (!m_MSig.Get(r.m_Comm0, Multisig::Codes::Commitment))
		r.m_Comm0 = Zero;

	m_Tx0.Get(r.m_txOpen, MultiTx::Codes::TxFinal);

	ChannelWithdrawal::get_Result(r);
}

uint8_t ChannelOpen::get_DoneParts()
{
	uint8_t ret = 0;

	uint32_t iRole = 0;
	Get(iRole, Codes::Role);

	{
		uint32_t status = 0;
		m_Tx0.Get(status, Codes::Status);
		if (Status::Success == status)
			ret |= DoneParts::Main;
	}

	{
		WithdrawTx& x = iRole ? m_WdA : m_WdB;
		uint32_t status = 0;
		x.Get(status, Codes::Status);

		if (Status::Success == status)
			ret |= DoneParts::PeerWd;
	}

	return ret;
}

uint32_t ChannelOpen::Update2()
{
	Worker wrk(*this);

	// update all internal txs. Dependencies:
	//
	// MSig is not dependent on anything.
	// m_Tx0: depends on MSig. In addition it's blocked until on of (m_WdA, m_WdB) is ready.
	// m_WdA: depends on MSig
	// m_WdB: depends on MSig

	// Hence our order is:
	// MSig is first
	// m_Tx0 is last

	uint32_t status0 = m_MSig.Update();
	if (status0 > Status::Success)
		return status0;

	uint32_t statusA = m_WdA.Update();
	if (statusA > Status::Success)
		return statusA;

	uint32_t statusB = m_WdB.Update();
	if (statusB > Status::Success)
		return statusB;

	uint32_t status1 = m_Tx0.Update();
	if (status1 > Status::Success)
		return status1;

	if (status0 && status1 && statusA && statusB)
		return Status::Success;

	return 0;
}

void ChannelOpen::QueryVar(std::string& s, uint32_t code)
{
	m_MSig.SubQueryVar(s, code, 1, 2, "MuSig");
	m_Tx0.SubQueryVar(s, code, 2, 3, "Tx-Open");
	m_WdA.SubQueryVar(s, code, 3, 3 + WithdrawTx::s_Channels, "Exit-A");
	m_WdB.SubQueryVar(s, code, 3 + WithdrawTx::s_Channels, 3 + WithdrawTx::s_Channels * 2, "Exit-B");
}

/////////////////////
// ChannelUpdate

ChannelUpdate::Worker::Worker(ChannelUpdate& x)
	:m_sa(x.m_pGateway, x.m_pStorage, 1, x.m_WdA)
	,m_sb(x.m_pGateway, x.m_pStorage, 1 + WithdrawTx::s_Channels, x.m_WdB)
	,m_wrkA(x.m_WdA)
	,m_wrkB(x.m_WdB)
{
}

void ChannelUpdate::Setup(bool bSet,
	Key::IDV* pMsig0,
	ECC::Point* pComm0,
	Key::IDV* pMsig1A,
	Key::IDV* pMsig1B,
	std::vector<Key::IDV>* pOutsWd,
	const WithdrawTx::CommonParam& cp,
	Key::IDV* pMsigPrevMy,
	Key::IDV* pMsigPrevPeer,
	ECC::Point* pCommPrevPeer)
{
	m_WdA.m_pKdf = m_pKdf;
	m_WdB.m_pKdf = m_pKdf;

	m_WdA.Setup(bSet, pMsig1A, pMsig0, pComm0, pOutsWd, cp);
	m_WdB.Setup(bSet, pMsig1B, nullptr, nullptr, nullptr, WithdrawTx::CommonParam());

	SetGet(bSet, pMsigPrevMy, Codes::KidvPrev);
	SetGet(bSet, pMsigPrevPeer, Codes::KidvPrevPeer);
	SetGet(bSet, pCommPrevPeer, Codes::CommitmentPrevPeer);
}

uint32_t ChannelUpdate::Update2()
{
	Worker wrk(*this);

	// m_WdA and m_WdB are independent
	uint32_t statusA = m_WdA.Update();
	if (statusA > Status::Success)
		return statusA;

	uint32_t statusB = m_WdB.Update();
	if (statusB > Status::Success)
		return statusB;

	uint32_t iRole = 0;
	Get(iRole, Codes::Role);

	ECC::Scalar k;

	uint32_t statusMy = iRole ? statusB : statusA;
	if ((Status::Success == statusMy) && (m_Pos < 1))
	{
		// send my previous blinding factor
		Key::IDV kidv;
		if (Get(kidv, Codes::KidvPrev))
		{
			ECC::Scalar::Native sk;
			m_pKdf->DeriveKey(sk, kidv);
			k = sk; // not secret anymore

			RaiseTo(1);
			Send(k, Codes::PeerBlindingFactor);

			Set(uint32_t(1), Codes::SelfKeyRevealed);
		}
	}

	bool bGotPeerKey = false;
	uint32_t statusPeer = iRole ? statusA : statusB;
	if (Status::Success == statusPeer)
	{
		bGotPeerKey = Get(k, Codes::PeerKey);
		if (!bGotPeerKey && Get(k, Codes::PeerBlindingFactor))
		{
			Key::IDV kidv;
			ECC::Point comm;
			if (Get(kidv, Codes::KidvPrevPeer) && Get(comm, Codes::CommitmentPrevPeer))
			{
				// verify it
				ECC::Scalar::Native sk;
				m_pKdf->DeriveKey(sk, kidv);

				sk += ECC::Scalar::Native(k);

				comm.m_Y = !comm.m_Y; // negate
				ECC::Point::Native pt;
				pt.Import(comm); // don't care

				pt += ECC::Commitment(sk, kidv.m_Value);

				if (!(pt == Zero))
					return Status::Error;

				Set(k, Codes::PeerKey);
				bGotPeerKey = true;
			}
		}
	}

	if (statusA && statusB && bGotPeerKey)
		return Status::Success;

	return 0;
}

void ChannelUpdate::get_Result(Result& r)
{
	ChannelWithdrawal::get_Result(r);

	uint32_t val = 0;
	Get(val, Codes::SelfKeyRevealed);
	r.m_RevealedSelfKey = (val > 0);
	r.m_PeerKeyValid = Get(r.m_PeerKey, Codes::PeerKey);
}

uint8_t ChannelUpdate::get_DoneParts()
{
	uint8_t ret = 0;

	uint32_t iRole = 0;
	Get(iRole, Codes::Role);

	{
		WithdrawTx& x = iRole ? m_WdB : m_WdA;
		uint32_t status = 0;
		x.Get(status, Codes::Status);

		if (Status::Success == status)
			ret |= DoneParts::Main;
	}

	{
		WithdrawTx& x = iRole ? m_WdA : m_WdB;
		uint32_t status = 0;
		x.Get(status, Codes::Status);

		if (Status::Success == status)
			ret |= DoneParts::PeerWd;
	}

	ECC::Scalar peerKey;
	if (Get(peerKey, Codes::PeerKey))
		ret |= DoneParts::PeerKey;

	return ret;
}

void ChannelUpdate::QueryVar(std::string& s, uint32_t code)
{
	switch (code)
	{
	case Codes::PeerBlindingFactor:
		s = "Reveal Previous Blinding Factor";
		break;

	default:

		m_WdA.SubQueryVar(s, code, 1, 1 + WithdrawTx::s_Channels, "Exit-A");
		m_WdB.SubQueryVar(s, code, 1 + WithdrawTx::s_Channels, 1 + WithdrawTx::s_Channels * 2, "Exit-B");
	}
}

} // namespace Negotiator
} // namespace beam
