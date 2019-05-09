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

		m_Peer.m_pStorage->Send(code, std::move(buf));
	}


} // namespace Gateway

namespace Storage
{

	void Map::Send(uint32_t code, ByteBuffer&& buf)
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
// IBase

void IBase::OnFail()
{
	Set(Status::Error, Codes::Status);
}

void IBase::OnDone()
{
	Set(Status::Success, Codes::Status);
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

} // namespace Negotiator
} // namespace beam
