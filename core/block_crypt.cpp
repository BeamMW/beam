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
#include <sstream>
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
		CMP_MEMBER_EX(m_Commitment)
		return 0;
	}

	int Input::cmp(const Input& v) const
	{
		return Cast::Down<TxElement>(*this).cmp(v);
	}

	/////////////
	// MasterKey
	Key::IKdf::Ptr MasterKey::get_Child(Key::IKdf& kdf, Key::Index iSubkey)
	{
		Key::IKdf::Ptr pRes;
		ECC::HKdf::CreateChild(pRes, kdf, iSubkey);
		return pRes;
	}

	Key::IKdf::Ptr MasterKey::get_Child(const Key::IKdf::Ptr& pKdf, const Key::IDV& kidv)
	{
		Key::Index iSubkey = kidv.get_Subkey();
		if (!iSubkey)
			return pKdf; // by convention: scheme V0, Subkey=0 - is a master key

		if (Key::IDV::Scheme::BB21 == kidv.get_Scheme())
			return pKdf; // BB2.1 workaround

		return get_Child(*pKdf, iSubkey);
	}

	/////////////
	// SwitchCommitment
	SwitchCommitment::SwitchCommitment(const AssetID* pAssetID /* = nullptr */)
	{
		if (pAssetID && !(*pAssetID == Zero))
		{
			ECC::Oracle oracle;
			oracle
				<< "a-id"
				<< *pAssetID;

			ECC::Point pt;
			pt.m_Y = 0;

			do
			{
				oracle
					<< "a-gen"
					>> pt.m_X;
			}
			while (!m_hGen.ImportNnz(pt));
		}
		else
			m_hGen = Zero;
	}

	void SwitchCommitment::get_sk1(ECC::Scalar::Native& res, const ECC::Point::Native& comm0, const ECC::Point::Native& sk0_J)
	{
		ECC::Oracle()
			<< comm0
			<< sk0_J
			>> res;
	}

	void SwitchCommitment::AddValue(ECC::Point::Native& comm, Amount v) const
	{
		ECC::Tag::AddValue(comm, &m_hGen, v);
	}

	void SwitchCommitment::get_Hash(ECC::Hash::Value& hv, const Key::IDV& kidv)
	{
		Key::Index nScheme = kidv.get_Scheme();
		if (nScheme > Key::IDV::Scheme::V0)
		{
			if (Key::IDV::Scheme::BB21 == nScheme)
			{
				// BB2.1 workaround
				Key::IDV kidv2 = kidv;
				kidv2.set_Subkey(kidv.get_Subkey(), Key::IDV::Scheme::V0);
				kidv2.get_Hash(hv);
			}
			else
			{
				// newer scheme - account for the Value.
				// Make it infeasible to tamper with value for unknown blinding factor
				ECC::Hash::Processor()
					<< "kidv-1"
					<< kidv.m_Idx
					<< kidv.m_Type.V
					<< kidv.m_SubIdx
					<< kidv.m_Value
					>> hv;
			}
		}
		else
			kidv.get_Hash(hv); // legacy
	}

	void SwitchCommitment::CreateInternal(ECC::Scalar::Native& sk, ECC::Point::Native& comm, bool bComm, Key::IKdf& kdf, const Key::IDV& kidv) const
	{
		ECC::Hash::Value hv;
		get_Hash(hv, kidv);
		kdf.DeriveKey(sk, hv);

		comm = ECC::Context::get().G * sk;
		AddValue(comm, kidv.m_Value);

		ECC::Point::Native sk0_J = ECC::Context::get().J * sk;

		ECC::Scalar::Native sk1;
		get_sk1(sk1, comm, sk0_J);

		sk += sk1;
		if (bComm)
			comm += ECC::Context::get().G * sk1;
	}

	void SwitchCommitment::Create(ECC::Scalar::Native& sk, Key::IKdf& kdf, const Key::IDV& kidv) const
	{
		ECC::Point::Native comm;
		CreateInternal(sk, comm, false, kdf, kidv);
	}

	void SwitchCommitment::Create(ECC::Scalar::Native& sk, ECC::Point::Native& comm, Key::IKdf& kdf, const Key::IDV& kidv) const
	{
		CreateInternal(sk, comm, true, kdf, kidv);
	}

	void SwitchCommitment::Create(ECC::Scalar::Native& sk, ECC::Point& comm, Key::IKdf& kdf, const Key::IDV& kidv) const
	{
		ECC::Point::Native comm2;
		Create(sk, comm2, kdf, kidv);
		comm = comm2;
	}

	void SwitchCommitment::Recover(ECC::Point::Native& res, Key::IPKdf& pkdf, const Key::IDV& kidv) const
	{
		ECC::Hash::Value hv;
		get_Hash(hv, kidv);

		ECC::Point::Native sk0_J;
		pkdf.DerivePKeyJ(sk0_J, hv);
		pkdf.DerivePKeyG(res, hv);
		AddValue(res, kidv.m_Value);

		ECC::Scalar::Native sk1;
		get_sk1(sk1, res, sk0_J);

		res += ECC::Context::get().G * sk1;
	}

	/////////////
	// Output
	bool Output::IsValid(Height hScheme, ECC::Point::Native& comm) const
	{
		if (!comm.Import(m_Commitment))
			return false;

		SwitchCommitment sc(&m_AssetID);

		ECC::Oracle oracle;
		Prepare(oracle, hScheme);

		if (m_pConfidential)
		{
			if (m_Coinbase)
				return false; // coinbase must have visible amount

			if (m_pPublic)
				return false;

			return m_pConfidential->IsValid(comm, oracle, &sc.m_hGen);
		}

		if (!m_pPublic)
			return false;

		if (!(Rules::get().AllowPublicUtxos || m_Coinbase))
			return false;

		return m_pPublic->IsValid(comm, oracle, &sc.m_hGen);
	}

	void Output::operator = (const Output& v)
	{
		Cast::Down<TxElement>(*this) = v;
		m_Coinbase = v.m_Coinbase;
		m_RecoveryOnly = v.m_RecoveryOnly;
		m_Incubation = v.m_Incubation;
		m_AssetID = v.m_AssetID;
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
		CMP_MEMBER(m_RecoveryOnly)
		CMP_MEMBER(m_Incubation)
		CMP_MEMBER_EX(m_AssetID)
		CMP_MEMBER_PTR(m_pConfidential)
		CMP_MEMBER_PTR(m_pPublic)

		return 0;
	}

	void Output::Create(Height hScheme, ECC::Scalar::Native& sk, Key::IKdf& coinKdf, const Key::IDV& kidv, Key::IPKdf& tagKdf, bool bPublic /* = false */)
	{
		SwitchCommitment sc(&m_AssetID);
		sc.Create(sk, m_Commitment, coinKdf, kidv);

		ECC::Oracle oracle;
		Prepare(oracle, hScheme);

		ECC::RangeProof::CreatorParams cp;
		cp.m_Kidv = kidv;
		GenerateSeedKid(cp.m_Seed.V, m_Commitment, tagKdf);

		if (bPublic || m_Coinbase)
		{
			m_pPublic.reset(new ECC::RangeProof::Public);
			m_pPublic->m_Value = kidv.m_Value;
			m_pPublic->Create(sk, cp, oracle);
		}
		else
		{
			m_pConfidential.reset(new ECC::RangeProof::Confidential);
			m_pConfidential->Create(sk, cp, oracle, &sc.m_hGen);
		}
	}

	void Output::GenerateSeedKid(ECC::uintBig& seed, const ECC::Point& commitment, Key::IPKdf& tagKdf)
	{
		ECC::Hash::Processor() << commitment >> seed;

		ECC::Scalar::Native sk;
		tagKdf.DerivePKey(sk, seed);

		ECC::Hash::Processor() << sk >> seed;
	}

	void Output::Prepare(ECC::Oracle& oracle, Height hScheme) const
	{
		oracle << m_Incubation;

		if (hScheme >= Rules::get().pForks[1].m_Height)
		{
			oracle << m_Commitment;
		}
	}

	bool Output::Recover(Height hScheme, Key::IPKdf& tagKdf, Key::IDV& kidv) const
	{
		ECC::RangeProof::CreatorParams cp;
		GenerateSeedKid(cp.m_Seed.V, m_Commitment, tagKdf);

		ECC::Oracle oracle;
		Prepare(oracle, hScheme);

		bool bSuccess =
			m_pConfidential ? m_pConfidential->Recover(oracle, cp) :
			m_pPublic ? m_pPublic->Recover(cp) :
			false;

		if (bSuccess)
			// Skip further verification, assuming no need to fully reconstruct the commitment
			kidv = cp.m_Kidv;

		return bSuccess;
	}

	bool Output::VerifyRecovered(Key::IPKdf& coinKdf, const Key::IDV& kidv) const
	{
		// reconstruct the commitment
		ECC::Mode::Scope scope(ECC::Mode::Fast);

		ECC::Point::Native comm, comm2;
		if (!comm2.Import(m_Commitment))
			return false;

		SwitchCommitment(&m_AssetID).Recover(comm, coinKdf, kidv);

		comm = -comm;
		comm += comm2;
		
		return (comm == Zero);
	}

	void HeightAdd(Height& trg, Height val)
	{
		trg += val;
		if (trg < val)
			trg = Height(-1);
	}

	Height Output::get_MinMaturity(Height h) const
	{
		HeightAdd(h, m_Coinbase ? Rules::get().Maturity.Coinbase : Rules::get().Maturity.Std);
		HeightAdd(h, m_Incubation);
		return h;
	}

	/////////////
	// TxKernel
	bool TxKernel::Traverse(ECC::Hash::Value& hv, AmountBig::Type* pFee, ECC::Point::Native* pExcess, const TxKernel* pParent, const ECC::Hash::Value* pLockImage, const Height* pScheme) const
	{
		if (pScheme && (*pScheme < Rules::get().pForks[1].m_Height) && (m_CanEmbed || m_pRelativeLock))
			return false; // unsupported for that version

		if (pParent)
		{
			if (!m_CanEmbed && pScheme && (*pScheme >= Rules::get().pForks[1].m_Height)) // for older version embedding is implicitly allowed (though unlikely to be used)
				return false;

			// nested kernel restrictions
			if ((m_Height.m_Min > pParent->m_Height.m_Min) ||
				(m_Height.m_Max < pParent->m_Height.m_Max))
				return false; // parent Height range must be contained in ours.
		}

		uint8_t nFlags =
			((m_pHashLock || pLockImage) ? 1 : 0) |
			(m_pRelativeLock ? 2 : 0) |
			(m_CanEmbed ? 4 : 0);

		ECC::Hash::Processor hp;
		hp	<< m_Fee
			<< m_Height.m_Min
			<< m_Height.m_Max
			<< m_Commitment
			<< Amount(m_AssetEmission)
			<< nFlags;

		if (m_pHashLock || pLockImage)
		{
			if (!pLockImage)
			{
				ECC::Hash::Processor() << m_pHashLock->m_Preimage >> hv;
				pLockImage = &hv;
			}

			hp << *pLockImage;
		}

		if (m_pRelativeLock)
		{
			hp
				<< m_pRelativeLock->m_ID
				<< m_pRelativeLock->m_LockHeight;
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

			if (!v.Traverse(hv, pFee, pExcess ? &ptExcNested : nullptr, this, nullptr, pScheme))
				return false;

			hp << hv;
		}

		hp >> hv;

		if (pExcess)
		{
			ECC::Point::Native pt;
			if (!pt.ImportNnz(m_Commitment))
				return false;

			ptExcNested = -ptExcNested;
			ptExcNested += pt;

			if (!m_Signature.IsValid(hv, ptExcNested))
				return false;

			*pExcess += pt;

			if (m_AssetEmission)
			{
				if (!Rules::get().CA.Enabled)
					return false;

				if (pParent || !m_vNested.empty())
					return false; // Ban complex cases. Emission kernels must be simple

				const AssetID& aid = m_Commitment.m_X;
				if (aid == Zero)
				{
					assert(false); // Currently zero kernels are not allowed, but if we change this eventually - this will allow attacker to emit default asset (i.e. Beams).
					// hence - extra verification
					return false;
				}

				SwitchCommitment sc(&aid);
				assert(ECC::Tag::IsCustom(&sc.m_hGen));

				sc.m_hGen = -sc.m_hGen;

				if (Rules::get().CA.Deposit)
					sc.m_hGen += ECC::Context::get().m_Ipp.H_; // Asset is traded for beam!

				// In case of block validation with multiple asset instructions it's better to calculate this via MultiMac than multiplying each point separately
				Amount val;
				if (m_AssetEmission > 0)
					val = m_AssetEmission;
				else
				{
					val = -m_AssetEmission;
					sc.m_hGen = -sc.m_hGen;
				}

				ECC::Tag::AddValue(*pExcess, &sc.m_hGen, val);
			}
		}

		if (pFee)
			*pFee += uintBigFrom(m_Fee);

		return true;
	}

	size_t TxKernel::get_TotalCount() const
	{
		size_t ret = 1;

		for (auto it = m_vNested.begin(); m_vNested.end() != it; it++)
			ret += (*it)->get_TotalCount();

		return ret;
	}

	void TxKernel::get_Hash(Merkle::Hash& out, const ECC::Hash::Value* pLockImage /* = NULL */) const
	{
		Traverse(out, nullptr, nullptr, nullptr, pLockImage, nullptr);
	}

	bool TxKernel::IsValid(Height hScheme, AmountBig::Type& fee, ECC::Point::Native& exc) const
	{
		ECC::Hash::Value hv;
		return Traverse(hv, &fee, &exc, nullptr, nullptr, &hScheme);
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
		CMP_MEMBER(m_AssetEmission)

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

		CMP_MEMBER_PTR(m_pHashLock)
		CMP_MEMBER_PTR(m_pRelativeLock)

		return 0;
	}

	int TxKernel::HashLock::cmp(const HashLock& v) const
	{
		CMP_MEMBER_EX(m_Preimage)
		return 0;
	}

	int TxKernel::RelativeLock::cmp(const RelativeLock& v) const
	{
		CMP_MEMBER_EX(m_ID)
		CMP_MEMBER(m_LockHeight)
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
		m_AssetEmission = v.m_AssetEmission;
		ClonePtr(m_pHashLock, v.m_pHashLock);
		ClonePtr(m_pRelativeLock, v.m_pRelativeLock);

		m_vNested.resize(v.m_vNested.size());

		for (size_t i = 0; i < v.m_vNested.size(); i++)
			ClonePtr(m_vNested[i], v.m_vNested[i]);
	}

	/////////////
	// Transaction
	Transaction::FeeSettings::FeeSettings()
	{
		m_Output = 10;
		m_Kernel = 10;
	}

	Amount Transaction::FeeSettings::Calculate(const Transaction& t) const
	{
		size_t nKernels = 0;
		for (size_t i = 0; i < t.m_vKernels.size(); i++)
			nKernels += t.m_vKernels[i]->get_TotalCount();


		return
			m_Output * t.m_vOutputs.size() +
			m_Kernel * nKernels;
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

	int TxBase::CmpInOut(const Input& in, const Output& out)
	{
		return Cast::Down<TxElement>(in).cmp(out);
	}

	size_t TxVectors::Perishable::NormalizeP()
	{
		std::sort(m_vInputs.begin(), m_vInputs.end());
		std::sort(m_vOutputs.begin(), m_vOutputs.end());

		size_t nDel = 0;

		size_t i1 = 0;
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
						i1++;
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

	void TxVectors::Eternal::NormalizeE()
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
	// AmountBig
	namespace AmountBig
	{
		Amount get_Lo(const Type& x)
		{
			Amount res;
			x.ExportWord<1>(res);
			return res;
		}

		Amount get_Hi(const Type& x)
		{
			Amount res;
			x.ExportWord<0>(res);
			return res;
		}

		void AddTo(ECC::Point::Native& res, const Type& x)
		{
			ECC::Mode::Scope scope(ECC::Mode::Fast);

			if (get_Hi(x))
			{
				ECC::Scalar s;
				s.m_Value = x;
				res += ECC::Context::get().H_Big * s;
			}
			else
			{
				Amount lo = get_Lo(x);
				if (lo)
					res += ECC::Context::get().H * lo;
			}
		}

	} // namespace AmountBig


	/////////////
	// Block

	Rules g_Rules; // refactor this to enable more flexible acess for current rules (via TLS or etc.)
	Rules& Rules::get()
	{
		return g_Rules;
	}

	const Height Rules::HeightGenesis	= 1;
	const Amount Rules::Coin			= 100000000;

	Rules::Rules()
	{
		TreasuryChecksum = {
			0x5d, 0x9b, 0x18, 0x78, 0x9c, 0x02, 0x1a, 0x1e,
			0xfb, 0x83, 0xd9, 0x06, 0xf4, 0xac, 0x7d, 0xce,
			0x99, 0x7d, 0x4a, 0xc5, 0xd4, 0x71, 0xd7, 0xb4,
			0x6f, 0x99, 0x77, 0x6e, 0x7a, 0xbd, 0x2e, 0xc9
		};

		Prehistoric = {
			// BTC Block #556833
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x25, 0x2d, 0x12, 0x33, 0xb4, 0x5d, 0xb2,
			0x39, 0x81, 0x47, 0x67, 0x6e, 0x16, 0x62, 0xf4,
			0x3c, 0x26, 0xa5, 0x26, 0xd2, 0xe2, 0x20, 0x63,
		};

		ZeroObject(pForks);

		pForks[1].m_Height = 270910; // testnet fork

		// future forks
		for (size_t i = 2; i < _countof(pForks); i++)
			pForks[i].m_Height = MaxHeight;
	}

	Amount Rules::get_EmissionEx(Height h, Height& hEnd, Amount base) const
	{
		h -= Rules::HeightGenesis; // may overflow, but it's ok. If h < HeightGenesis (which must not happen anyway) - then it'll give a huge height, for which the emission would be zero anyway.

		// Current emission strategy:
		// at Emission.Drop0 - 1/2
		// at Emission.Drop1 - 5/8
		// each Emission.Drop1 cycle - 1/2

		if (h < Emission.Drop0)
		{
			hEnd = Rules::HeightGenesis + Emission.Drop0;
			return base;
		}

		assert(Emission.Drop1);
		Height n = 1 + (h - Emission.Drop0) / Emission.Drop1;

		const uint32_t nBitsMax = sizeof(Amount) << 3;
		if (n >= nBitsMax)
		{
			hEnd = MaxHeight;
			return 0;
		}

		hEnd = Rules::HeightGenesis + Emission.Drop0 + n * Emission.Drop1;

		if (n >= 2)
			base += (base >> 2); // the unusual part - add 1/4

		return base >> n;
	}

	Amount Rules::get_Emission(Height h)
	{
		return get().get_EmissionEx(h, h, get().Emission.Value0);
	}

	void Rules::get_Emission(AmountBig::Type& res, const HeightRange& hr)
	{
		get_Emission(res, hr, get().Emission.Value0);
	}

	void Rules::get_Emission(AmountBig::Type& res, const HeightRange& hr, Amount base)
	{
		res = Zero;

		if (hr.IsEmpty())
			return;

		for (Height hPos = hr.m_Min; ; )
		{
			Height hEnd;
			Amount nCurrent = get().get_EmissionEx(hPos, hEnd, base);
			if (!nCurrent)
				break;

			assert(hEnd > hPos);

			if (hr.m_Max < hEnd)
			{
				res += uintBigFrom(nCurrent) * uintBigFrom(hr.m_Max - hPos + 1);
				break;
			}

			res += uintBigFrom(nCurrent) * uintBigFrom(hEnd - hPos);
			hPos = hEnd;
		}
	}

	bool Rules::IsForkHeightsConsistent() const
	{
		if (pForks[0].m_Height != Rules::HeightGenesis - 1)
			return false;

		for (size_t i = 1; i < _countof(pForks); i++)
			if (pForks[i].m_Height < pForks[i - 1].m_Height)
				return false;

		return true;
	}

	void Rules::UpdateChecksum()
	{
		if (!IsForkHeightsConsistent())
			throw std::runtime_error("Inconsistent Forks");

		// all parameters, including const (in case they'll be hardcoded to different values in later versions)
		ECC::Oracle oracle;
		oracle
			<< ECC::Context::get().m_hvChecksum
			<< Prehistoric
			<< TreasuryChecksum
			<< HeightGenesis
			<< Coin
			<< Emission.Value0
			<< Emission.Drop0
			<< Emission.Drop1
			<< Maturity.Coinbase
			<< Maturity.Std
			<< MaxBodySize
			<< FakePoW
			<< AllowPublicUtxos
			<< CA.Enabled
			<< CA.Deposit
			<< DA.Target_s
			<< DA.MaxAhead_s
			<< DA.WindowWork
			<< DA.WindowMedian0
			<< DA.WindowMedian1
			<< DA.Difficulty0.m_Packed
			<< MaxRollback
			<< uint32_t(720) // deprecated parameter
			<< (uint32_t) Block::PoW::K
			<< (uint32_t) Block::PoW::N
			<< (uint32_t) Block::PoW::NonceType::nBits
			<< uint32_t(15) // increment this whenever we change something in the protocol
#ifndef BEAM_TESTNET
			<< "masternet"
#endif
			// out
			>> pForks[0].m_Hash;

		oracle
			<< "fork1"
			<< pForks[1].m_Height
			<< DA.Damp.M
			<< DA.Damp.N
			// out
			>> pForks[1].m_Hash;

		oracle
			<< "fork2"
			<< pForks[2].m_Height
			// TBD
			// ...
			// out
			>> pForks[2].m_Hash;
	}

	const HeightHash* Rules::FindFork(const Merkle::Hash& hv) const
	{
		for (size_t i = _countof(pForks); i--; )
		{
			const HeightHash& x = pForks[i];

			if ((MaxHeight!= x.m_Height) && (x.m_Hash == hv))
				return pForks + i;
		}

		return nullptr;
	}

	const HeightHash& Rules::get_LastFork() const
	{
		size_t i = _countof(pForks);
		while (--i)
		{
			if (MaxHeight != pForks[i].m_Height)
				break;
		}

		return pForks[i];
	}

	std::string Rules::get_SignatureStr() const
	{
		std::ostringstream os;

		for (size_t i = 0; i < _countof(pForks); i++)
		{
			const HeightHash& x = pForks[i];
			if (MaxHeight == x.m_Height)
				break; // skip those

			if (i)
				os << ", ";

			os << x;
		}

		return os.str();
	}

	int HeightHash::cmp(const HeightHash& v) const
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
		if (m_Height >= Rules::HeightGenesis)
			get_HashInternal(hv, true);
		else
			hv = Rules::get().Prehistoric;
	}

	bool Block::SystemState::Full::IsSane() const
	{
		if (m_Height < Rules::HeightGenesis)
			return false;
		if ((m_Height == Rules::HeightGenesis) && !(m_Prev == Rules::get().Prehistoric))
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
		return m_PoW.IsValid(hv.m_pData, hv.nBytes, m_Height);
	}

	bool Block::SystemState::Full::GeneratePoW(const PoW::Cancel& fnCancel)
	{
		Merkle::Hash hv;
		get_HashForPoW(hv);

		return m_PoW.Solve(hv.m_pData, hv.nBytes, m_Height, fnCancel);
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
		ZeroObject(m_Offset);
	}

	void Block::BodyBase::Merge(const BodyBase& next)
	{
		ECC::Scalar::Native offs(m_Offset);
		offs += next.m_Offset;
		m_Offset = offs;
	}

	bool Block::BodyBase::IsValid(const HeightRange& hr, TxBase::IReader&& r) const
	{
		if ((hr.m_Min < Rules::HeightGenesis) || hr.IsEmpty())
			return false;

		TxBase::Context::Params pars;
		pars.m_bBlockMode = true;

		TxBase::Context ctx(pars);
		ctx.m_Height = hr;

		return
			ctx.ValidateAndSummarize(*this, std::move(r)) &&
			ctx.IsValidBlock();
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
	Block::Builder::Builder(Key::Index subIdx, Key::IKdf& coin, Key::IPKdf& tag, Height h)
		:m_SubIdx(subIdx)
		,m_Coin(coin)
		,m_Tag(tag)
		,m_Height(h)
	{
		m_Offset = Zero;
	}

	void Block::Builder::AddCoinbaseAndKrn(Output::Ptr& pOutp, TxKernel::Ptr& pKrn)
	{
		ECC::Scalar::Native sk;

		Amount val = Rules::get_Emission(m_Height);
		if (val)
		{
			pOutp.reset(new Output);
			pOutp->m_Coinbase = true;
			pOutp->Create(m_Height, sk, m_Coin, Key::IDV(val, m_Height, Key::Type::Coinbase, m_SubIdx), m_Tag);

			m_Offset += sk;
		}

		pKrn.reset(new TxKernel);
		pKrn->m_Height.m_Min = m_Height; // make it similar to others

		m_Coin.DeriveKey(sk, Key::ID(m_Height, Key::Type::Kernel2, m_SubIdx));
		pKrn->Sign(sk);
		m_Offset += sk;
	}

	void Block::Builder::AddCoinbaseAndKrn()
	{
		Output::Ptr pOutp;
		TxKernel::Ptr pKrn;
		AddCoinbaseAndKrn(pOutp, pKrn);

		if (pOutp)
			m_Txv.m_vOutputs.push_back(std::move(pOutp));
		if (pKrn)
			m_Txv.m_vKernels.push_back(std::move(pKrn));
	}

	void Block::Builder::AddFees(Amount fees, Output::Ptr& pOutp)
	{
		ECC::Scalar::Native sk;

		pOutp.reset(new Output);
		pOutp->Create(m_Height, sk, m_Coin, Key::IDV(fees, m_Height, Key::Type::Comission, m_SubIdx), m_Tag);

		m_Offset += sk;
	}

	void Block::Builder::AddFees(Amount fees)
	{
		if (fees)
		{
			Output::Ptr pOutp;
			AddFees(fees, pOutp);

			m_Txv.m_vOutputs.push_back(std::move(pOutp));
		}
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
