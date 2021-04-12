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
#include "serialization_adapters.h"

namespace beam
{
	/////////////
	// PeerID
	bool PeerID::ExportNnz(ECC::Point::Native& pt) const
	{
		ECC::Point pk;
		pk.m_X = Cast::Down<ECC::uintBig>(*this);
		pk.m_Y = 0;

		return pt.ImportNnz(pk);
	}

	bool PeerID::Import(const ECC::Point::Native& pt)
	{
		ECC::Point pk = pt;
		*this = pk.m_X;
		return !pk.m_Y;
	}

	void PeerID::FromSk(ECC::Scalar::Native& sk)
	{
		ECC::Point::Native pt = ECC::Context::get().G * sk;
		if (!Import(pt))
			sk = -sk;
	}

	/////////////
	// HeightRange
	void HeightRange::Reset()
	{
		m_Min = 0;
		m_Max = MaxHeight;
	}

	void HeightRange::Intersect(const HeightRange& x)
	{
		std::setmax(m_Min, x.m_Min);
		std::setmin(m_Max, x.m_Max);
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
	// TxStats
	void TxStats::Reset()
	{
		ZeroObject(*this);
	}

	void TxStats::operator += (const TxStats& s)
	{
		m_Fee				+= s.m_Fee;
		m_Coinbase			+= s.m_Coinbase;

		m_Kernels			+= s.m_Kernels;
		m_KernelsNonStd		+= s.m_KernelsNonStd;
		m_Inputs			+= s.m_Inputs;
		m_Outputs			+= s.m_Outputs;
		m_InputsShielded	+= s.m_InputsShielded;
		m_OutputsShielded	+= s.m_OutputsShielded;
		m_Contract			+= s.m_Contract;
		m_ContractSizeExtra	+= s.m_ContractSizeExtra;
	}

	/////////////
	// Input
	int TxElement::cmp(const TxElement& v) const
	{
		CMP_MEMBER_EX(m_Commitment)
		return 0;
	}

	void Input::operator = (const Input& v)
	{
		Cast::Down<TxElement>(*this) = v;
		m_Internal = v.m_Internal;
	}

	void Input::AddStats(TxStats& s) const
	{
		s.m_Inputs++;
	}

	/////////////
	// MasterKey
	Key::IKdf::Ptr MasterKey::get_Child(Key::IKdf& kdf, Key::Index iSubkey)
	{
		Key::IKdf::Ptr pRes;
		ECC::HKdf::CreateChild(pRes, kdf, iSubkey);
		return pRes;
	}

	/////////////
	// CoinID
	CoinID::Generator::Generator(Asset::ID aid)
	{
		if (aid)
			Asset::Base(aid).get_Generator(m_hGen);
	}

	void CoinID::Generator::AddValue(ECC::Point::Native& comm, Amount val) const
	{
		ECC::Tag::AddValue(comm, &m_hGen, val);
	}

	CoinID::Worker::Worker(const CoinID& cid)
		:Generator(cid.m_AssetID)
		,m_Cid(cid)
	{
	}

	bool CoinID::get_ChildKdfIndex(Key::Index& idx) const
	{
		Key::Index iSubkey = get_Subkey();
		if (!iSubkey)
			return false; // by convention: up to latest scheme, Subkey=0 - is a master key

		if (Scheme::BB21 == get_Scheme())
			return false; // BB2.1 workaround

		idx = iSubkey;
		return true;
	}

	Key::IKdf::Ptr CoinID::get_ChildKdf(const Key::IKdf::Ptr& pMasterKdf) const
	{
		Key::Index iIdx;
		if (!get_ChildKdfIndex(iIdx))
			return pMasterKdf;

		return MasterKey::get_Child(*pMasterKdf, iIdx);
	}

	void CoinID::Worker::get_sk1(ECC::Scalar::Native& res, const ECC::Point::Native& comm0, const ECC::Point::Native& sk0_J)
	{
		ECC::Oracle()
			<< comm0
			<< sk0_J
			>> res;
	}

	void CoinID::Worker::AddValue(ECC::Point::Native& comm) const
	{
		Generator::AddValue(comm, m_Cid.m_Value);
	}

	void CoinID::get_Hash(ECC::Hash::Value& hv) const
	{
		Key::Index nScheme = get_Scheme();
		if (nScheme > Scheme::V0)
		{
			if (Scheme::BB21 == nScheme)
			{
				// BB2.1 workaround
				CoinID cid2 = *this;
				cid2.set_Subkey(get_Subkey(), Scheme::V0);
				cid2.get_Hash(hv);
			}
			else
			{
				// newer scheme - account for the Value.
				// Make it infeasible to tamper with value or asset for unknown blinding factor
				ECC::Hash::Processor hp;
				hp
					<< "kidv-1"
					<< m_Idx
					<< m_Type.V
					<< m_SubIdx
					<< m_Value;

				if (m_AssetID)
				{
					hp
						<< "asset"
						<< m_AssetID;
				}

				hp >> hv;
			}
		}
		else
			Cast::Down<Key::ID>(*this).get_Hash(hv); // legacy
	}

	std::ostream& operator << (std::ostream& s, const CoinID& x)
	{
		s
			<< "Key=" << x.m_Type
			<< "-" << x.get_Scheme()
			<< ":" << x.get_Subkey()
			<< ":" << x.m_Idx
			<< ", Value=" << x.m_Value;

		if (x.m_AssetID)
			s << ", AssetID=" << x.m_AssetID;

		return s;
	}

	void CoinID::Worker::CreateInternal(ECC::Scalar::Native& sk, ECC::Point::Native& comm, bool bComm, Key::IKdf& kdf) const
	{
		ECC::Hash::Value hv;
		m_Cid.get_Hash(hv);
		kdf.DeriveKey(sk, hv);

		comm = ECC::Context::get().G * sk;
		AddValue(comm);

		ECC::Point::Native sk0_J = ECC::Context::get().J * sk;

		ECC::Scalar::Native sk1;
		get_sk1(sk1, comm, sk0_J);

		sk += sk1;
		if (bComm)
			comm += ECC::Context::get().G * sk1;
	}

	void CoinID::Worker::Create(ECC::Scalar::Native& sk, Key::IKdf& kdf) const
	{
		ECC::Point::Native comm;
		CreateInternal(sk, comm, false, kdf);
	}

	void CoinID::Worker::Create(ECC::Scalar::Native& sk, ECC::Point::Native& comm, Key::IKdf& kdf) const
	{
		CreateInternal(sk, comm, true, kdf);
	}

	void CoinID::Worker::Create(ECC::Scalar::Native& sk, ECC::Point& comm, Key::IKdf& kdf) const
	{
		ECC::Point::Native comm2;
		Create(sk, comm2, kdf);
		comm = comm2;
	}

	void CoinID::Worker::Recover(ECC::Point::Native& res, Key::IPKdf& pkdf) const
	{
		ECC::Hash::Value hv;
		m_Cid.get_Hash(hv);

		ECC::Point::Native sk0_J;
		pkdf.DerivePKeyJ(sk0_J, hv);
		pkdf.DerivePKeyG(res, hv);

		Recover(res, sk0_J);
	}

	void CoinID::Worker::Recover(ECC::Point::Native& pkG_in_res_out, const ECC::Point::Native& pkJ) const
	{
		AddValue(pkG_in_res_out);

		ECC::Scalar::Native sk1;
		get_sk1(sk1, pkG_in_res_out, pkJ);

		pkG_in_res_out += ECC::Context::get().G * sk1;
	}

	/////////////
	// Output
	bool Output::IsValid(Height hScheme, ECC::Point::Native& comm) const
	{
		if (!comm.Import(m_Commitment))
			return false;

		if (!m_pAsset)
			return IsValid2(hScheme, comm, nullptr);

		const Rules& r = Rules::get();
		if ((hScheme < r.pForks[2].m_Height) || !r.CA.Enabled)
			return false;

		ECC::Point::Native hGen;
		return
			m_pAsset->IsValid(hGen) &&
			IsValid2(hScheme, comm, &hGen);
	}

	bool Output::IsValid2(Height hScheme, ECC::Point::Native& comm, const ECC::Point::Native* pGen) const
	{
		ECC::Oracle oracle;
		Prepare(oracle, hScheme);

		if (m_pConfidential)
		{
			if (m_Coinbase)
				return false; // coinbase must have visible amount

			if (m_pPublic)
				return false;

			return m_pConfidential->IsValid(comm, oracle, pGen);
		}

		if (!m_pPublic)
			return false;

		if (m_Coinbase)
		{
			if (pGen)
				return false; // should contain unobscured default asset
		}
		else
		{
			if (!Rules::get().AllowPublicUtxos)
				return false;
		}

		if (!(Rules::get().AllowPublicUtxos || m_Coinbase))
			return false;

		return m_pPublic->IsValid(comm, oracle, pGen);
	}

	void Output::operator = (const Output& v)
	{
		Cast::Down<TxElement>(*this) = v;
		m_Coinbase = v.m_Coinbase;
		m_RecoveryOnly = v.m_RecoveryOnly;
		m_Incubation = v.m_Incubation;
		ClonePtr(m_pConfidential, v.m_pConfidential);
		ClonePtr(m_pPublic, v.m_pPublic);
		ClonePtr(m_pAsset, v.m_pAsset);
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
		CMP_MEMBER_PTR(m_pConfidential)
		CMP_MEMBER_PTR(m_pPublic)
		//CMP_MEMBER_PTR(m_pAsset)

		return 0;
	}

	void Output::AddStats(TxStats& s) const
	{
		s.m_Outputs++;

		if (m_Coinbase && m_pPublic)
			s.m_Coinbase += uintBigFrom(m_pPublic->m_Value);
	}

#pragma pack (push, 1)
	struct Output::PackedKA
	{
		uintBigFor<Asset::ID>::Type m_AssetID;
		Key::ID::Packed m_Kid; // for historical reasons: Key::ID should be last. All new data should be added above.
	};
#pragma pack (pop)

	void Output::Create(Height hScheme, ECC::Scalar::Native& sk, Key::IKdf& coinKdf, const CoinID& cid, Key::IPKdf& tagKdf, OpCode::Enum eOp, const User* pUser)
	{
		CoinID::Worker wrk(cid);

		bool bUseCoinKdf = true;
		switch (eOp)
		{
		case OpCode::Mpc_1:
		case OpCode::Mpc_2:
			bUseCoinKdf = false;
			break;

		default:
			wrk.Create(sk, m_Commitment, coinKdf);
		}

		ECC::Scalar::Native skSign = sk;
		if (cid.m_AssetID)
		{
			ECC::Hash::Value hv;
			if (!bUseCoinKdf)
				cid.get_Hash(hv);

			m_pAsset = std::make_unique<Asset::Proof>();
			m_pAsset->Create(wrk.m_hGen, skSign, cid.m_Value, cid.m_AssetID, wrk.m_hGen, bUseCoinKdf ? nullptr : &hv);
		}

		ECC::Oracle oracle;
		Prepare(oracle, hScheme);

		ECC::RangeProof::CreatorParams cp;
		cp.m_Value = cid.m_Value;
		GenerateSeedKid(cp.m_Seed.V, m_Commitment, tagKdf);

		if ((OpCode::Public == eOp) || m_Coinbase)
		{
			Key::ID::Packed kid;
			cp.m_Blob.p = &kid;
			cp.m_Blob.n = sizeof(kid);
			kid = cid;

			m_pPublic.reset(new ECC::RangeProof::Public);
			m_pPublic->m_Value = cid.m_Value;
			m_pPublic->Create(skSign, cp, oracle);
		}
		else
		{
			PackedKA kida;
			cp.m_Blob.p = &kida;
			cp.m_Blob.n = sizeof(kida);
			kida.m_Kid = cid;
			kida.m_AssetID = cid.m_AssetID;

			if (OpCode::Mpc_2 != eOp)
				m_pConfidential.reset(new ECC::RangeProof::Confidential);
			else
				assert(m_pConfidential);

			ECC::Scalar::Native pKExtra[_countof(pUser->m_pExtra)];
			if (pUser)
			{
				for (uint32_t i = 0; i < static_cast<uint32_t>(_countof(pUser->m_pExtra)); i++)
					pKExtra[i] = pUser->m_pExtra[i];

				cp.m_pExtra = pKExtra;
			}

			if (bUseCoinKdf)
				m_pConfidential->Create(skSign, cp, oracle, &wrk.m_hGen);
			else
			{
				ECC::RangeProof::Confidential::Nonces nonces; // not required, leave it zero (set in c'tor)

				if (OpCode::Mpc_1 == eOp)
				{
					ZeroObject(m_pConfidential->m_Part2);
					m_pConfidential->CoSign(nonces, skSign, cp, oracle, ECC::RangeProof::Confidential::Phase::Step2, &wrk.m_hGen); // stop after Part2
				}
				else
				{
					// by now Part2 and Part3 
					m_pConfidential->CoSign(nonces, skSign, cp, oracle, ECC::RangeProof::Confidential::Phase::Finalize, &wrk.m_hGen);
				}
			}
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

	bool Output::Recover(Height hScheme, Key::IPKdf& tagKdf, CoinID& cid, User* pUser) const
	{
		ECC::RangeProof::CreatorParams cp;
		GenerateSeedKid(cp.m_Seed.V, m_Commitment, tagKdf);

		ECC::Oracle oracle;
		Prepare(oracle, hScheme);

		if (m_pConfidential)
		{
			PackedKA kida;
			cp.m_Blob.p = &kida;
			cp.m_Blob.n = sizeof(kida);

			ECC::Scalar::Native pKExtra[_countof(pUser->m_pExtra)];
			if (pUser)
				cp.m_pExtra = pKExtra;

			if (!m_pConfidential->Recover(oracle, cp))
				return false;

			Cast::Down<Key::ID>(cid) = kida.m_Kid;
			kida.m_AssetID.Export(cid.m_AssetID);

			if (pUser)
			{
				for (uint32_t i = 0; i < static_cast<uint32_t>(_countof(pUser->m_pExtra)); i++)
					pUser->m_pExtra[i] = pKExtra[i];
			}
		}
		else
		{
			if (!m_pPublic)
				return false;

			Key::ID::Packed kid;
			cp.m_Blob.p = &kid;
			cp.m_Blob.n = sizeof(kid);

			if (!m_pPublic->Recover(cp))
				return false;

			Cast::Down<Key::ID>(cid) = kid;
			cid.m_AssetID = 0; // can't be recovered atm

			if (pUser)
				ZeroObject(*pUser); // can't be recovered
		}


		// Skip further verification, assuming no need to fully reconstruct the commitment
		cid.m_Value = cp.m_Value;

		return true;
	}

	bool Output::VerifyRecovered(Key::IPKdf& coinKdf, const CoinID& cid) const
	{
		// reconstruct the commitment
		ECC::Mode::Scope scope(ECC::Mode::Fast);

		ECC::Point::Native comm, comm2;
		if (!comm2.Import(m_Commitment))
			return false;

		CoinID::Worker(cid).Recover(comm, coinKdf);

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
	const ECC::Hash::Value& TxKernelStd::HashLock::get_Image(ECC::Hash::Value& hv) const
	{
		if (m_IsImage)
			return m_Value;

		ECC::Hash::Processor() << m_Value >> hv;
		return hv;
	}

	void TxKernel::HashBase(ECC::Hash::Processor& hp) const
	{
		hp	<< m_Fee
			<< m_Height.m_Min
			<< m_Height.m_Max;
	}

	void TxKernel::HashNested(ECC::Hash::Processor& hp) const
	{
		for (auto it = m_vNested.begin(); ; it++)
		{
			bool bBreak = (m_vNested.end() == it);
			hp << bBreak;

			if (bBreak)
				break;

			TxKernel& v = *(*it);
			v.UpdateID();

			hp << v.m_Internal.m_ID;
		}
	}

	void TxKernelStd::UpdateID()
	{
		m_Internal.m_HasNonStd = false;

		ECC::Hash::Processor hp;
		HashBase(hp);

		uint8_t nFlags =
			(m_pHashLock ? 1 : 0) |
			(m_pRelativeLock ? 2 : 0) |
			(m_CanEmbed ? 4 : 0);

		hp	<< m_Commitment
			<< Amount(0) // former m_AssetEmission
			<< nFlags;

		if (m_pHashLock)
		{
			ECC::Hash::Value hv;
			hp << m_pHashLock->get_Image(hv);
		}

		if (m_pRelativeLock)
		{
			hp
				<< m_pRelativeLock->m_ID
				<< m_pRelativeLock->m_LockHeight;
		}

		HashNested(hp);
		hp >> m_Internal.m_ID;

		for (auto it = m_vNested.begin(); m_vNested.end() != it; it++)
		{
			const TxKernel& v = *(*it);
			if (v.m_Internal.m_HasNonStd)
			{
				m_Internal.m_HasNonStd = true;
				break;
			}
		}
	}

	bool TxKernel::IsValidBase(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent, ECC::Point::Native* pComm) const
	{
		const Rules& r = Rules::get(); // alias
		if ((hScheme < r.pForks[1].m_Height) && m_CanEmbed)
			return false; // unsupported for that version

		if (pParent)
		{
			if (!m_CanEmbed)
				return false;

			// nested kernel restrictions
			if ((m_Height.m_Min > pParent->m_Height.m_Min) ||
				(m_Height.m_Max < pParent->m_Height.m_Max))
				return false; // parent Height range must be contained in ours.
		}

		if (!m_vNested.empty())
		{
			ECC::Point::Native excNested(Zero);

			const TxKernel* p0Krn = nullptr;
			for (auto it = m_vNested.begin(); m_vNested.end() != it; it++)
			{
				const TxKernel& v = *(*it);

				// sort for nested kernels is not important. But for 'historical' reasons it's enforced up to Fork2
				// Remove this code once Fork2 is reached iff no multiple nested kernels
				if ((hScheme < r.pForks[2].m_Height) && p0Krn && (*p0Krn > v))
					return false;
				p0Krn = &v;

				if (!v.IsValid(hScheme, excNested, this))
					return false;
			}

			if (hScheme < r.pForks[2].m_Height)
			{
				// Prior to Fork2 the parent commitment was supposed to include the nested. But nested kernels are unlikely to be seen up to Fork2.
				// Remove this code once Fork2 is reached iff no such kernels exist
				if (!pComm)
					return false;
				excNested = -excNested;
				(*pComm) += excNested;
			}
			else
				exc += excNested;
		}

		return true;
	}

#define THE_MACRO(id, name) \
	TxKernel::Subtype::Enum TxKernel##name::get_Subtype() const \
	{ \
		return Subtype::name; \
	}

	BeamKernelsAll(THE_MACRO)
#undef THE_MACRO


	bool TxKernelStd::IsValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		const Rules& r = Rules::get(); // alias
		if (m_pRelativeLock)
		{
			if (hScheme < r.pForks[1].m_Height)
				return false; // unsupported for that version

			if ((hScheme >= r.pForks[2].m_Height) && !m_pRelativeLock->m_LockHeight)
				return false; // zero m_LockHeight makes no sense, but allowed prior to Fork2
		}

		ECC::Point::Native pt;
		if (!pt.ImportNnz(m_Commitment))
			return false;

		exc += pt;

		if (!IsValidBase(hScheme, exc, pParent, &pt))
			return false;

		if (!m_Signature.IsValid(m_Internal.m_ID, pt))
			return false;

		return true;
	}

	void TxKernel::AddStats(TxStats& s) const
	{
		s.m_Kernels++;
		s.m_Fee += uintBigFrom(m_Fee);

		for (auto it = m_vNested.begin(); m_vNested.end() != it; it++)
			(*it)->AddStats(s);
	}

	int TxKernel::cmp(const TxKernel& v) const
	{
		CMP_MEMBER(m_Internal.m_HasNonStd)

		if (m_Internal.m_HasNonStd)
			return 0; // no sort for non-std kernels (always keep their order)

		Subtype::Enum t0 = get_Subtype();
		Subtype::Enum t1 = v.get_Subtype();
		CMP_SIMPLE(t0, t1)

		return cmp_Subtype(v);
	}

	int TxKernel::cmp_Subtype(const TxKernel&) const
	{
		return 0; // currentl unreachable: all kernels besides TxKernelStd are defined as 'non-standard', and should not be compared
	}

	int TxKernelStd::cmp_Subtype(const TxKernel& v_) const
	{
		const TxKernelStd& v = Cast::Up<TxKernelStd>(v_);

		CMP_MEMBER_EX(m_Commitment)
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

		CMP_MEMBER_PTR(m_pHashLock)
		CMP_MEMBER_PTR(m_pRelativeLock)

		return 0;
	}

	int TxKernelStd::HashLock::cmp(const HashLock& v) const
	{
		CMP_MEMBER_EX(m_Value)
		return 0;
	}

	int TxKernelStd::RelativeLock::cmp(const RelativeLock& v) const
	{
		CMP_MEMBER_EX(m_ID)
		CMP_MEMBER(m_LockHeight)
		return 0;
	}

	void TxKernelStd::Sign(const ECC::Scalar::Native& sk)
	{
		m_Commitment = ECC::Context::get().G * sk;
		UpdateID();
		m_Signature.Sign(m_Internal.m_ID, sk);
	}

	void TxKernel::CopyFrom(const TxKernel& v)
	{
		m_Internal = v.m_Internal;
		m_Fee = v.m_Fee;
		m_Height = v.m_Height;
		m_CanEmbed = v.m_CanEmbed;

		m_vNested.resize(v.m_vNested.size());

		for (size_t i = 0; i < v.m_vNested.size(); i++)
			v.m_vNested[i]->Clone(m_vNested[i]);
	}

	void TxKernelStd::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelStd);
		TxKernelStd& v = Cast::Up<TxKernelStd>(*p);

		v.CopyFrom(*this);
		v.m_Commitment = m_Commitment;
		v.m_Signature = m_Signature;
		ClonePtr(v.m_pHashLock, m_pHashLock);
		ClonePtr(v.m_pRelativeLock, m_pRelativeLock);
	}

	bool TxKernel::IWalker::Process(const std::vector<TxKernel::Ptr>& v)
	{
		for (size_t i = 0; i < v.size(); i++)
			if (!Process(*v[i]))
				return false;
		return true;
	}

	bool TxKernel::IWalker::Process(const TxKernel& krn)
	{
		bool bRet = 
			Process(krn.m_vNested) &&
			OnKrn(krn);

		if (bRet)
			m_nKrnIdx++;

		return bRet;
	}

	void TxKernelNonStd::UpdateID()
	{
		m_Internal.m_HasNonStd = true; // I'm non-standard already
		UpdateMsg();
		MsgToID();
	}

	void TxKernelNonStd::UpdateMsg()
	{
		ECC::Hash::Processor hp;

		HashBase(hp);

		ECC::Point comm(Zero); // invalid point, avoid collision with Std kernel, which provides the commitment here
		bool bFlag = m_CanEmbed && (m_Height.m_Min >= Rules::get().pForks[3].m_Height);
		comm.m_Y = bFlag ? 2 : 1;

		hp
			<< comm
			<< static_cast<uint32_t>(get_Subtype());

		HashNested(hp);
		HashSelfForMsg(hp);

		hp >> m_Msg;
	}

	void TxKernelNonStd::MsgToID()
	{
		ECC::Hash::Processor hp;
		hp << m_Msg;
		HashSelfForID(hp);
		hp >> m_Internal.m_ID;
	}

	void TxKernelNonStd::CopyFrom(const TxKernelNonStd& v)
	{
		TxKernel::CopyFrom(v);
		m_Msg = v.m_Msg;
	}

	void TxKernelNonStd::AddStats(TxStats& s) const
	{
		TxKernel::AddStats(s);
		s.m_KernelsNonStd++;
	}

	/////////////
	// TxKernelAssetControl
	void TxKernelAssetControl::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
		hp
			<< m_Commitment
			<< m_Owner;
	}

	void TxKernelAssetControl::HashSelfForID(ECC::Hash::Processor& hp) const
	{
		hp.Serialize(m_Signature);
	}

	bool TxKernelAssetControl::IsValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		if (!IsValidBase(hScheme, exc, pParent))
			return false;

		const Rules& r = Rules::get(); // alias
		if ((hScheme < r.pForks[2].m_Height) || !r.CA.Enabled)
			return false; // unsupported for that version

		ECC::Point::Native pPt[2];
		if (!pPt[0].ImportNnz(m_Commitment))
			return false;

		exc += pPt[0];

		if (!m_Owner.ExportNnz(pPt[1]))
			return false;

		assert(m_Owner != Zero); // the above ensures this

		// prover must prove knowledge of excess AND m_AssetID sk
		return m_Signature.IsValid(ECC::Context::get().m_Sig.m_CfgG2, m_Msg, m_Signature.m_pK, pPt);
	}

	void TxKernelAssetControl::CopyFrom(const TxKernelAssetControl& v)
	{
		TxKernelNonStd::CopyFrom(v);
		m_Commitment = v.m_Commitment;
		m_Signature = v.m_Signature;
		m_Owner = v.m_Owner;
	}

	void TxKernelAssetControl::Sign_(const ECC::Scalar::Native& sk, const ECC::Scalar::Native& skAsset)
	{
		m_Commitment = ECC::Context::get().G * sk;
		UpdateMsg();

		ECC::Scalar::Native pSk[2] = { sk, skAsset };
		ECC::Scalar::Native res;
		m_Signature.Sign(ECC::Context::get().m_Sig.m_CfgG2, m_Msg, m_Signature.m_pK, pSk, &res);

		MsgToID();
	}

	void TxKernelAssetControl::Sign(const ECC::Scalar::Native& sk, Key::IKdf& kdf, const Asset::Metadata& md)
	{
		ECC::Scalar::Native skAsset;
		kdf.DeriveKey(skAsset, md.m_Hash);

		m_Owner.FromSk(skAsset);

		Sign_(sk, skAsset);
	}

	void TxKernelAssetControl::get_Sk(ECC::Scalar::Native& sk, Key::IKdf& kdf)
	{
		m_Commitment = Zero;
		UpdateMsg();

		ECC::Hash::Processor()
			<< "ac.sk"
			<< m_Msg
			>> m_Msg;

		kdf.DeriveKey(sk, m_Msg);
	}

	/////////////
	// TxKernelAssetEmit
	void TxKernelAssetEmit::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
		TxKernelAssetControl::HashSelfForMsg(hp);
		hp
			<< m_AssetID
			<< Amount(m_Value);
	}

	bool TxKernelAssetEmit::IsValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		if (!TxKernelAssetControl::IsValid(hScheme, exc, pParent))
			return false;

		if (!m_Value || !m_AssetID)
			return false;

		CoinID::Generator g(m_AssetID);

		// In case of block validation with multiple asset instructions it's better to calculate this via MultiMac than multiplying each point separately
		Amount val;
		if (m_Value > 0)
		{
			val = m_Value;
			g.m_hGen = -g.m_hGen;
		}
		else
		{
			val = -m_Value;
		}

		g.AddValue(exc, val);

		return true;
	}

	void TxKernelAssetEmit::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelAssetEmit);
		TxKernelAssetEmit& v = Cast::Up<TxKernelAssetEmit>(*p);

		v.CopyFrom(*this);
		v.m_AssetID = m_AssetID;
		v.m_Value = m_Value;
	}

	/////////////
	// TxKernelAssetCreate
	bool TxKernelAssetCreate::IsValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		if (!TxKernelAssetControl::IsValid(hScheme, exc, pParent))
			return false;

		if (m_MetaData.m_Value.size() > Asset::Info::s_MetadataMaxSize)
			return false;

		ECC::Point::Native pt = ECC::Context::get().H * Rules::get().CA.DepositForList;
		exc += pt;

		return true;
	}

	void TxKernelAssetCreate::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelAssetCreate);
		TxKernelAssetCreate& v = Cast::Up<TxKernelAssetCreate>(*p);

		v.CopyFrom(*this);
		v.m_MetaData = m_MetaData;
	}

	void TxKernelAssetCreate::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
		TxKernelAssetControl::HashSelfForMsg(hp);
		hp << m_MetaData.m_Hash;
	}

	void TxKernelAssetCreate::Sign(const ECC::Scalar::Native& sk, Key::IKdf& kdf)
	{
		TxKernelAssetControl::Sign(sk, kdf, m_MetaData);
	}

	/////////////
	// TxKernelAssetDestroy
	void TxKernelAssetDestroy::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
		TxKernelAssetControl::HashSelfForMsg(hp);
		hp << m_AssetID;
	}

	bool TxKernelAssetDestroy::IsValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		if (!TxKernelAssetControl::IsValid(hScheme, exc, pParent))
			return false;

		ECC::Point::Native pt = ECC::Context::get().H * Rules::get().CA.DepositForList;

		pt = -pt;
		exc += pt;

		return true;
	}

	void TxKernelAssetDestroy::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelAssetDestroy);
		TxKernelAssetDestroy& v = Cast::Up<TxKernelAssetDestroy>(*p);

		v.CopyFrom(*this);
		v.m_AssetID = m_AssetID;
	}

	/////////////
	// TxKernelShieldedOutput
	bool TxKernelShieldedOutput::IsValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		if (!IsValidBase(hScheme, exc, pParent))
			return false;

		const Rules& r = Rules::get(); // alias
		if ((hScheme < r.pForks[2].m_Height) || !r.Shielded.Enabled)
			return false; // unsupported for that version

		if (m_Txo.m_pAsset && !r.CA.Enabled)
			return false; // unsupported for that version

		ECC::Oracle oracle;
		oracle << m_Msg;

		ECC::Point::Native comm, ser;
		if (!m_Txo.IsValid(oracle, comm, ser))
			return false;

		exc += comm;
		return true;
	}

	void TxKernelShieldedOutput::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
	}

	void TxKernelShieldedOutput::HashSelfForID(ECC::Hash::Processor& hp) const
	{
		hp.Serialize(m_Txo.m_RangeProof);
	}

	void TxKernelShieldedOutput::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelShieldedOutput);
		TxKernelShieldedOutput& v = Cast::Up<TxKernelShieldedOutput>(*p);

		v.CopyFrom(*this);
		v.m_Txo = m_Txo;
	}

	void TxKernelShieldedOutput::AddStats(TxStats& s) const
	{
		TxKernelNonStd::AddStats(s);
		s.m_OutputsShielded++;
	}

	/////////////
	// TxKernelShieldedInput
	bool TxKernelShieldedInput::IsValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		if (!IsValidBase(hScheme, exc, pParent))
			return false;

		const Rules& r = Rules::get(); // alias
		if ((hScheme < r.pForks[2].m_Height) || !r.Shielded.Enabled)
			return false; // unsupported for that version

		ECC::Point ptNeg = m_SpendProof.m_Commitment;
		ptNeg.m_Y = !ptNeg.m_Y; // probably faster than negating the result

		ECC::Point::Native comm;
		if (m_pAsset)
		{
			if (!r.CA.Enabled)
				return false;

			if (!m_pAsset->IsValid(comm))
				return false;
		}

		if (!comm.ImportNnz(ptNeg))
			return false;

		exc += comm;
		return true; // Spend proof verification is not done here
	}

	void TxKernelShieldedInput::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
		hp << m_WindowEnd;
	}

	void TxKernelShieldedInput::HashSelfForID(ECC::Hash::Processor& hp) const
	{
		hp.Serialize(m_SpendProof);
	}

	void TxKernelShieldedInput::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelShieldedInput);
		TxKernelShieldedInput& v = Cast::Up<TxKernelShieldedInput>(*p);

		v.CopyFrom(*this);
		v.m_WindowEnd = m_WindowEnd;
		v.m_SpendProof = m_SpendProof;
		v.m_NotSerialized = m_NotSerialized;

		if (m_pAsset)
			m_pAsset->Clone(v.m_pAsset);
		else
			v.m_pAsset.reset();
	}

	void TxKernelShieldedInput::AddStats(TxStats& s) const
	{
		TxKernelNonStd::AddStats(s);
		s.m_InputsShielded++;
	}

	void TxKernelShieldedInput::Sign(Lelantus::Prover& p, Asset::ID aid, bool bHideAssetAlways /* = false */)
	{
		ECC::Oracle oracle;
		oracle << m_Msg;

		if (m_Height.m_Min >= Rules::get().pForks[3].m_Height)
			oracle << m_NotSerialized.m_hvShieldedState;

		// auto-generate seed for sigma proof and m_R_Output
		ECC::NoLeak<ECC::uintBig> hvSeed;
		ECC::GenRandom(hvSeed.V); // use both deterministic and random

		Lelantus::Prover::Witness& w = p.m_Witness; // alias

		ECC::Oracle(oracle) // copy
			<< hvSeed.V
			<< w.m_L
			<< w.m_V
			<< w.m_R
			<< w.m_R_Output
			<< w.m_SpendSk
			>> hvSeed.V;

		ECC::NonceGenerator("krn.sh.i")
			<< hvSeed.V
			>> hvSeed.V;

		ECC::Point::Native hGen;
		if (aid)
			Asset::Base(aid).get_Generator(hGen);

		if (aid || bHideAssetAlways)
		{
			// not necessary for beams, just a demonstration of assets support
			m_pAsset = std::make_unique<Asset::Proof>();
			w.m_R_Adj = w.m_R_Output;
			m_pAsset->Create(hGen, w.m_R_Adj, w.m_V, aid, hGen, &hvSeed.V);
		}

		p.Generate(hvSeed.V, oracle, &hGen);

		MsgToID();
	}

	/////////////
	// TxKernelContractControl
	bool TxKernelContractControl::IsValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		if (!IsValidBase(hScheme, exc, pParent))
			return false;

		const Rules& r = Rules::get(); // alias
		if (hScheme < r.pForks[3].m_Height)
			return false; // unsupported for that version

		ECC::Point::Native comm;
		if (!comm.ImportNnz(m_Commitment))
			return false;

		exc += comm;
		return true; // the rest is deferred till the context-bound validation
	}

	void TxKernelContractControl::CopyFrom(const TxKernelContractControl& v)
	{
		TxKernelNonStd::CopyFrom(v);
		m_Commitment = v.m_Commitment;
		m_Signature = v.m_Signature;
		m_Args = v.m_Args;
	}

	void TxKernelContractControl::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
		hp
			<< m_Commitment
			<< m_Args.size()
			<< Blob(m_Args);
	}

	void TxKernelContractControl::HashSelfForID(ECC::Hash::Processor& hp) const
	{
		hp.Serialize(m_Signature);
	}

	void TxKernelContractControl::Sign(const ECC::Scalar::Native* pK, uint32_t nKeys, const ECC::Point::Native& ptFunds)
	{
		assert(nKeys);
		ECC::Point::Native pt = ECC::Context::get().G * pK[nKeys - 1];
		pt += ptFunds;
		m_Commitment = pt;

		UpdateMsg();

		ECC::Hash::Processor hp;
		hp << m_Msg;

		for (uint32_t i = 0; i + 1 < nKeys; i++)
		{
			pt = ECC::Context::get().G * pK[i];
			hp << pt;
		}

		ECC::Hash::Value hv;
		hp
			<< m_Commitment
			>> hv;

		ECC::SignatureBase::Config cfg = ECC::Context::get().m_Sig.m_CfgG1; // copy
		cfg.m_nKeys = nKeys;

		ECC::Scalar::Native res;
		ECC::SignatureBase& sig = m_Signature;
		sig.Sign(cfg, hv, &m_Signature.m_k, pK, &res);

		MsgToID();
	}

	void TxKernelContractControl::AddStats(TxStats& s) const
	{
		TxKernelNonStd::AddStats(s);
		s.m_Contract++;
		s.m_ContractSizeExtra += static_cast<uint32_t>(m_Args.size());
	}

	/////////////
	// TxKernelContractCreate
	void TxKernelContractCreate::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelContractCreate);
		TxKernelContractCreate& v = Cast::Up<TxKernelContractCreate>(*p);

		v.CopyFrom(*this);
		v.m_Data = m_Data;
	}

	void TxKernelContractCreate::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
		TxKernelContractControl::HashSelfForMsg(hp);
		hp
			<< m_Data.size()
			<< Blob(m_Data);
	}

	void TxKernelContractCreate::AddStats(TxStats& s) const
	{
		TxKernelContractControl::AddStats(s);
		s.m_ContractSizeExtra += static_cast<uint32_t>(m_Data.size());
	}

	/////////////
	// TxKernelContractInvoke
	void TxKernelContractInvoke::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelContractInvoke);
		TxKernelContractInvoke& v = Cast::Up<TxKernelContractInvoke>(*p);

		v.CopyFrom(*this);
		v.m_Cid = m_Cid;
		v.m_iMethod = m_iMethod;
	}

	void TxKernelContractInvoke::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
		TxKernelContractControl::HashSelfForMsg(hp);
		hp
			<< m_Cid
			<< m_iMethod;
	}

	/////////////
	// FeeSettings

	struct FeeSettingsGlobal
	{
		Transaction::FeeSettings m_BeforeHF3;
		Transaction::FeeSettings m_AfterHF3;

		FeeSettingsGlobal()
		{
			ZeroObject(*this);

			m_BeforeHF3.m_Output = 10;
			m_BeforeHF3.m_Kernel = 10;
			m_BeforeHF3.m_Default = 100;

			m_BeforeHF3.m_ShieldedInputTotal = Rules::Coin / 100;
			m_BeforeHF3.m_ShieldedOutputTotal = Rules::Coin / 100;

			m_AfterHF3.m_Output = 18000;
			m_AfterHF3.m_Kernel = 10000;
			m_AfterHF3.m_Default = 100000; // exactly covers 5 outputs + 1 kernel

			m_AfterHF3.m_ShieldedOutputTotal = Rules::Coin / 100;

			m_AfterHF3.m_Bvm.m_ChargeUnitPrice = 10; // 10 groth
			m_AfterHF3.m_Bvm.m_Minimum = 1000000; // 0.01 beam. This pays for 100K charge

			// the following is to mitigate spamming by excessive contract argumets + data size. Make it comparable to the spamming price by UTXOs
			// for UTXO the price is approximately 25 groth/byte
			m_AfterHF3.m_Bvm.m_ExtraSizeFree = 32768; // up to 32K arguments (+ bytecode for creation) are included in minimal fee. This is nearly 30 groth/byte
			m_AfterHF3.m_Bvm.m_ExtraBytePrice = 50;
		}

	} g_FeeSettingsGlobal;

	const Transaction::FeeSettings& Transaction::FeeSettings::get(Height h)
	{
		return (h >= Rules::get().pForks[3].m_Height) ?
			g_FeeSettingsGlobal.m_AfterHF3 :
			g_FeeSettingsGlobal.m_BeforeHF3;
	}

	Amount Transaction::FeeSettings::Calculate(const Transaction& t) const
	{
		TxStats s;
		t.get_Reader().AddStats(s);
		return Calculate(s);
	}

	Amount Transaction::FeeSettings::Calculate(const TxStats& s) const
	{
		Amount val =
			m_Output * s.m_Outputs +
			m_Kernel * (s.m_Kernels - s.m_InputsShielded - s.m_OutputsShielded) +
			m_ShieldedInputTotal * s.m_InputsShielded +
			m_ShieldedOutputTotal * s.m_OutputsShielded;

		uint32_t nContractSizeFree = m_Bvm.m_ExtraSizeFree * s.m_Contract;
		if (s.m_ContractSizeExtra > nContractSizeFree)
			val += m_Bvm.m_ExtraBytePrice *(s.m_ContractSizeExtra - nContractSizeFree);

		return val;
	}

	Amount Transaction::FeeSettings::CalculateForBvm(const TxStats& s, uint32_t nBvmCharge) const
	{
		Amount fee = m_Bvm.m_ChargeUnitPrice * nBvmCharge;
		Amount feeMin = m_Bvm.m_Minimum * s.m_Contract;

		return std::max(fee, feeMin);
	}

	Amount Transaction::FeeSettings::get_DefaultStd() const
	{
		return m_Default;
	}

	Amount Transaction::FeeSettings::get_DefaultShieldedOut(uint32_t nNumShieldedOutputs) const
	{
		return m_Default + m_ShieldedOutputTotal * nNumShieldedOutputs;
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
	    // Please do not rewrite to a shorter form.
	    // It is easy to debug/set breakpoints when code is like below
		if(!ctx.ValidateAndSummarize(*this, get_Reader()))
        {
		    return false;
        }

		if(!ctx.IsValidTransaction())
        {
		    return false;
        }
		
		return true;
	}

	void Transaction::get_Key(KeyType& key) const
	{
		// proper transactions must contain non-trivial offset, and this should be enough to identify it with sufficient probability
		// In case it's not specified - just ignore the collisions (means, part of those txs would not propagate)
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

	void TxBase::IReader::AddStats(TxStats& s)
	{
		Reset();

		for (; m_pUtxoIn; NextUtxoIn())
			m_pUtxoIn->AddStats(s);

		for (; m_pUtxoOut; NextUtxoOut())
			m_pUtxoOut->AddStats(s);

		for (; m_pKernel; NextKernel())
			m_pKernel->AddStats(s);
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
		m_E.m_vKernels.emplace_back();
		v.Clone(m_E.m_vKernels.back());
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

		void AddTo(ECC::Point::Native& res, const Type& x, const ECC::Point::Native& hGen)
        {
            ECC::Mode::Scope scope(ECC::Mode::Fast);
            ECC::Scalar s;
            s.m_Value = x;
			res += hGen * s;
        }
	} // namespace AmountBig


	/////////////
	// Rules

	Rules g_Rules; // refactor this to enable more flexible acess for current rules (via TLS or etc.)
	thread_local const Rules* g_pRulesOverride = nullptr;

	Rules& Rules::get()
	{
		return g_pRulesOverride ? Cast::NotConst(*g_pRulesOverride) : g_Rules;
	}

	Rules::Scope::Scope(const Rules& r) {
		m_pPrev = g_pRulesOverride;
		g_pRulesOverride = &r;
	}

	Rules::Scope::~Scope() {
		g_pRulesOverride = m_pPrev;
	}

	const Height Rules::HeightGenesis	= 1;

	Rules::Rules()
	{
		TreasuryChecksum = {
			0xcf, 0x9c, 0xc2, 0xdf, 0x67, 0xa2, 0x24, 0x19,
			0x2d, 0x2f, 0x88, 0xda, 0x20, 0x20, 0x00, 0xac,
			0x94, 0xb9, 0x11, 0x45, 0x26, 0x51, 0x3a, 0x8f,
			0xc0, 0x7c, 0xd2, 0x58, 0xcd, 0x7e, 0x50, 0x70,
		};

		Prehistoric = {
			// BTC Block #556833
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x25, 0x2d, 0x12, 0x33, 0xb4, 0x5d, 0xb2,
			0x39, 0x81, 0x47, 0x67, 0x6e, 0x16, 0x62, 0xf4,
			0x3c, 0x26, 0xa5, 0x26, 0xd2, 0xe2, 0x20, 0x63,
		};

		ZeroObject(pForks);

		pForks[1].m_Height = 30;
		pForks[2].m_Height = 30;
		pForks[3].m_Height = 1500;

		// future forks
		for (size_t i = 4; i < _countof(pForks); i++)
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

		if (!CA.m_ProofCfg.get_N())
			throw std::runtime_error("Bad CA/Sigma cfg");

		uint32_t n1 = Shielded.m_ProofMin.get_N();
		uint32_t n2 = Shielded.m_ProofMax.get_N();

		if (!n1 || (n1 > n2))
			throw std::runtime_error("Bad Shielded/Sigma cfg");

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
			<< false // deprecated CA.Enabled
			<< true // deprecated CA.Deposit
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
			<< Magic.v0; // increment this whenever we change something in the protocol

		if (!Magic.IsTestnet)
			oracle << "masternet";

		oracle
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
			<< MaxKernelValidityDH
			<< Shielded.Enabled
			<< Magic.v2; // increment this whenever we change something in the protocol

		// for historical reasons we didn't include Shielded.MaxIns and Shielded.MaxOuts
		if ((Shielded.MaxIns != 20) || (Shielded.MaxOuts != 30))
		{
			oracle
				<< Shielded.MaxIns
				<< Shielded.MaxOuts;
		}

		oracle
			<< Shielded.m_ProofMax.n
			<< Shielded.m_ProofMax.M
			<< Shielded.m_ProofMin.n
			<< Shielded.m_ProofMin.M
			<< Shielded.MaxWindowBacklog
			<< CA.Enabled
			<< CA.DepositForList
			<< CA.LockPeriod
			<< CA.m_ProofCfg.n
			<< CA.m_ProofCfg.M
			<< Asset::ID(Asset::s_MaxCount)
			// out
			>> pForks[2].m_Hash;

		oracle
			<< "fork3"
			<< pForks[3].m_Height
			<< (uint32_t) 4 // bvm version
			// TODO: bvm contraints
			>> pForks[3].m_Hash;
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

	uint32_t Rules::FindFork(Height h) const
	{
		for (uint32_t i = (uint32_t) _countof(pForks); i--; )
		{
			if (h >= pForks[i].m_Height)
				return i;
		}

		return 0; // should not be reached
	}

	Height Rules::get_ForkMaxHeightSafe(uint32_t iFork) const
	{
		assert(iFork < _countof(pForks));
		if (iFork + 1 < _countof(pForks))
		{
			Height h = pForks[iFork + 1].m_Height;
			if (h < MaxHeight)
				return h - 1;
		}

		return MaxHeight;
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

	void ExecutorMT_R::StartThread(std::thread& t, uint32_t iThread)
	{
		t = std::thread(&ExecutorMT_R::RunThreadInternal, this, iThread, Rules::get());
	}

	void ExecutorMT_R::RunThreadInternal(uint32_t iThread, const Rules& r)
	{
		Rules::Scope scopeRules(r);
		RunThread(iThread);
	}

	void ExecutorMT_R::RunThread(uint32_t iThread)
	{
		Context ctx;
		ctx.m_iThread = iThread;
		RunThreadCtx(ctx);
	}

	/////////////
	// Block

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

		// Starting from Fork2: add Rules cfg. Make it harder to tamper using headers mined on different cfg
		const Rules& r = Rules::get();
		auto iFork = r.FindFork(m_Height);
		if (iFork >= 2)
			hp << r.pForks[iFork].m_Hash;

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

	bool Block::SystemState::Evaluator::get_Definition(Merkle::Hash& hv)
	{
		Merkle::Hash hvHist;
		return Interpret(hv, hvHist, get_History(hvHist), hv, get_Live(hv));
	}

	void Block::SystemState::Evaluator::GenerateProof()
	{
		Merkle::Hash hvDummy;
		get_Definition(hvDummy);
	}

	bool Block::SystemState::Evaluator::get_History(Merkle::Hash& hv)
	{
		return OnNotImpl();
	}

	bool Block::SystemState::Evaluator::get_Live(Merkle::Hash& hv)
	{
		const Rules& r = Rules::get();

		if (m_Height >= r.pForks[3].m_Height)
		{
			Merkle::Hash hvCSA;
			return Interpret(hv, hv, get_Kernels(hv), hvCSA, get_CSA(hvCSA));
		}

		bool bUtxo = get_Utxos(hv);
		if (m_Height < r.pForks[2].m_Height)
			return bUtxo;

		Merkle::Hash hvSA;
		return Interpret(hv, hv, bUtxo, hvSA, get_SA(hvSA));
	}

	bool Block::SystemState::Evaluator::get_SA(Merkle::Hash& hv)
	{
		Merkle::Hash hvAssets;
		return Interpret(hv, hv, get_Shielded(hv), hvAssets, get_Assets(hvAssets));
	}

	bool Block::SystemState::Evaluator::get_CSA(Merkle::Hash& hv)
	{
		Merkle::Hash hvSA;
		return Interpret(hv, hv, get_Contracts(hv), hvSA, get_SA(hvSA));
	}

	bool Block::SystemState::Evaluator::get_Utxos(Merkle::Hash&)
	{
		return OnNotImpl();
	}

	bool Block::SystemState::Evaluator::get_Kernels(Merkle::Hash&)
	{
		return OnNotImpl();
	}

	bool Block::SystemState::Evaluator::get_Shielded(Merkle::Hash&)
	{
		return OnNotImpl();
	}

	bool Block::SystemState::Evaluator::get_Assets(Merkle::Hash&)
	{
		return OnNotImpl();
	}

	bool Block::SystemState::Evaluator::get_Contracts(Merkle::Hash&)
	{
		return OnNotImpl();
	}

	void ShieldedTxo::DescriptionOutp::get_Hash(Merkle::Hash& hv) const
	{
		ECC::Hash::Processor()
			<< "stxo-out"
			<< m_ID
			<< m_Height
			<< m_SerialPub
			<< m_Commitment
			>> hv;
	}

	void ShieldedTxo::DescriptionInp::get_Hash(Merkle::Hash& hv) const
	{
		ECC::Hash::Processor()
			<< "stxo-in"
			<< m_Height
			<< m_SpendPk
			>> hv;
	}

	struct Block::SystemState::Full::ProofVerifierHard
		:public Merkle::HardVerifier
		,public Evaluator
	{
		using HardVerifier::HardVerifier;

		bool Verify(const Full& s, uint64_t iIdx, uint64_t nCount)
		{
			if (!InterpretMmr(iIdx, nCount))
				return false;

			m_Height = s.m_Height;
			m_Verifier = true;

			GenerateProof();

			return
				IsEnd() &&
				(m_hv == s.m_Definition);
		}

		virtual void OnProof(Merkle::Hash&, bool bOnRight) override
		{
			InterpretOnce(!bOnRight);
		}
	};

	struct Block::SystemState::Full::ProofVerifier
		:public Evaluator
	{
		size_t m_Hashes = 0;
		const Merkle::Node* m_pNode = nullptr;

		bool VerifyKnownPartOrder(const Full& s, const Merkle::Proof& proof)
		{
			// deduce and verify the hashing direction of the last part
			m_Height = s.m_Height;
			m_Verifier = true;

			// Phase 1: count known elements
			GenerateProof();

			if (m_Hashes > proof.size())
				return false;
			if (!m_Hashes)
				return true; // nothing to check

			// Phase 2: verify order
			m_pNode = &proof.back() - (m_Hashes - 1);

			GenerateProof();
			return !m_Failed;
		}

		bool Verify(const Full& s, Merkle::Hash& hv, const Merkle::Proof& proof)
		{
			if (!VerifyKnownPartOrder(s, proof))
				return false;

			Merkle::Interpret(hv, proof);
			return hv == s.m_Definition;
		}

		virtual void OnProof(Merkle::Hash&, bool bOnRight) override
		{
			if (m_pNode)
			{
				if (m_pNode->first == bOnRight)
					m_Failed = true;

				m_pNode++;
			}
			else
				m_Hashes++;
		}
	};

	void Block::get_HashContractVar(Merkle::Hash& hv, const Blob& key, const Blob& val)
	{
		ECC::Hash::Processor()
			<< "beam.contract.val"
			<< key.n
			<< key
			<< val.n
			<< val
			>> hv;
	}

	bool Block::SystemState::Full::IsValidProofUtxo(const ECC::Point& comm, const Input::Proof& p) const
	{
		Merkle::Hash hv;
		p.m_State.get_ID(hv, comm);

		if (m_Height < Rules::get().pForks[3].m_Height)
		{
			struct MyVerifier
				:public ProofVerifier
			{
				virtual bool get_Utxos(Merkle::Hash&) override {
					return true;
				}
			} v;

			return v.Verify(*this, hv, p.m_Proof);
		}

		Merkle::Interpret(hv, p.m_Proof);
		return (hv == m_Kernels);
	}

	bool Block::SystemState::Full::IsValidProofShieldedOutp(const ShieldedTxo::DescriptionOutp& d, const Merkle::Proof& p) const
	{
		Merkle::Hash hv;
		d.get_Hash(hv);
		return IsValidProofShielded(hv, p);
	}

	bool Block::SystemState::Full::IsValidProofShieldedInp(const ShieldedTxo::DescriptionInp& d, const Merkle::Proof& p) const
	{
		Merkle::Hash hv;
		d.get_Hash(hv);
		return IsValidProofShielded(hv, p);
	}

	bool Block::SystemState::Full::IsValidProofShielded(Merkle::Hash& hv, const Merkle::Proof& p) const
	{
		struct MyVerifier
			:public ProofVerifier
		{
			virtual bool get_Shielded(Merkle::Hash&) override {
				return true;
			}
		} v;

		return v.Verify(*this, hv, p);
	}

	bool Block::SystemState::Full::IsValidProofAsset(const Asset::Full& ai, const Merkle::Proof& p) const
	{
		struct MyVerifier
			:public ProofVerifier
		{
			virtual bool get_Assets(Merkle::Hash&) override {
				return true;
			}
		} v;

		Merkle::Hash hv;
		ai.get_Hash(hv);
		return v.Verify(*this, hv, p);
	}

	bool Block::SystemState::Full::IsValidProofContract(const Blob& key, const Blob& val, const Merkle::Proof& p) const
	{
		struct MyVerifier
			:public ProofVerifier
		{
			virtual bool get_Contracts(Merkle::Hash&) override {
				return true;
			}
		} v;

		Merkle::Hash hv;
		get_HashContractVar(hv, key, val);
		return v.Verify(*this, hv, p);
	}

	bool Block::SystemState::Full::IsValidProofKernel(const TxKernel& krn, const TxKernel::LongProof& proof) const
	{
		return IsValidProofKernel(krn.m_Internal.m_ID, proof);
	}

	bool Block::SystemState::Full::IsValidProofKernel(const Merkle::Hash& hvID, const TxKernel::LongProof& proof) const
	{
		if (!proof.m_State.IsValid())
			return false;

		if (!proof.m_State.IsValidProofKernel(hvID, proof.m_Inner))
			return false;

		if (proof.m_State == *this)
			return true;
		if (proof.m_State.m_Height > m_Height)
			return false;

		ID id;
		proof.m_State.get_ID(id);
		return IsValidProofState(id, proof.m_Outer);
	}

	bool Block::SystemState::Full::IsValidProofKernel(const Merkle::Hash& hvID, const Merkle::Proof& proof) const
	{
		Merkle::Hash hv = hvID;
		if (m_Height < Rules::get().pForks[3].m_Height)
		{
			Merkle::Interpret(hv, proof);
			return (hv == m_Kernels);
		}
		struct MyVerifier
			:public ProofVerifier
		{
			virtual bool get_Kernels(Merkle::Hash&) override {
				return true;
			}
		} v;

		return v.Verify(*this, hv, proof);
	}

	bool Block::SystemState::Full::IsValidProofState(const ID& id, const Merkle::HardProof& proof) const
	{
		// verify the whole proof structure
		if ((id.m_Height < Rules::HeightGenesis) || (id.m_Height >= m_Height))
			return false;

		struct MyVerifier
			:public ProofVerifierHard
		{
			using ProofVerifierHard::ProofVerifierHard;

			virtual bool get_History(Merkle::Hash& hv) override {
				hv = m_hv;
				return true;
			}
		};

		MyVerifier hver(proof);
		hver.m_hv = id.m_Hash;
		return hver.Verify(*this, id.m_Height - Rules::HeightGenesis, m_Height - Rules::HeightGenesis);
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
			pOutp->Create(m_Height, sk, m_Coin, CoinID(val, m_Height, Key::Type::Coinbase, m_SubIdx), m_Tag);

			m_Offset += sk;
		}

		pKrn.reset(new TxKernelStd);
		pKrn->m_Height.m_Min = m_Height; // make it similar to others

		m_Coin.DeriveKey(sk, Key::ID(m_Height, Key::Type::Kernel2, m_SubIdx));
		Cast::Up<TxKernelStd>(*pKrn).Sign(sk);
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
		pOutp->Create(m_Height, sk, m_Coin, CoinID(fees, m_Height, Key::Type::Comission, m_SubIdx), m_Tag);

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

	std::ostream& operator << (std::ostream& s, const HeightHash& id)
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

	/////////////
	// Asset
	const PeerID Asset::s_InvalidOwnerID = Zero;

	void Asset::Base::get_Generator(ECC::Point::Native& res, ECC::Point::Storage& res_s) const
	{
		assert(m_ID);

		ECC::Point pt;
		pt.m_Y = 0;

		ECC::Oracle oracle;
		oracle
			<< "B.Asset.Gen.V1"
			<< m_ID;

		do
			oracle >> pt.m_X;
		while (!res.ImportNnz(pt, &res_s));
	}

	void Asset::Base::get_Generator(ECC::Point::Native& res) const
	{
		ECC::Point::Storage res_s;
		get_Generator(res, res_s);
	}

	void Asset::Base::get_Generator(ECC::Point::Storage& res_s) const
	{
		ECC::Point::Native res;
		get_Generator(res, res_s);
	}

	void Asset::Info::Reset()
	{
		m_Value = Zero;
		m_Owner = Zero;
		m_LockHeight = 0;
		m_Metadata.Reset();
	}

	bool Asset::Info::IsEmpty() const
	{
	    return m_Value == Zero && m_Owner == Zero && m_LockHeight == Zero && m_Metadata.m_Value.empty();
	}

	bool Asset::Info::IsValid() const
    {
	    return m_Owner != Zero && m_LockHeight != Zero;
    }

	void Asset::Full::get_Hash(ECC::Hash::Value& hv) const
	{
		ECC::Hash::Processor()
			<< "B.Asset.V1"
			<< m_ID
			<< m_Value
			<< m_Owner
			<< m_LockHeight
			<< m_Metadata.m_Hash
			>> hv;
	}

	void Asset::Metadata::Reset()
	{
		m_Value.clear();
		UpdateHash();
	}

	void Asset::Metadata::UpdateHash()
	{
		if (m_Value.empty())
		{
			m_Hash = Zero;
		}
		else
		{
			ECC::Hash::Processor()
				<< "B.AssetMeta"
				<< m_Value.size()
				<< Blob(m_Value)
				>> m_Hash;
		}
	}

	void Asset::Metadata::set_String(const std::string& s, bool bLegacy)
	{
		if (bLegacy)
		{
			Serializer ser;
			ser & s;
			ser.swap_buf(m_Value);
		}
		else
		{
			m_Value.resize(s.size());
			if (!s.empty())
				memcpy(&m_Value.front(), &s.front(), s.size());
		}
	}

	void Asset::Metadata::get_String(std::string& s) const
	{
		s.clear();
		if (m_Value.empty())
			return;

		// historically the metadata was a serialized string, which is [size][string]
		// The [size] was either a single byte with MSB on, or multi-byte encoding with leading byte in range [0x01 - 0x08]
		// Currently we just assign it to the string itself.
		uint8_t x = m_Value.front();
		bool bDeserialize = (0x80 & x) || (x <= 8);
		if (bDeserialize)
		{
			try
			{
				Deserializer der;
				der.reset(m_Value);
				der & s;

				return; // ok
			}
			catch (const std::exception&) {
				// ignore error, fallback to standard case
				s.clear();
			}
		}

		s.assign((const char*) &m_Value.front(), m_Value.size());
	}

	void Asset::Metadata::get_Owner(PeerID& res, Key::IPKdf& pkdf) const
	{
		ECC::Point::Native pt;
		pkdf.DerivePKeyG(pt, m_Hash);
		res.Import(pt);
	}

	bool Asset::Info::Recognize(Key::IPKdf& pkdf) const
	{
		PeerID pid;
		m_Metadata.get_Owner(pid, pkdf);
		return pid == m_Owner;
	}

	const ECC::Point::Compact& Asset::Proof::get_H()
	{
		return ECC::Context::get().m_Ipp.H_.m_Fast.m_pPt[0];
	}

	bool Asset::Proof::CmList::get_At(ECC::Point::Storage& pt_s, uint32_t iIdx)
	{
		Asset::ID id = m_Begin + iIdx;
		if (id)
			Base(id).get_Generator(pt_s);
		else
		{
			secp256k1_ge ge;
			get_H().Assign(ge);
			pt_s.FromNnz(ge);
		}

		return true;
	}

	void Asset::Proof::Create(ECC::Point::Native& genBlinded, ECC::Scalar::Native& skInOut, Amount val, Asset::ID aid, const ECC::Hash::Value* phvSeed)
	{
		ECC::Point::Native gen;
		if (aid)
			Base(aid).get_Generator(gen);

		Create(genBlinded, skInOut, val, aid, gen, phvSeed);
	}

	void Asset::Proof::Create(ECC::Point::Native& genBlinded, ECC::Scalar::Native& skInOut, Amount val, Asset::ID aid, const ECC::Point::Native& gen, const ECC::Hash::Value* phvSeed)
	{
		ECC::NonceGenerator nonceGen("out-sk-asset");
		ECC::NoLeak<ECC::Scalar> k;

		k.V = skInOut;
		nonceGen << k.V.m_Value;

		ECC::Hash::Processor hp;
		hp
			<< aid
			<< val;
		if (phvSeed)
			hp << (*phvSeed);

		hp >> k.V.m_Value;
		nonceGen << k.V.m_Value;

		ECC::Scalar::Native skAsset;
		nonceGen >> skAsset;

		ModifySk(skInOut, skAsset, val);

		Create(genBlinded, skAsset, aid, gen);
	}

	void Asset::Proof::Create(ECC::Point::Native& genBlinded, const ECC::Scalar::Native& skGen, Asset::ID aid, const ECC::Point::Native& gen)
	{
		if (aid)
			genBlinded = gen;
		else
			get_H().Assign(genBlinded, true); // not always specified explicitly for aid==0

		genBlinded += ECC::Context::get().G * skGen;
		m_hGen = genBlinded;

		uint32_t nPos = SetBegin(aid, skGen);

		CmList lst;
		lst.m_Begin = m_Begin;

		Sigma::Prover prover(lst, Rules::get().CA.m_ProofCfg, *this);
		prover.m_Witness.m_L = nPos;
		prover.m_Witness.m_R = -skGen;

		ECC::Hash::Value hvSeed;
		ECC::Hash::Processor()
			<< "asset-pr-gen"
			<< skGen
			>> hvSeed;

		ECC::Oracle oracle;
		oracle << m_hGen;
		prover.Generate(hvSeed, oracle, genBlinded);
	}

	void Asset::Proof::ModifySk(ECC::Scalar::Native& skInOut, const ECC::Scalar::Native& skGen, Amount val)
	{
		// modify the blinding factor, to keep the original commitment
		ECC::Scalar::Native k = skGen* val;
		k = -k;
		skInOut += k;
	}

	uint32_t Asset::Proof::SetBegin(Asset::ID aid, const ECC::Scalar::Native& skGen)
	{
		// Randomize m_Begin
		uint32_t N = Rules::get().CA.m_ProofCfg.get_N();
		assert(N);

		if (aid > N / 2)
		{
			ECC::Hash::Value hv;
			ECC::Hash::Processor() << skGen >> hv;

			uint32_t nPos;
			hv.ExportWord<0>(nPos);
			nPos %= N; // the position of this element in the list

			if (aid > nPos)
			{
				// TODO: don't exceed the max current asset count, for this we must query it
				m_Begin = aid - nPos;
				return nPos;
			}
		}

		m_Begin = 0;
		return aid;
	}

	bool Asset::Proof::IsValid(ECC::Point::Native& hGen, ECC::InnerProduct::BatchContext& bc, ECC::Scalar::Native* pKs) const
	{
		ECC::Oracle oracle;
		oracle << m_hGen;

		ECC::Scalar::Native kBias;
		if (!Cast::Down<Sigma::Proof>(*this).IsValid(bc, oracle, Rules::get().CA.m_ProofCfg, pKs, kBias))
			return false;

		if (!hGen.ImportNnz(m_hGen))
			return false;

		bc.AddCasual(hGen, kBias, true); // the deferred part from m_Signature, plus the needed bias
		return true;
	}

	bool Asset::Proof::IsValid(ECC::Point::Native& hGen) const
	{
		if (BatchContext::s_pInstance)
			return BatchContext::s_pInstance->IsValid(hGen, *this);

		ECC::Mode::Scope scope(ECC::Mode::Fast);

		ECC::InnerProduct::BatchContextEx<1> bc;
		std::vector<ECC::Scalar::Native> vKs;

		const Sigma::Cfg& cfg = Rules::get().CA.m_ProofCfg;
		uint32_t N = cfg.get_N();
		assert(N);
		vKs.resize(N);

		if (!IsValid(hGen, bc, &vKs.front()))
			return false;

		CmList lst;
		lst.m_Begin = m_Begin;

		lst.Calculate(bc.m_Sum, 0, N, &vKs.front());

		return bc.Flush();
	}

	void Asset::Proof::Clone(Ptr& p) const
	{
		p = std::make_unique<Proof>();
		*p = *this;
	}

	thread_local Asset::Proof::BatchContext* Asset::Proof::BatchContext::s_pInstance = nullptr;

} // namespace beam
