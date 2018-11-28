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

#include <ctime>
#include <chrono>
#include <cmath>
#include "block_crypt.h"

namespace beam
{

	/////////////
	// HeightRange
	void HeightRange::Reset()
	{
		m_Min = 0;
		m_Max = MaxHeight;
	}

	void HeightRange::Intersect(const HeightRange& x)
	{
		m_Min = std::max(m_Min, x.m_Min);
		m_Max = std::min(m_Max, x.m_Max);
	}

	bool HeightRange::IsEmpty() const
	{
		return m_Min > m_Max;
	}

	bool HeightRange::IsInRange(Height h) const
	{
		return IsInRangeRelative(h - m_Min);
	}

	bool HeightRange::IsInRangeRelative(Height dh) const
	{
		return dh <= (m_Max - m_Min);
	}

#define CMP_SIMPLE(a, b) \
		if (a < b) \
			return -1; \
		if (a > b) \
			return 1;

#define CMP_MEMBER(member) CMP_SIMPLE(member, v.member)

#define CMP_MEMBER_EX(member) \
		{ \
			int n = member.cmp(v.member); \
			if (n) \
				return n; \
		}

#define CMP_PTRS(a, b) \
		if (a) \
		{ \
			if (!b) \
				return 1; \
			int n = a->cmp(*b); \
			if (n) \
				return n; \
		} else \
			if (b) \
				return -1;

#define CMP_MEMBER_PTR(member) CMP_PTRS(member, v.member)

	template <typename T>
	void ClonePtr(std::unique_ptr<T>& trg, const std::unique_ptr<T>& src)
	{
		if (src)
		{
			trg.reset(new T);
			*trg = *src;
		}
		else
			trg.reset();
	}

	template <typename T>
	void PushVectorPtr(std::vector<typename T::Ptr>& vec, const T& val)
	{
		vec.resize(vec.size() + 1);
		typename T::Ptr& p = vec.back();
		p.reset(new T);
		*p = val;
	}

	/////////////
	// Input
	int TxElement::cmp(const TxElement& v) const
	{
		CMP_MEMBER(m_Maturity)
		CMP_MEMBER_EX(m_Commitment)
		return 0;
	}

	int Input::cmp(const Input& v) const
	{
		return Cast::Down<TxElement>(*this).cmp(v);
	}

	/////////////
	// Output
	bool Output::IsValid(ECC::Point::Native& comm) const
	{
		if (!comm.Import(m_Commitment))
			return false;

		ECC::Oracle oracle;
		oracle << m_Incubation;

		if (m_pConfidential)
		{
			if (m_Coinbase)
				return false; // coinbase must have visible amount

			if (m_pPublic)
				return false;

			return m_pConfidential->IsValid(comm, oracle);
		}

		if (!m_pPublic)
			return false;

		if (!(Rules::get().AllowPublicUtxos || m_Coinbase))
			return false;

		return m_pPublic->IsValid(comm, oracle);
	}

	void Output::operator = (const Output& v)
	{
		Cast::Down<TxElement>(*this) = v;
		m_Coinbase = v.m_Coinbase;
		m_Incubation = v.m_Incubation;
		ClonePtr(m_pConfidential, v.m_pConfidential);
		ClonePtr(m_pPublic, v.m_pPublic);
	}

	int Output::cmp(const Output& v) const
	{
		{
			int n = Cast::Down<TxElement>(*this).cmp(v);
			if (n)
				return n;
		}

		CMP_MEMBER(m_Coinbase)
		CMP_MEMBER(m_Incubation)
		CMP_MEMBER_PTR(m_pConfidential)
		CMP_MEMBER_PTR(m_pPublic)

		return 0;
	}

	void Output::CreateInternal(const ECC::Scalar::Native& sk, Amount v, bool bPublic, Key::IKdf* pKdf, const Key::ID* pKid)
	{
		m_Commitment = ECC::Commitment(sk, v);

		ECC::Oracle oracle;
		oracle << m_Incubation;

		ECC::RangeProof::CreatorParams cp;
		cp.m_Kidv.m_Value = v;

		if (pKdf)
		{
			assert(pKid);
			Cast::Down<Key::ID>(cp.m_Kidv) = *pKid;
			get_SeedKid(cp.m_Seed.V, *pKdf);
		}
		else
		{
			ZeroObject(Cast::Down<Key::ID>(cp.m_Kidv));
			ECC::Hash::Processor() << "outp" << sk << v >> cp.m_Seed.V;
		}

		if (bPublic)
		{
			m_pPublic.reset(new ECC::RangeProof::Public);
			m_pPublic->m_Value = v;
			m_pPublic->Create(sk, cp, oracle);
		}
		else
		{
			m_pConfidential.reset(new ECC::RangeProof::Confidential);
			m_pConfidential->Create(sk, cp, oracle);
		}
	}

	void Output::Create(ECC::Scalar::Native& sk, Key::IKdf& kdf, const Key::IDV& kidv, bool bPublic /* = false */)
	{
		kdf.DeriveKey(sk, kidv);
		CreateInternal(sk, kidv.m_Value, bPublic || m_Coinbase, &kdf, &kidv);
	}

	void Output::Create(const ECC::Scalar::Native& sk, Amount v, bool bPublic /* = false */)
	{
		CreateInternal(sk, v, bPublic, NULL, NULL);
	}

	void Output::get_SeedKid(ECC::uintBig& seed, Key::IPKdf& kdf) const
	{
		ECC::Hash::Processor() << m_Commitment >> seed;

		ECC::Scalar::Native sk;
		kdf.DerivePKey(sk, seed);

		ECC::Hash::Processor() << sk >> seed;
	}

	bool Output::Recover(Key::IPKdf& kdf, Key::IDV& kidv) const
	{
		ECC::RangeProof::CreatorParams cp;
		get_SeedKid(cp.m_Seed.V, kdf);

		ECC::Oracle oracle;
		oracle << m_Incubation;

		if (m_pPublic)
		{
		    m_pPublic->Recover(cp);
		}
		else if (!(m_pConfidential && m_pConfidential->Recover(oracle, cp)))
		{
			return false;
		}

		// reconstruct the commitment
		ECC::Mode::Scope scope(ECC::Mode::Fast);

		ECC::Hash::Value hv;
		cp.m_Kidv.get_Hash(hv);

		ECC::Point::Native comm, comm2;
		kdf.DerivePKey(comm, hv);

		comm += ECC::Context::get().H * cp.m_Kidv.m_Value;

		if (!comm2.Import(m_Commitment))
			return false;

		comm = -comm;
		comm += comm2;
		
		if (!(comm == Zero))
			return false;

		// bingo!
		kidv = cp.m_Kidv;
		return true;
	}

	void HeightAdd(Height& trg, Height val)
	{
		trg += val;
		if (trg < val)
			trg = Height(-1);
	}

	Height Output::get_MinMaturity(Height h) const
	{
		HeightAdd(h, m_Coinbase ? Rules::get().MaturityCoinbase : Rules::get().MaturityStd);
		HeightAdd(h, m_Incubation);
		return h;
	}

	/////////////
	// TxKernel
	bool TxKernel::Traverse(ECC::Hash::Value& hv, AmountBig* pFee, ECC::Point::Native* pExcess, const TxKernel* pParent, const ECC::Hash::Value* pLockImage) const
	{
		if (pParent)
		{
			// nested kernel restrictions
			if ((m_Height.m_Min > pParent->m_Height.m_Min) ||
				(m_Height.m_Max < pParent->m_Height.m_Max))
				return false; // parent Height range must be contained in ours.
		}

		ECC::Hash::Processor hp;
		hp	<< m_Fee
			<< m_Height.m_Min
			<< m_Height.m_Max
			<< m_Commitment
			<< (bool) m_pHashLock;

		if (m_pHashLock)
		{
			if (!pLockImage)
			{
				ECC::Hash::Processor() << m_pHashLock->m_Preimage >> hv;
				pLockImage = &hv;
			}

			hp << *pLockImage;
		}

		ECC::Point::Native ptExcNested;
		if (pExcess)
			ptExcNested = Zero;

		const TxKernel* p0Krn = NULL;
		for (auto it = m_vNested.begin(); ; it++)
		{
			bool bBreak = (m_vNested.end() == it);
			hp << bBreak;

			if (bBreak)
				break;

			const TxKernel& v = *(*it);
			if (p0Krn && (*p0Krn > v))
				return false;
			p0Krn = &v;

			if (!v.Traverse(hv, pFee, pExcess ? &ptExcNested : NULL, this, NULL))
				return false;

			hp << hv;
		}

		hp >> hv;

		if (pExcess)
		{
			ECC::Point::Native pt;
			if (!pt.Import(m_Commitment))
				return false;

			ptExcNested = -ptExcNested;
			ptExcNested += pt;

			if (!m_Signature.IsValid(hv, ptExcNested))
				return false;

			*pExcess += pt;
		}

		if (pFee)
			*pFee += m_Fee;

		return true;
	}

	void TxKernel::get_Hash(Merkle::Hash& out, const ECC::Hash::Value* pLockImage /* = NULL */) const
	{
		Traverse(out, NULL, NULL, NULL, pLockImage);
	}

	bool TxKernel::IsValid(AmountBig& fee, ECC::Point::Native& exc) const
	{
		ECC::Hash::Value hv;
		return Traverse(hv, &fee, &exc, NULL, NULL);
	}

	void TxKernel::get_ID(Merkle::Hash& out, const ECC::Hash::Value* pLockImage /* = NULL */) const
	{
		get_Hash(out, pLockImage);
	}

	int TxKernel::cmp(const TxKernel& v) const
	{
		{
			int n = Cast::Down<TxElement>(*this).cmp(v);
			if (n)
				return n;
		}

		CMP_MEMBER_EX(m_Signature)
		CMP_MEMBER(m_Fee)
		CMP_MEMBER(m_Height.m_Min)
		CMP_MEMBER(m_Height.m_Max)

		auto it0 = m_vNested.begin();
		auto it1 = v.m_vNested.begin();

		for ( ; m_vNested.end() != it0; it0++, it1++)
		{
			if (v.m_vNested.end() == it1)
				return 1;

			int n = (*it0)->cmp(*(*it1));
			if (n)
				return n;
		}

		if (v.m_vNested.end() != it1)
			return -1;

		return 0;
	}

	void TxKernel::Sign(const ECC::Scalar::Native& sk)
	{
		m_Commitment = ECC::Point::Native(ECC::Context::get().G * sk);

		ECC::Hash::Value hv;
		get_Hash(hv);
		m_Signature.Sign(hv, sk);
	}

	void TxKernel::operator = (const TxKernel& v)
	{
		Cast::Down<TxElement>(*this) = v;
		m_Signature = v.m_Signature;
		m_Fee = v.m_Fee;
		m_Height = v.m_Height;
		ClonePtr(m_pHashLock, v.m_pHashLock);

		m_vNested.resize(v.m_vNested.size());

		for (size_t i = 0; i < v.m_vNested.size(); i++)
			ClonePtr(m_vNested[i], v.m_vNested[i]);
	}

	/////////////
	// Transaction
	template <class T>
	void RebuildVectorWithoutNulls(std::vector<T>& v, size_t nDel)
	{
		std::vector<T> vSrc;
		vSrc.swap(v);
		v.reserve(vSrc.size() - nDel);

		for (size_t i = 0; i < vSrc.size(); i++)
			if (vSrc[i])
				v.push_back(std::move(vSrc[i]));
	}

	int TxBase::CmpInOut(const Input& in, const Output& out)
	{
		if (in.m_Maturity)
			return Cast::Down<TxElement>(in).cmp(out);

		// if maturity isn't overridden (as in standard txs/blocks) - we consider the commitment and the coinbase flag.
		// In such a case the maturity parameters (such as explicit incubation) - are ignored. There's just no way to prevent the in/out elimination.
		int n = in.m_Commitment.cmp(out.m_Commitment);
		if (n)
			return n;

		return out.m_Coinbase ? 1 : 0;
	}

	size_t TxVectors::Perishable::NormalizeP()
	{
		std::sort(m_vInputs.begin(), m_vInputs.end());
		std::sort(m_vOutputs.begin(), m_vOutputs.end());

		size_t nDel = 0;

		size_t i1 = m_vOutputs.size();
		for (size_t i0 = 0; i0 < m_vInputs.size(); i0++)
		{
			Input::Ptr& pInp = m_vInputs[i0];

			for (; i1 < m_vOutputs.size(); i1++)
			{
				Output::Ptr& pOut = m_vOutputs[i1];

				int n = TxBase::CmpInOut(*pInp, *pOut);
				if (n <= 0)
				{
					if (!n)
					{
						pInp.reset();
						pOut.reset();
						nDel++;
					}
					break;
				}
			}
		}

		if (nDel)
		{
			RebuildVectorWithoutNulls(m_vInputs, nDel);
			RebuildVectorWithoutNulls(m_vOutputs, nDel);
		}

		return nDel;
	}

	void TxVectors::Ethernal::NormalizeE()
	{
		std::sort(m_vKernels.begin(), m_vKernels.end());
	}

	size_t TxVectors::Full::Normalize()
	{
		NormalizeE();
		return NormalizeP();
	}

	template <typename T>
	void MoveIntoVec(std::vector<T>& trg, std::vector<T>& src)
	{
		if (trg.empty())
			trg = std::move(src);
		else
		{
			trg.reserve(trg.size() + src.size());

			for (size_t i = 0; i < src.size(); i++)
				trg.push_back(std::move(src[i]));

			src.clear();
		}
	}

	void TxVectors::Full::MoveInto(Full& trg)
	{
		MoveIntoVec(trg.m_vInputs, m_vInputs);
		MoveIntoVec(trg.m_vOutputs, m_vOutputs);
		MoveIntoVec(trg.m_vKernels, m_vKernels);
	}

	template <typename T>
	int CmpPtrVectors(const std::vector<T>& a, const std::vector<T>& b)
	{
		CMP_SIMPLE(a.size(), b.size())

		for (size_t i = 0; i < a.size(); i++)
		{
			CMP_PTRS(a[i], b[i])
		}

		return 0;
	}

	#define CMP_MEMBER_VECPTR(member) \
		{ \
			int n = CmpPtrVectors(member, v.member); \
			if (n) \
				return n; \
		}

	bool Transaction::IsValid(Context& ctx) const
	{
		return
			ctx.ValidateAndSummarize(*this, get_Reader()) &&
			ctx.IsValidTransaction();
	}

	void Transaction::get_Key(KeyType& key) const
	{
		if (m_Offset.m_Value == Zero)
		{
			// proper transactions must contain non-trivial offset, and this should be enough to identify it with sufficient probability
			// However in case it's not specified - construct the key from contents
			key = Zero;

			for (size_t i = 0; i < m_vInputs.size(); i++)
				key ^= m_vInputs[i]->m_Commitment.m_X;

			for (size_t i = 0; i < m_vOutputs.size(); i++)
				key ^= m_vOutputs[i]->m_Commitment.m_X;

			for (size_t i = 0; i < m_vKernels.size(); i++)
				key ^= m_vKernels[i]->m_Commitment.m_X;
		}
		else
			key = m_Offset.m_Value;
	}

	template <typename T>
	const T* get_FromVector(const std::vector<std::unique_ptr<T> >& v, size_t idx)
	{
		return (idx >= v.size()) ? NULL : v[idx].get();
	}

	void TxBase::IReader::Compare(IReader&& rOther, bool& bICover, bool& bOtherCovers)
	{
		bICover = bOtherCovers = true;
		Reset();
		rOther.Reset();

#define COMPARE_TYPE(var, fnNext) \
		while (var) \
		{ \
			if (!rOther.var) \
			{ \
				bOtherCovers = false; \
				break; \
			} \
 \
			int n = var->cmp(*rOther.var); \
			if (n < 0) \
				bOtherCovers = false; \
			if (n > 0) \
				bICover = false; \
			if (n <= 0) \
				fnNext(); \
			if (n >= 0) \
				rOther.fnNext(); \
		} \
 \
		if (rOther.var) \
			bICover = false;

		COMPARE_TYPE(m_pUtxoIn, NextUtxoIn)
		COMPARE_TYPE(m_pUtxoOut, NextUtxoOut)
		COMPARE_TYPE(m_pKernel, NextKernel)
	}


	void TxVectors::Reader::Clone(Ptr& pOut)
	{
		pOut.reset(new Reader(m_P, m_E));
	}

	void TxVectors::Reader::Reset()
	{
		ZeroObject(m_pIdx);

		m_pUtxoIn = get_FromVector(m_P.m_vInputs, 0);
		m_pUtxoOut = get_FromVector(m_P.m_vOutputs, 0);
		m_pKernel = get_FromVector(m_E.m_vKernels, 0);
	}

	void TxVectors::Reader::NextUtxoIn()
	{
		m_pUtxoIn = get_FromVector(m_P.m_vInputs, ++m_pIdx[0]);
	}

	void TxVectors::Reader::NextUtxoOut()
	{
		m_pUtxoOut = get_FromVector(m_P.m_vOutputs, ++m_pIdx[1]);
	}

	void TxVectors::Reader::NextKernel()
	{
		m_pKernel = get_FromVector(m_E.m_vKernels, ++m_pIdx[2]);
	}

	void TxVectors::Writer::Write(const Input& v)
	{
		PushVectorPtr(m_P.m_vInputs, v);
	}

	void TxVectors::Writer::Write(const Output& v)
	{
		PushVectorPtr(m_P.m_vOutputs, v);
	}

	void TxVectors::Writer::Write(const TxKernel& v)
	{
		PushVectorPtr(m_E.m_vKernels, v);
	}

	/////////////
	// AmoutBig
	void AmountBig::operator += (Amount x)
	{
		Lo += x;
		if (Lo < x)
			Hi++;
	}

	void AmountBig::operator -= (Amount x)
	{
		if (Lo < x)
			Hi--;
		Lo -= x;
	}

	void AmountBig::operator += (const AmountBig& x)
	{
		operator += (x.Lo);
		Hi += x.Hi;
	}

	void AmountBig::operator -= (const AmountBig& x)
	{
		operator -= (x.Lo);
		Hi -= x.Hi;
	}

	void AmountBig::Export(uintBig& x) const
	{
		x = Zero;
		x.AssignRange<Amount, 0>(Lo);
		x.AssignRange<Amount, (sizeof(Lo) << 3) >(Hi);
	}

	void AmountBig::AddTo(ECC::Point::Native& res) const
	{
		if (Hi)
		{
			uintBig val;
			Export(val);

			ECC::Scalar s;
			s.m_Value = val;
			res += ECC::Context::get().H_Big * s;
		}
		else
			if (Lo)
				res += ECC::Context::get().H * Lo;
	}

	/////////////
	// Block

	Rules g_Rules; // refactor this to enable more flexible acess for current rules (via TLS or etc.)
	Rules& Rules::get()
	{
		return g_Rules;
	}

	const Height Rules::HeightGenesis	= 1;
	const Amount Rules::Coin			= 1000000;

	void Rules::UpdateChecksum()
	{
		// all parameters, including const (in case they'll be hardcoded to different values in later versions)
		ECC::Hash::Processor()
			<< ECC::Context::get().m_hvChecksum
			<< HeightGenesis
			<< Coin
			<< CoinbaseEmission
			<< MaturityCoinbase
			<< MaturityStd
			<< MaxBodySize
			<< FakePoW
			<< AllowPublicUtxos
			<< DesiredRate_s
			<< DifficultyReviewCycle
			<< MaxDifficultyChange
			<< TimestampAheadThreshold_s
			<< WindowForMedian
			<< StartDifficulty.m_Packed
			<< MaxRollbackHeight
			<< MacroblockGranularity
			<< (uint32_t) Block::PoW::K
			<< (uint32_t) Block::PoW::N
			<< (uint32_t) Block::PoW::NonceType::nBits
			<< uint32_t(11) // increment this whenever we change something in the protocol
#ifndef BEAM_TESTNET
            << "masternet"
#endif
			// out
			>> Checksum;
	}


	int Block::SystemState::ID::cmp(const ID& v) const
	{
		CMP_MEMBER(m_Height)
		CMP_MEMBER_EX(m_Hash)
		return 0;
	}

	int Block::SystemState::Full::cmp(const Full& v) const
	{
		CMP_MEMBER(m_Height)
		CMP_MEMBER_EX(m_Kernels)
		CMP_MEMBER_EX(m_Definition)
		CMP_MEMBER_EX(m_Prev)
		CMP_MEMBER_EX(m_ChainWork)
		CMP_MEMBER(m_TimeStamp)
		CMP_MEMBER(m_PoW.m_Difficulty.m_Packed)
		CMP_MEMBER_EX(m_PoW.m_Nonce)
		CMP_MEMBER(m_PoW.m_Indices)
		return 0;
	}

	bool Block::SystemState::Full::IsNext(const Full& sNext) const
	{
		if (m_Height + 1 != sNext.m_Height)
			return false;

		if (!m_Height)
			return sNext.m_Prev == Zero;

		Merkle::Hash hv;
		get_Hash(hv);
		return sNext.m_Prev == hv;
	}

	void Block::SystemState::Full::NextPrefix()
	{
		get_Hash(m_Prev);
		m_Height++;
	}

	void Block::SystemState::Full::get_HashInternal(Merkle::Hash& out, bool bTotal) const
	{
		// Our formula:
		ECC::Hash::Processor hp;
		hp
			<< m_Height
			<< m_Prev
			<< m_ChainWork
			<< m_Kernels
			<< m_Definition
			<< m_TimeStamp
			<< m_PoW.m_Difficulty.m_Packed;

		if (bTotal)
		{
			hp
				<< Blob(&m_PoW.m_Indices.at(0), sizeof(m_PoW.m_Indices))
				<< m_PoW.m_Nonce;
		}

		hp >> out;
	}

	void Block::SystemState::Full::get_HashForPoW(Merkle::Hash& hv) const
	{
		get_HashInternal(hv, false);
	}

	void Block::SystemState::Full::get_Hash(Merkle::Hash& hv) const
	{
		get_HashInternal(hv, true);
	}

	bool Block::SystemState::Full::IsSane() const
	{
		if (m_Height < Rules::HeightGenesis)
			return false;
		if ((m_Height == Rules::HeightGenesis) && !(m_Prev == Zero))
			return false;

		return true;
	}

	void Block::SystemState::Full::get_ID(ID& out) const
	{
		out.m_Height = m_Height;
		get_Hash(out.m_Hash);
	}

	bool Block::SystemState::Full::IsValidPoW() const
	{
		if (Rules::get().FakePoW)
			return true;

		Merkle::Hash hv;
		get_HashForPoW(hv);
		return m_PoW.IsValid(hv.m_pData, hv.nBytes);
	}

#if defined(BEAM_USE_GPU)
    bool Block::SystemState::Full::GeneratePoW(const PoW::Cancel& fnCancel, bool useGpu)
#else
    bool Block::SystemState::Full::GeneratePoW(const PoW::Cancel& fnCancel)
#endif
	{
		Merkle::Hash hv;
		get_HashForPoW(hv);

#if defined(BEAM_USE_GPU)
        if (useGpu)
            return m_PoW.SolveGPU(hv.m_pData, hv.nBytes, fnCancel);
#endif
        return m_PoW.Solve(hv.m_pData, hv.nBytes, fnCancel);
	}

	bool Block::SystemState::Sequence::Element::IsValidProofUtxo(const ECC::Point& comm, const Input::Proof& p) const
	{
		// verify known part. Last node (history) should be at left
		if (p.m_Proof.empty() || p.m_Proof.back().first)
			return false;

		Merkle::Hash hv;
		p.m_State.get_ID(hv, comm);

		Merkle::Interpret(hv, p.m_Proof);
		return hv == m_Definition;
	}

	bool Block::SystemState::Full::IsValidProofKernel(const TxKernel& krn, const TxKernel::LongProof& proof) const
	{
		Merkle::Hash hv;
		krn.get_ID(hv);
		return IsValidProofKernel(hv, proof);
	}

	bool Block::SystemState::Full::IsValidProofKernel(const Merkle::Hash& hvID, const TxKernel::LongProof& proof) const
	{
		if (!proof.m_State.IsValid())
			return false;

		Merkle::Hash hv = hvID;
		Merkle::Interpret(hv, proof.m_Inner);
		if (hv != proof.m_State.m_Kernels)
			return false;

		if (proof.m_State == *this)
			return true;
		if (proof.m_State.m_Height > m_Height)
			return false;

		ID id;
		proof.m_State.get_ID(id);
		return IsValidProofState(id, proof.m_Outer);
	}

	bool Block::SystemState::Full::IsValidProofState(const ID& id, const Merkle::HardProof& proof) const
	{
		// verify the whole proof structure
		if ((id.m_Height < Rules::HeightGenesis) || (id.m_Height >= m_Height))
			return false;

		struct Verifier
			:public Merkle::Mmr
			,public Merkle::IProofBuilder
		{
			Merkle::Hash m_hv;

			Merkle::HardProof::const_iterator m_itPos;
			Merkle::HardProof::const_iterator m_itEnd;

			bool InterpretOnce(bool bOnRight)
			{
				if (m_itPos == m_itEnd)
					return false;

				Merkle::Interpret(m_hv, *m_itPos++, bOnRight);
				return true;
			}

			virtual bool AppendNode(const Merkle::Node& n, const Merkle::Position&) override
			{
				return InterpretOnce(n.first);
			}

			virtual void LoadElement(Merkle::Hash&, const Merkle::Position&) const override {}
			virtual void SaveElement(const Merkle::Hash&, const Merkle::Position&) override {}
		};

		Verifier vmmr;
		vmmr.m_hv = id.m_Hash;
		vmmr.m_itPos = proof.begin();
		vmmr.m_itEnd = proof.end();

		vmmr.m_Count = m_Height - Rules::HeightGenesis;
		if (!vmmr.get_Proof(vmmr, id.m_Height - Rules::HeightGenesis))
			return false;

		if (!vmmr.InterpretOnce(true))
			return false;
		
		if (vmmr.m_itPos != vmmr.m_itEnd)
			return false;

		return vmmr.m_hv == m_Definition;
	}

	void Block::BodyBase::ZeroInit()
	{
		ZeroObject(m_Subsidy);
		ZeroObject(m_Offset);
		m_SubsidyClosing = false;
	}

	void Block::BodyBase::Merge(const BodyBase& next)
	{
		m_Subsidy += next.m_Subsidy;

		if (next.m_SubsidyClosing)
		{
			assert(!m_SubsidyClosing);
			m_SubsidyClosing = true;
		}

		ECC::Scalar::Native offs(m_Offset);
		offs += next.m_Offset;
		m_Offset = offs;
	}

	bool Block::BodyBase::IsValid(const HeightRange& hr, bool bSubsidyOpen, TxBase::IReader&& r) const
	{
		assert((hr.m_Min >= Rules::HeightGenesis) && !hr.IsEmpty());

		TxBase::Context ctx;
		ctx.m_Height = hr;
		ctx.m_bBlockMode = true;

		return
			ctx.ValidateAndSummarize(*this, std::move(r)) &&
			ctx.IsValidBlock(*this, bSubsidyOpen);
	}

	/////////////
	// SystemState::IHistory
	bool Block::SystemState::IHistory::get_Tip(Full& s)
	{
		struct Walker :public IWalker
		{
			Full& m_Res;
			Walker(Full& s) :m_Res(s) {}

			virtual bool OnState(const Full& s) override {
				m_Res = s;
				return false;
			}
		} w(s);

		if (!Enum(w, NULL))
			return true;

		ZeroObject(s);
		return false;
	}

	bool Block::SystemState::HistoryMap::Enum(IWalker& w, const Height* pBelow)
	{
		for (auto it = (pBelow ? m_Map.lower_bound(*pBelow) : m_Map.end()); m_Map.begin() != it; )
			if (!w.OnState((--it)->second))
				return false;

		return true;
	}

	bool Block::SystemState::HistoryMap::get_At(Full& s, Height h)
	{
		auto it = m_Map.find(h);
		if (m_Map.end() == it)
			return false;

		s = it->second;
		return true;
	}

	void Block::SystemState::HistoryMap::AddStates(const Full* pS, size_t nCount)
	{
		for (size_t i = 0; i < nCount; i++)
		{
			const Full& s = pS[i];
			m_Map[s.m_Height] = s;
		}
	}

	void Block::SystemState::HistoryMap::DeleteFrom(Height h)
	{
		while (!m_Map.empty())
		{
			auto it = m_Map.end();
			if ((--it)->first < h)
				break;

			m_Map.erase(it);
		}
	}

	void Block::SystemState::HistoryMap::ShrinkToWindow(Height dh)
	{
		if (m_Map.empty())
			return;

		Height h = m_Map.rbegin()->first;
		if (h <= dh)
			return;

		Height h0 = h - dh;

		while (true)
		{
			auto it = m_Map.begin();
			assert(m_Map.end() != it);

			if (it->first > h0)
				break;

			m_Map.erase(it);
		}
	}

	/////////////
	// Builder
	Block::Builder::Builder()
	{
		m_Offset = Zero;
	}

	void Block::Builder::AddCoinbaseAndKrn(Key::IKdf& kdf, Height h, Output::Ptr& pOutp, TxKernel::Ptr& pKrn)
	{
		ECC::Scalar::Native sk;

		pOutp.reset(new Output);
		pOutp->m_Coinbase = true;
		pOutp->Create(sk, kdf, Key::IDV(Rules::get().CoinbaseEmission, h, Key::Type::Coinbase));

		m_Offset += sk;

		pKrn.reset(new TxKernel);
		pKrn->m_Height.m_Min = h; // make it similar to others

		kdf.DeriveKey(sk, Key::ID(h, Key::Type::Kernel2));
		pKrn->Sign(sk);
		m_Offset += sk;
	}

	void Block::Builder::AddCoinbaseAndKrn(Key::IKdf& kdf, Height h)
	{
		Output::Ptr pOutp;
		TxKernel::Ptr pKrn;
		AddCoinbaseAndKrn(kdf, h, pOutp, pKrn);

		m_Txv.m_vOutputs.push_back(std::move(pOutp));
		m_Txv.m_vKernels.push_back(std::move(pKrn));
	}

	void Block::Builder::AddFees(Key::IKdf& kdf, Height h, Amount fees, Output::Ptr& pOutp)
	{
		ECC::Scalar::Native sk;

		pOutp.reset(new Output);
		pOutp->Create(sk, kdf, Key::IDV(fees, h, Key::Type::Comission));

		m_Offset += sk;
	}

	void Block::Builder::AddFees(Key::IKdf& kdf, Height h, Amount fees)
	{
		if (fees)
		{
			Output::Ptr pOutp;
			AddFees(kdf, h, fees, pOutp);

			m_Txv.m_vOutputs.push_back(std::move(pOutp));
		}
	}

	/////////////
	// Difficulty
	void Rules::AdjustDifficulty(Difficulty& d, Timestamp tCycleBegin_s, Timestamp tCycleEnd_s) const
	{
		//static_assert(DesiredRate_s * DifficultyReviewCycle < uint32_t(-1), "overflow?");
		const uint32_t dtTrg_s = DesiredRate_s * DifficultyReviewCycle;

		uint32_t dt_s; // evaluate carefully, avoid possible overflow
		if (tCycleEnd_s <= tCycleBegin_s)
			dt_s = 0;
		else
		{
			tCycleEnd_s -= tCycleBegin_s;
			dt_s = (tCycleEnd_s < uint32_t(-1)) ? uint32_t(tCycleEnd_s) : uint32_t(-1);
		}

		d.Adjust(dt_s, dtTrg_s, MaxDifficultyChange);
	}

	void Difficulty::Pack(uint32_t order, uint32_t mantissa)
	{
		if (order <= s_MaxOrder)
		{
			assert((mantissa >> s_MantissaBits) == 1U);
			mantissa &= (1U << s_MantissaBits) - 1;

			m_Packed = mantissa | (order << s_MantissaBits);
		}
		else
			m_Packed = s_Inf;
	}

	void Difficulty::Unpack(uint32_t& order, uint32_t& mantissa) const
	{
		order = (m_Packed >> s_MantissaBits);

		const uint32_t nLeadingBit = 1U << s_MantissaBits;
		mantissa = nLeadingBit | (m_Packed & (nLeadingBit - 1));
	}

	bool Difficulty::IsTargetReached(const ECC::uintBig& hv) const
	{
		if (m_Packed > s_Inf)
			return false; // invalid

		// multiply by (raw) difficulty, check if the result fits wrt normalization.
		Raw val;
		Unpack(val);

		auto a = hv * val; // would be 512 bits

		static_assert(!(s_MantissaBits & 7), ""); // fix the following code lines to support non-byte-aligned mantissa size

		return memis0(a.m_pData, Raw::nBytes - (s_MantissaBits >> 3));
	}

	void Difficulty::Unpack(Raw& res) const
	{
		res = Zero;
		if (m_Packed < s_Inf)
		{
			uint32_t order, mantissa;
			Unpack(order, mantissa);
			res.AssignSafe(mantissa, order);

		}
		else
			res.Inv();
	}

	Difficulty::Raw operator + (const Difficulty::Raw& base, const Difficulty& d)
	{
		Difficulty::Raw res;
		d.Unpack(res);
		res += base;
		return res;
	}

	Difficulty::Raw& operator += (Difficulty::Raw& res, const Difficulty& d)
	{
		Difficulty::Raw base;
		d.Unpack(base);
		res += base;
		return res;
	}

	Difficulty::Raw operator - (const Difficulty::Raw& base, const Difficulty& d)
	{
		Difficulty::Raw res;
		d.Unpack(res);
		res.Negate();
		res += base;
		return res;
	}

	Difficulty::Raw& operator -= (Difficulty::Raw& res, const Difficulty& d)
	{
		Difficulty::Raw base;
		d.Unpack(base);
		base.Negate();
		res += base;
		return res;
	}

	void Difficulty::Adjust(uint32_t src, uint32_t trg, uint32_t nMaxOrderChange)
	{
		if (!(src || trg))
			return; // degenerate case

		uint32_t order, mantissa;
		Unpack(order, mantissa);

		Adjust(src, trg, nMaxOrderChange, order, mantissa);

		if (signed(order) >= 0)
			Pack(order, mantissa);
		else
			m_Packed = 0;

	}

	void Difficulty::Adjust(uint32_t src, uint32_t trg, uint32_t nMaxOrderChange, uint32_t& order, uint32_t& mantissa)
	{
		bool bIncrease = (src < trg);

		// order adjustment (rough)
		for (uint32_t i = 0; ; i++)
		{
			if (i == nMaxOrderChange)
				return;

			uint32_t srcAdj = src;
			if (bIncrease)
			{
				if ((srcAdj <<= 1) > trg)
					break;

				if (++order > s_MaxOrder)
					return;
			}
			else
			{
				if ((srcAdj >>= 1) < trg)
					break;

				if (!order--)
					return;
			}

			src = srcAdj;
		}

		// By now the ratio between src/trg is less than 2. Adjust the mantissa
		uint64_t val = trg;
		val *= uint64_t(mantissa);
		val /= uint64_t(src);

		mantissa = (uint32_t) val;

		uint32_t nLeadingBit = mantissa >> Difficulty::s_MantissaBits;

		if (bIncrease)
		{
			assert(nLeadingBit && (nLeadingBit < 4));

			if (nLeadingBit > 1)
			{
				order++;
				mantissa >>= 1;
			}
		}
		else
		{
			assert(nLeadingBit <= 1);

			if (!nLeadingBit)
			{
				order--;
				mantissa <<= 1;
				assert(mantissa >> Difficulty::s_MantissaBits);
			}
		}
	}

	double Difficulty::ToFloat() const
	{
		uint32_t order, mantissa;
		Unpack(order, mantissa);

		int nOrderCorrected = order - s_MantissaBits; // must be signed
		return ldexp(mantissa, nOrderCorrected);
	}

	double Difficulty::ToFloat(Raw& x)
	{
		double res = 0;

		int nOrder = x.nBits - 8 - s_MantissaBits;
		for (uint32_t i = 0; i < x.nBytes; i++, nOrder -= 8)
		{
			uint8_t n = x.m_pData[i];
			if (n)
				res += ldexp(n, nOrder);
		}

		return res;
	}

	std::ostream& operator << (std::ostream& s, const Difficulty& d)
	{
		typedef uintBig_t<sizeof(Difficulty) * 8 - Difficulty::s_MantissaBits> uintOrder;
		typedef uintBig_t<Difficulty::s_MantissaBits> uintMantissa;

		uintOrder n0;
		n0.AssignSafe(d.m_Packed >> Difficulty::s_MantissaBits, 0);
		char sz0[uintOrder::nTxtLen + 1];
		n0.Print(sz0);

		uintMantissa n1;
		n1.AssignSafe(d.m_Packed & ((1U << Difficulty::s_MantissaBits) - 1), 0);
		char sz1[uintMantissa::nTxtLen + 1];
		n1.Print(sz1);

		s << sz0 << '-' << sz1 << '(' << d.ToFloat() << ')';

		return s;
	}

	std::ostream& operator << (std::ostream& s, const Block::SystemState::ID& id)
	{
		s << id.m_Height << "-" << id.m_Hash;
		return s;
	}

	/////////////
	// Misc
	Timestamp getTimestamp()
	{
		return beam::Timestamp(std::chrono::seconds(std::time(nullptr)).count());
	}

	uint32_t GetTime_ms()
	{
#ifdef WIN32
		return GetTickCount();
#else // WIN32
		// platform-independent analogue of GetTickCount
		using namespace std::chrono;
		return (uint32_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
#endif // WIN32
	}

	uint32_t GetTimeNnz_ms()
	{
		uint32_t ret = GetTime_ms();
		return ret ? ret : 1;
	}

} // namespace beam
