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

#include <utility> // std::swap
#include <algorithm>
#include <ctime>
#include "block_crypt.h"
#include "../utility/serialize.h"
#include "../core/serialization_adapters.h"

#ifndef WIN32
#	include <unistd.h>
#	include <errno.h>
#endif // WIN32

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

	/////////////
	// Merkle
	namespace Merkle
	{
		void Interpret(Hash& out, const Hash& hLeft, const Hash& hRight)
		{
			ECC::Hash::Processor() << hLeft << hRight >> out;
		}

		void Interpret(Hash& hOld, const Hash& hNew, bool bNewOnRight)
		{
			if (bNewOnRight)
				Interpret(hOld, hOld, hNew);
			else
				Interpret(hOld, hNew, hOld);
		}

		void Interpret(Hash& hash, const Node& n)
		{
			Interpret(hash, n.second, n.first);
		}

		void Interpret(Hash& hash, const Proof& p)
		{
			for (Proof::const_iterator it = p.begin(); p.end() != it; it++)
				Interpret(hash, *it);
		}
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
	int CommitmentAndMaturity::cmp_CaM(const CommitmentAndMaturity& v) const
	{
		CMP_MEMBER_EX(m_Commitment)
		CMP_MEMBER(m_Maturity)
		return 0;
	}

	int CommitmentAndMaturity::cmp(const CommitmentAndMaturity& v) const
	{
		return cmp_CaM(v);
	}

	int Input::cmp(const Input& v) const
	{
		return cmp_CaM(v);
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

		return m_pPublic->IsValid(comm, oracle);
	}

	void Output::operator = (const Output& v)
	{
		*((CommitmentAndMaturity*) this) = v;
		m_Coinbase = v.m_Coinbase;
		m_Incubation = v.m_Incubation;
		ClonePtr(m_pConfidential, v.m_pConfidential);
		ClonePtr(m_pPublic, v.m_pPublic);
	}

	int Output::cmp(const Output& v) const
	{
		int n = cmp_CaM(v);
		if (n)
			return n;

		CMP_MEMBER(m_Coinbase)
		CMP_MEMBER(m_Incubation)
		CMP_MEMBER_PTR(m_pConfidential)
		CMP_MEMBER_PTR(m_pPublic)

		return 0;
	}

	void Output::Create(const ECC::Scalar::Native& k, Amount v, bool bPublic /* = false */)
	{
		m_Commitment = ECC::Commitment(k, v);

		ECC::Oracle oracle;
		oracle << m_Incubation;

		if (bPublic)
		{
			m_pPublic.reset(new ECC::RangeProof::Public);
			m_pPublic->m_Value = v;
			m_pPublic->Create(k, oracle);
		} else
		{
			m_pConfidential.reset(new ECC::RangeProof::Confidential);
			m_pConfidential->Create(k, v, oracle);
		}
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
	bool TxKernel::Traverse(ECC::Hash::Value& hv, AmountBig* pFee, ECC::Point::Native* pExcess, const TxKernel* pParent) const
	{
		if (pParent)
		{
			// nested kernel restrictions
			if (m_Multiplier != pParent->m_Multiplier) // Multipliers must be equal
				return false; 

			if ((m_Height.m_Min > pParent->m_Height.m_Min) ||
				(m_Height.m_Max < pParent->m_Height.m_Max))
				return false; // parent Height range must be contained in ours.
		}

		ECC::Hash::Processor hp;
		hp	<< m_Fee
			<< m_Height.m_Min
			<< m_Height.m_Max
			<< (bool) m_pContract
			<< (bool) m_pHashLock;

		if (m_pContract)
		{
			hp	<< m_pContract->m_Msg
				<< m_pContract->m_PublicKey;
		}

		if (m_pHashLock)
			hp << m_pHashLock->m_Hash;

		const TxKernel* p0Krn = NULL;
		for (auto it = m_vNested.begin(); m_vNested.end() != it; it++)
		{
			const TxKernel& v = *(*it);
			if (p0Krn && (*p0Krn > v))
				return false;
			p0Krn = &v;

			if (!v.Traverse(hv, pFee, pExcess, this))
				return false;

			v.HashForSigningToTotal(hv); // The hash of this kernel should account for the signature and the excess of the internal kernels.
			hp << true << hv;
		}

		hp << false >> hv;

		if (pExcess)
		{
			ECC::Point::Native pt;
			if (!pt.Import(m_Excess))
				return false;

			if (m_Multiplier)
			{
				ECC::Mode::Scope scope(ECC::Mode::Fast);

				ECC::Point::Native pt2(pt);
				pt = pt2 * (m_Multiplier + 1);
			}

			if (!m_Signature.IsValid(hv, pt))
				return false;

			*pExcess += pt;

			if (m_pContract)
			{
				ECC::Hash::Value hv2;
				get_HashForContract(hv2, hv);

				if (!pt.Import(m_pContract->m_PublicKey))
					return false;

				if (!m_pContract->m_Signature.IsValid(hv2, pt))
					return false;
			}

			if (m_pHashLock)
			{
				ECC::Hash::Value hv2;
				ECC::Hash::Processor() << m_pHashLock->m_Preimage >> hv2;
				if (m_pHashLock->m_Hash != hv2)
					return false;
			}
		}

		if (pFee)
			*pFee += m_Fee;

		return true;
	}

	void TxKernel::get_HashForContract(ECC::Hash::Value& out, const ECC::Hash::Value& msg) const
	{
		ECC::Hash::Processor()
			<< msg
			<< m_Excess
			>> out;
	}

	void TxKernel::get_HashForSigning(Merkle::Hash& out) const
	{
		Traverse(out, NULL, NULL, NULL);
	}

	bool TxKernel::IsValid(AmountBig& fee, ECC::Point::Native& exc) const
	{
		ECC::Hash::Value hv;
		return Traverse(hv, &fee, &exc, NULL);
	}

	void TxKernel::HashForSigningToTotal(Merkle::Hash& hv) const
	{
		ECC::Hash::Processor()
			<< hv
			<< m_Excess
			<< m_Multiplier
			<< m_Signature.m_e
			<< m_Signature.m_k
			>> hv;

		// Some kernel hash values are reserved for the system usage
		if (hv == ECC::Zero)
		{
			ECC::Hash::Processor() << hv >> hv;
			assert(!(hv == ECC::Zero));
		}
	}

	void TxKernel::get_HashTotal(Merkle::Hash& hv) const
	{
		get_HashForSigning(hv);
		HashForSigningToTotal(hv);
	}

	bool TxKernel::IsValidProof(const Merkle::Proof& proof, const Merkle::Hash& root) const
	{
		Merkle::Hash hv;
		get_HashTotal(hv);
		Merkle::Interpret(hv, proof);
		return hv == root;
	}

	int TxKernel::Contract::cmp(const Contract& v) const
	{
		CMP_MEMBER_EX(m_Msg)
		CMP_MEMBER_EX(m_PublicKey)
		CMP_MEMBER_EX(m_Signature)
		return 0;
	}

	int TxKernel::cmp(const TxKernel& v) const
	{
		CMP_MEMBER_EX(m_Excess)
		CMP_MEMBER(m_Multiplier)
		CMP_MEMBER_EX(m_Signature)
		CMP_MEMBER(m_Fee)
		CMP_MEMBER(m_Height.m_Min)
		CMP_MEMBER(m_Height.m_Max)
		CMP_MEMBER_PTR(m_pContract)

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

	void TxKernel::operator = (const TxKernel& v)
	{
		m_Excess = v.m_Excess;
		m_Multiplier = v.m_Multiplier;
		m_Signature = v.m_Signature;
		m_Fee = v.m_Fee;
		m_Height = v.m_Height;
		ClonePtr(m_pContract, v.m_pContract);
		ClonePtr(m_pHashLock, v.m_pHashLock);

		m_vNested.resize(v.m_vNested.size());

		for (size_t i = 0; i < v.m_vNested.size(); i++)
			ClonePtr(m_vNested[i], v.m_vNested[i]);
	}

	/////////////
	// Transaction
	void TxBase::Context::Reset()
	{
		m_Sigma = ECC::Zero;

		ZeroObject(m_Fee);
		ZeroObject(m_Coinbase);
		m_Height.Reset();
		m_bBlockMode = false;
		m_nVerifiers = 1;
		m_iVerifier = 0;
		m_pAbort = NULL;
	}

	bool TxBase::Context::ShouldVerify(uint32_t& iV) const
	{
		if (iV)
		{
			iV--;
			return false;
		}

		iV = m_nVerifiers - 1;
		return true;
	}

	bool TxBase::Context::ShouldAbort() const
	{
		return m_pAbort && *m_pAbort;
	}

	bool TxBase::Context::HandleElementHeight(const HeightRange& hr)
	{
		HeightRange r = m_Height;
		r.Intersect(hr);
		if (r.IsEmpty())
			return false;

		if (!m_bBlockMode)
			m_Height = r; // shrink permitted range

		return true;
	}

	bool TxBase::Context::Merge(const Context& x)
	{
		assert(m_bBlockMode == x.m_bBlockMode);

		if (!HandleElementHeight(x.m_Height))
			return false;

		m_Sigma += x.m_Sigma;
		m_Fee += x.m_Fee;
		m_Coinbase += x.m_Coinbase;
		return true;
	}

	bool TxBase::Context::ValidateAndSummarize(const TxBase& txb, IReader&& r)
	{
		if (m_Height.IsEmpty())
			return false;

		m_Sigma = -m_Sigma;
		AmountBig feeInp; // dummy var

		assert(m_nVerifiers);
		uint32_t iV = m_iVerifier;

		// Inputs
		r.Reset();

		ECC::Point::Native pt;

		for (const Input* pPrev = NULL; r.m_pUtxoIn; pPrev = r.m_pUtxoIn, r.NextUtxoIn())
		{
			if (ShouldAbort())
				return false;

			if (ShouldVerify(iV))
			{
				if (pPrev && (*pPrev > *r.m_pUtxoIn))
					return false;

				if (!pt.Import(r.m_pUtxoIn->m_Commitment))
					return false;

				m_Sigma += pt;
			}
		}

		for (const TxKernel* pPrev = NULL; r.m_pKernelIn; pPrev = r.m_pKernelIn, r.NextKernelIn())
		{
			if (ShouldAbort())
				return false;

			// locate the corresponding output kernel. Use the fact that kernels are sorted by excess, and then by multiplier
			// Do it regardless to the milti-verifier logic, to ensure we're not confused (muliple identical inputs, less outputs, and etc.)
			while (true)
			{
				if (!r.m_pKernelOut)
					return false;

				const TxKernel& vOut = *r.m_pKernelOut;
				r.NextKernelOut();

				if (vOut.m_Excess > r.m_pKernelIn->m_Excess)
					return false;

				if (vOut.m_Excess == r.m_pKernelIn->m_Excess)
				{
					if (vOut.m_Multiplier <= r.m_pKernelIn->m_Multiplier)
						return false;
					break; // ok
				}
			}

			if (ShouldVerify(iV))
			{
				if (pPrev && (*pPrev > *r.m_pKernelIn))
					return false;

				if (!r.m_pKernelIn->IsValid(feeInp, m_Sigma))
					return false;
			}
		}

		m_Sigma = -m_Sigma;

		// Outputs
		r.Reset();

		for (const Output* pPrev = NULL; r.m_pUtxoOut; pPrev = r.m_pUtxoOut, r.NextUtxoOut())
		{
			if (ShouldAbort())
				return false;

			if (ShouldVerify(iV))
			{
				if (pPrev && (*pPrev > *r.m_pUtxoOut))
					return false;

				if (!r.m_pUtxoOut->IsValid(pt))
					return false;

				m_Sigma += pt;

				if (r.m_pUtxoOut->m_Coinbase)
				{
					if (!m_bBlockMode)
						return false; // regular transactions should not produce coinbase outputs, only the miner should do this.

					assert(r.m_pUtxoOut->m_pPublic); // must have already been checked
					m_Coinbase += r.m_pUtxoOut->m_pPublic->m_Value;
				}
			}
		}

		for (const TxKernel* pPrev = NULL; r.m_pKernelOut; pPrev = r.m_pKernelOut, r.NextKernelOut())
		{
			if (ShouldAbort())
				return false;

			if (ShouldVerify(iV))
			{
				if (pPrev && (*pPrev > *r.m_pKernelOut))
					return false;

				if (!r.m_pKernelOut->IsValid(m_Fee, m_Sigma))
					return false;

				if (!HandleElementHeight(r.m_pKernelOut->m_Height))
					return false;
			}
		}

		if (ShouldVerify(iV))
			m_Sigma += ECC::Context::get().G * txb.m_Offset;

		assert(!m_Height.IsEmpty());
		return true;
	}

	void TxVectors::Sort()
	{
		std::sort(m_vInputs.begin(), m_vInputs.end());
		std::sort(m_vOutputs.begin(), m_vOutputs.end());
		std::sort(m_vKernelsInput.begin(), m_vKernelsInput.end());
		std::sort(m_vKernelsOutput.begin(), m_vKernelsOutput.end());
	}

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

	size_t TxVectors::DeleteIntermediateOutputs()
	{
		size_t nDel = 0;

		size_t i1 = m_vOutputs.size();
		for (size_t i0 = 0; i0 < m_vInputs.size(); i0++)
		{
			Input::Ptr& pInp = m_vInputs[i0];

			for (; i1 < m_vOutputs.size(); i1++)
			{
				Output::Ptr& pOut = m_vOutputs[i1];

				int n = pInp->cmp_CaM(*pOut);
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

	int Transaction::cmp(const Transaction& v) const
	{
		CMP_MEMBER(m_Offset)
		CMP_MEMBER_VECPTR(m_vInputs)
		CMP_MEMBER_VECPTR(m_vOutputs)
		CMP_MEMBER_VECPTR(m_vKernelsInput)
		CMP_MEMBER_VECPTR(m_vKernelsOutput)
		return 0;
	}

	bool TxBase::Context::IsValidTransaction()
	{
		assert(!(m_Coinbase.Lo || m_Coinbase.Hi)); // must have already been checked

		m_Fee.AddTo(m_Sigma);

		return m_Sigma == ECC::Zero;
	}

	bool Transaction::IsValid(Context& ctx) const
	{
		return
			ctx.ValidateAndSummarize(*this, get_Reader()) &&
			ctx.IsValidTransaction();
	}

	template <typename T>
	void TestNotNull(const std::unique_ptr<T>& p)
	{
		if (!p)
			throw std::runtime_error("invalid NULL ptr");
	}

	void TxVectors::TestNoNulls() const
	{
		for (auto it = m_vInputs.begin(); m_vInputs.end() != it; it++)
			TestNotNull(*it);

		for (auto it = m_vKernelsInput.begin(); m_vKernelsInput.end() != it; it++)
			TestNotNull(*it);

		for (auto it = m_vOutputs.begin(); m_vOutputs.end() != it; it++)
			TestNotNull(*it);

		for (auto it = m_vKernelsOutput.begin(); m_vKernelsOutput.end() != it; it++)
			TestNotNull(*it);
	}

	void Transaction::get_Key(KeyType& key) const
	{
		if (m_Offset.m_Value == ECC::Zero)
		{
			// proper transactions must contain non-trivial offset, and this should be enough to identify it with sufficient probability
			// However in case it's not specified - construct the key from contents
			key = ECC::Zero;

			for (size_t i = 0; i < m_vInputs.size(); i++)
				key ^= m_vInputs[i]->m_Commitment.m_X;

			for (size_t i = 0; i < m_vOutputs.size(); i++)
				key ^= m_vOutputs[i]->m_Commitment.m_X;

			for (size_t i = 0; i < m_vKernelsOutput.size(); i++)
				key ^= m_vKernelsOutput[i]->m_Excess.m_X;
		}
		else
			key = m_Offset.m_Value;
	}

	template <typename T>
	const T* get_FromVector(const std::vector<std::unique_ptr<T> >& v, size_t idx)
	{
		return (idx >= v.size()) ? NULL : v[idx].get();
	}

	void TxVectors::Reader::Clone(Ptr& pOut)
	{
		pOut.reset(new Reader(m_Txv));
	}

	void TxVectors::Reader::Reset()
	{
		ZeroObject(m_pIdx);

		m_pUtxoIn = get_FromVector(m_Txv.m_vInputs, 0);
		m_pUtxoOut = get_FromVector(m_Txv.m_vOutputs, 0);
		m_pKernelIn = get_FromVector(m_Txv.m_vKernelsInput, 0);
		m_pKernelOut = get_FromVector(m_Txv.m_vKernelsOutput, 0);
	}

	void TxVectors::Reader::NextUtxoIn()
	{
		m_pUtxoIn = get_FromVector(m_Txv.m_vInputs, ++m_pIdx[0]);
	}

	void TxVectors::Reader::NextUtxoOut()
	{
		m_pUtxoOut = get_FromVector(m_Txv.m_vOutputs, ++m_pIdx[1]);
	}

	void TxVectors::Reader::NextKernelIn()
	{
		m_pKernelIn = get_FromVector(m_Txv.m_vKernelsInput, ++m_pIdx[2]);
	}

	void TxVectors::Reader::NextKernelOut()
	{
		m_pKernelOut = get_FromVector(m_Txv.m_vKernelsOutput, ++m_pIdx[3]);
	}

	void TxVectors::Writer::WriteIn(const Input& v)
	{
		PushVectorPtr(m_Txv.m_vInputs, v);
	}

	void TxVectors::Writer::WriteOut(const Output& v)
	{
		PushVectorPtr(m_Txv.m_vOutputs, v);
	}

	void TxVectors::Writer::WriteIn(const TxKernel& v)
	{
		PushVectorPtr(m_Txv.m_vKernelsInput, v);
	}

	void TxVectors::Writer::WriteOut(const TxKernel& v)
	{
		PushVectorPtr(m_Txv.m_vKernelsOutput, v);
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

	void AmountBig::Export(ECC::uintBig& x) const
	{
		x = ECC::Zero;
		x.AssignRange<Amount, 0>(Lo);
		x.AssignRange<Amount, (sizeof(Lo) << 3) >(Hi);
	}

	void AmountBig::AddTo(ECC::Point::Native& res) const
	{
		if (Hi)
		{
			ECC::Scalar s;
			Export(s.m_Value);
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
			<< DesiredRate_s
			<< DifficultyReviewCycle
			<< MaxDifficultyChange
			<< TimestampAheadThreshold_s
			<< WindowForMedian
			<< uint32_t(1) // increment this whenever we change something in the protocol
			// out
			>> Checksum;
	}


	int Block::SystemState::ID::cmp(const ID& v) const
	{
		CMP_MEMBER(m_Height)
		CMP_MEMBER(m_Hash)
		return 0;
	}

	void Block::SystemState::Full::Set(Prefix& p, const Element& x)
	{
		((Prefix&) *this) = p;
		((Element&) *this) = x;

		get_Hash(p.m_Prev);
		p.m_Height++;
	}

	void Block::SystemState::Full::get_Hash(Merkle::Hash& out) const
	{
		// Our formula:
		ECC::Hash::Processor()
			<< m_Height
			<< m_TimeStamp
			<< m_Prev
			<< m_Definition
			>> out;
	}

	bool Block::SystemState::Full::IsSane() const
	{
		if (m_Height < Rules::HeightGenesis)
			return false;
		if ((m_Height == Rules::HeightGenesis) && !(m_Prev == ECC::Zero))
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
		Merkle::Hash hv;
		get_Hash(hv);
		return m_PoW.IsValid(hv.m_pData, sizeof(hv.m_pData));
	}

	bool Block::SystemState::Full::GeneratePoW(const PoW::Cancel& fnCancel)
	{
		Merkle::Hash hv;
		get_Hash(hv);
		return m_PoW.Solve(hv.m_pData, sizeof(hv.m_pData), fnCancel);
	}

	bool TxBase::Context::IsValidBlock(const Block::BodyBase& bb, bool bSubsidyOpen)
	{
		m_Sigma = -m_Sigma;

		bb.m_Subsidy.AddTo(m_Sigma);

		if (!(m_Sigma == ECC::Zero))
			return false;

		if (bSubsidyOpen)
			return true;

		if (bb.m_SubsidyClosing)
			return false; // already closed

		// For non-genesis blocks we have the following restrictions:
		// Subsidy is bounded by num of blocks multiplied by coinbase emission
		// There must at least some unspent coinbase UTXOs wrt maturity settings

		// check the subsidy is within allowed range
		Height nBlocksInRange = m_Height.m_Max - m_Height.m_Min + 1;

		ECC::uintBig ubSubsidy, ubCoinbase, mul;
		bb.m_Subsidy.Export(ubSubsidy);

		mul = Rules::get().CoinbaseEmission;
		ubCoinbase = nBlocksInRange;
		ubCoinbase = ubCoinbase * mul;

		if (ubSubsidy > ubCoinbase)
			return false;

		// ensure there's a minimal unspent coinbase UTXOs
		if (nBlocksInRange > Rules::get().MaturityCoinbase)
		{
			// some UTXOs may be spent already. Calculate the minimum remaining
			nBlocksInRange -= Rules::get().MaturityCoinbase;
			ubCoinbase = nBlocksInRange;
			ubCoinbase = ubCoinbase * mul;

			if (ubSubsidy > ubCoinbase)
			{
				ubCoinbase.Negate();
				ubSubsidy += ubCoinbase;

			} else
				ubSubsidy = ECC::Zero;
		}

		m_Coinbase.Export(ubCoinbase);
		return (ubCoinbase >= ubSubsidy);
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

    void DeriveKey(ECC::Scalar::Native& out, const ECC::Kdf& kdf, Height h, KeyType eType, uint32_t nIdx /* = 0 */)
    {
        kdf.DeriveKey(out, h, static_cast<uint32_t>(eType), nIdx);
    }

	void ExtractOffset(ECC::Scalar::Native& kKernel, ECC::Scalar::Native& kOffset, Height h /* = 0 */, uint32_t nIdx /* = 0 */)
	{
		ECC::Hash::Value hv;
		ECC::Hash::Processor() << h << nIdx >> hv;

		ECC::NoLeak<ECC::Scalar> s;
		s.V = kKernel;

		kOffset.GenerateNonce(s.V.m_Value, hv, NULL);

		kKernel += kOffset;
		kOffset = -kOffset;
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

		typedef ECC::uintBig_t<ECC::nBits * 2> uintHuge;

		uintHuge a, b;
		a = hv;
		b = val;
		a = a * b;

		static_assert(!(s_MantissaBits & 7), ""); // fix the following code lines to support non-byte-aligned mantissa size

		return memis0(a.m_pData, sizeof(a.m_pData) / 2 - (s_MantissaBits >> 3));
	}

	void Difficulty::Unpack(Raw& res) const
	{
		res = ECC::Zero;
		if (m_Packed < s_Inf)
		{
			uint32_t order, mantissa;
			Unpack(order, mantissa);
			res.AssignSafe(mantissa, order);

		}
		else
			res.Inv();
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
				order++;
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
			assert(nLeadingBit && (nLeadingBit <= 2));

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

	std::ostream& operator << (std::ostream& s, const Difficulty& d)
	{
		uint32_t order = d.m_Packed >> Difficulty::s_MantissaBits;
		uint32_t mantissa = d.m_Packed & ((1U << Difficulty::s_MantissaBits) - 1);
		s << std::hex << order << '-' << mantissa << std::dec;
		return s;
	}

	std::ostream& operator << (std::ostream& s, const Block::SystemState::ID& id)
	{
		s << id.m_Height << "-" << id.m_Hash;
		return s;
	}

	/////////////
	// RW
	void Block::BodyBase::RW::GetPathes(std::string* pArr) const
	{
		pArr[0] = m_sPath + "ui";
		pArr[1] = m_sPath + "uo";
		pArr[2] = m_sPath + "ki";
		pArr[3] = m_sPath + "ko";
		pArr[4] = m_sPath + "hd";

		static_assert(5 == s_Datas, "");
	}

	bool Block::BodyBase::RW::Open(bool bRead)
	{
		using namespace std;

		m_bRead = bRead;

		std::string pArr[s_Datas];
		GetPathes(pArr);

		for (size_t i = 0; i < _countof(m_pS); i++)
			if (!m_pS[i].Open(pArr[i].c_str(), bRead))
				return false;

		return true;
	}

	void Block::BodyBase::RW::Delete()
	{
		std::string pArr[s_Datas];
		GetPathes(pArr);

		for (size_t i = 0; i < _countof(m_pS); i++)
		{
			const std::string& sPath = pArr[i];
#ifdef WIN32
			DeleteFileA(sPath.c_str());
#else // WIN32
			unlink(sPath.c_str());
#endif // WIN32
		}
	}

	void Block::BodyBase::RW::Close()
	{
		for (size_t i = 0; i < _countof(m_pS); i++)
			m_pS[i].Close();
	}

	Block::BodyBase::RW::~RW()
	{
		if (m_bAutoDelete)
		{
			Close();
			Delete();
		}
	}

	void Block::BodyBase::RW::Reset()
	{
		for (size_t i = 0; i < _countof(m_pS); i++)
			m_pS[i].Restart();

		// preload
		LoadInternal(m_pUtxoIn, m_pS[0], m_pGuardUtxoIn);
		LoadInternal(m_pUtxoOut, m_pS[1], m_pGuardUtxoOut);
		LoadInternal(m_pKernelIn, m_pS[2], m_pGuardKernelIn);
		LoadInternal(m_pKernelOut, m_pS[3], m_pGuardKernelOut);
	}

	void Block::BodyBase::RW::Flush()
	{
		for (size_t i = 0; i < _countof(m_pS); i++)
			m_pS[i].Flush();
	}

	void Block::BodyBase::RW::Clone(Ptr& pOut)
	{
		RW* pRet = new RW;
		pOut.reset(pRet);

		pRet->m_sPath = m_sPath;
		pRet->Open(m_bRead);
	}

	void Block::BodyBase::RW::NextUtxoIn()
	{
		LoadInternal(m_pUtxoIn, m_pS[0], m_pGuardUtxoIn);
	}

	void Block::BodyBase::RW::NextUtxoOut()
	{
		LoadInternal(m_pUtxoOut, m_pS[1], m_pGuardUtxoOut);
	}

	void Block::BodyBase::RW::NextKernelIn()
	{
		LoadInternal(m_pKernelIn, m_pS[2], m_pGuardKernelIn);
	}

	void Block::BodyBase::RW::NextKernelOut()
	{
		LoadInternal(m_pKernelOut, m_pS[3], m_pGuardKernelOut);
	}

	void Block::BodyBase::RW::get_Start(BodyBase& body, SystemState::Sequence::Prefix& prefix)
	{
		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(m_pS[4]);

		ECC::Hash::Value hv;
		arc & hv;

		if (hv != Rules::get().Checksum)
			throw std::runtime_error("Block rules mismatch");

		arc & body;
		arc & prefix;
	}

	bool Block::BodyBase::RW::get_NextHdr(SystemState::Sequence::Element& elem)
	{
		std::FStream& s = m_pS[4];
		if (!s.IsDataRemaining())
			return false;

		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(s);
		arc & elem;

		return true;
	}

	void Block::BodyBase::RW::WriteIn(const Input& v)
	{
		WriteInternal(v, m_pS[0]);
	}

	void Block::BodyBase::RW::WriteIn(const TxKernel& v)
	{
		WriteInternal(v, m_pS[2]);
	}

	void Block::BodyBase::RW::WriteOut(const Output& v)
	{
		WriteInternal(v, m_pS[1]);
	}

	void Block::BodyBase::RW::WriteOut(const TxKernel& v)
	{
		WriteInternal(v, m_pS[3]);
	}

	void Block::BodyBase::RW::put_Start(const BodyBase& body, const SystemState::Sequence::Prefix& prefix)
	{
		WriteInternal(Rules::get().Checksum, m_pS[4]);
		WriteInternal(body, m_pS[4]);
		WriteInternal(prefix, m_pS[4]);
	}

	void Block::BodyBase::RW::put_NextHdr(const SystemState::Sequence::Element& elem)
	{
		WriteInternal(elem, m_pS[4]);
	}

	template <typename T>
	void Block::BodyBase::RW::LoadInternal(const T*& pPtr, std::FStream& s, typename T::Ptr* ppGuard)
	{
		if (s.IsDataRemaining())
		{
			ppGuard[0].swap(ppGuard[1]);
			//if (!ppGuard[0])
				ppGuard[0].reset(new T);

			yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(s);
			arc & *ppGuard[0];

			pPtr = ppGuard[0].get();
		}
		else
			pPtr = NULL;
	}

	template <typename T>
	void Block::BodyBase::RW::WriteInternal(const T& v, std::FStream& s)
	{
		yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> arc(s);
		arc & v;
	}

	void TxBase::IWriter::Dump(IReader&& r)
	{
		r.Reset();

		for (; r.m_pUtxoIn; r.NextUtxoIn())
			WriteIn(*r.m_pUtxoIn);
		for (; r.m_pUtxoOut; r.NextUtxoOut())
			WriteOut(*r.m_pUtxoOut);
		for (; r.m_pKernelIn; r.NextKernelIn())
			WriteIn(*r.m_pKernelIn);
		for (; r.m_pKernelOut; r.NextKernelOut())
			WriteOut(*r.m_pKernelOut);
	}

	bool TxBase::IWriter::Combine(IReader&& r0, IReader&& r1, const volatile bool& bStop)
	{
		IReader* ppR[] = { &r0, &r1 };
		return Combine(ppR, _countof(ppR), bStop);
	}

	bool TxBase::IWriter::Combine(IReader** ppR, int nR, const volatile bool& bStop)
	{
		for (int i = 0; i < nR; i++)
			ppR[i]->Reset();

		// Utxo
		while (true)
		{
			if (bStop)
				return false;

			const Input* pInp = NULL;
			const Output* pOut = NULL;
			int iInp, iOut;

			for (int i = 0; i < nR; i++)
			{
				const Input* pi = ppR[i]->m_pUtxoIn;
				if (pi && (!pInp || (*pInp > *pi)))
				{
					pInp = pi;
					iInp = i;
				}

				const Output* po = ppR[i]->m_pUtxoOut;
				if (po && (!pOut || (*pOut > *po)))
				{
					pOut = po;
					iOut = i;
				}
			}

			if (pInp)
			{
				if (pOut)
				{
					int n = pInp->cmp_CaM(*pOut);
					if (n > 0)
						pInp = NULL;
					else
						if (!n)
						{
							// skip both
							ppR[iInp]->NextUtxoIn();
							ppR[iOut]->NextUtxoOut();
							continue;
						}
				}
			}
			else
				if (!pOut)
					break;


			if (pInp)
			{
				WriteIn(*pInp);
				ppR[iInp]->NextUtxoIn();
			}
			else
			{
				WriteOut(*pOut);
				ppR[iOut]->NextUtxoOut();
			}
		}


		// Kernels
		while (true)
		{
			if (bStop)
				return false;

			const TxKernel* pInp = NULL;
			const TxKernel* pOut = NULL;
			int iInp, iOut;

			for (int i = 0; i < nR; i++)
			{
				const TxKernel* pi = ppR[i]->m_pKernelIn;
				if (pi && (!pInp || (*pInp > *pi)))
				{
					pInp = pi;
					iInp = i;
				}

				const TxKernel* po = ppR[i]->m_pKernelOut;
				if (po && (!pOut || (*pOut > *po)))
				{
					pOut = po;
					iOut = i;
				}
			}

			if (pInp)
			{
				if (pOut)
				{
					int n = pInp->cmp(*pOut);
					if (n > 0)
						pInp = NULL;
					else
						if (!n)
						{
							// skip both
							ppR[iInp]->NextUtxoIn();
							ppR[iOut]->NextUtxoOut();
							continue;
						}
				}
			}
			else
				if (!pOut)
					break;


			if (pInp)
			{
				WriteIn(*pInp);
				ppR[iInp]->NextKernelIn();
			}
			else
			{
				WriteOut(*pOut);
				ppR[iOut]->NextKernelOut();
			}
		}

		return true;
	}

	bool Block::BodyBase::IMacroWriter::CombineHdr(IMacroReader&& r0, IMacroReader&& r1, const volatile bool& bStop)
	{
		Block::BodyBase body0, body1;
		Block::SystemState::Sequence::Prefix prefix0, prefix1;
		Block::SystemState::Sequence::Element elem;

		r0.Reset();
		r0.get_Start(body0, prefix0);
		r1.Reset();
		r1.get_Start(body1, prefix1);

		body0.Merge(body1);
		put_Start(body0, prefix0);

		while (r0.get_NextHdr(elem))
		{
			if (bStop)
				return false;
			put_NextHdr(elem);
		}

		while (r1.get_NextHdr(elem))
		{
			if (bStop)
				return false;
			put_NextHdr(elem);
		}

		return true;
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

namespace std
{
	void ThrowIoError()
	{
#ifdef WIN32
		int nErrorCode = GetLastError();
#else // WIN32
		int nErrorCode = errno;
#endif // WIN32

		char sz[0x20];
		snprintf(sz, _countof(sz), "I/O Error=%d", nErrorCode);
		throw runtime_error(sz);
	}

	void TestNoError(const ios& obj)
	{
		if (obj.fail())
			ThrowIoError();
	}

	bool FStream::Open(const char* sz, bool bRead, bool bStrict /* = false */)
	{
		int mode = ios_base::binary;
		mode |= (bRead ? (ios_base::in | ios_base::ate) : (ios_base::out | ios_base::trunc));

		m_F.open(sz, (ios_base::openmode) mode);
		if (m_F.fail())
		{
			if (bStrict)
				ThrowIoError();
			return false;
		}

		if (bRead)
		{
			m_Remaining = m_F.tellg();
			m_F.seekg(0);
		}

		return true;
	}

	void FStream::Close()
	{
		if (m_F.is_open())
			m_F.close();
	}

	bool FStream::IsDataRemaining() const
	{
		return m_Remaining > 0;
	}

	void FStream::Restart()
	{
		m_Remaining += m_F.tellg();
		m_F.seekg(0);
	}

	void FStream::NotImpl()
	{
		throw runtime_error("not impl");
	}

	size_t FStream::read(void* pPtr, size_t nSize)
	{
		m_F.read((char*)pPtr, nSize);
		size_t ret = m_F.gcount();
		m_Remaining -= ret;

		if (ret != nSize)
			throw runtime_error("underflow");

		return ret;
	}

	size_t FStream::write(const void* pPtr, size_t nSize)
	{
		m_F.write((char*) pPtr, nSize);
		TestNoError(m_F);

		return nSize;
	}

	char FStream::getch()
	{
		char ch;
		read(&ch, 1);
		return ch;
	}

	char FStream::peekch() const
	{
		NotImpl();
		return 0;
	}

	void FStream::ungetch(char)
	{
		NotImpl();
	}

	void FStream::Flush()
	{
		m_F.flush();
		TestNoError(m_F);
	}

} // namespace std
