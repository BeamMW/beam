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
#include "../utility/logger.h"
#include "keccak.h"

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

	bool PeerID::FromSk(ECC::Scalar::Native& sk)
	{
		ECC::Point::Native pt = ECC::Context::get().G * sk;
		if (Import(pt))
			return true;

		sk = -sk;
		return false;
	}

	bool PeerID::CheckSignature(const Merkle::Hash& msg, const ECC::Signature& sig) const
	{
		ECC::Point::Native pt;
		return
			ExportNnz(pt) &&
			sig.IsValid(msg, pt);
	}

	/////////////
	// Evm::Address
	void Evm::Address::FromPubKey(const ECC::Point::Storage& pk)
	{
		KeccakProcessor<Word::nBits> hp;

		hp.Write(pk.m_X);
		hp.Write(pk.m_Y);

		Word w;
		hp.Read(w.m_pData);

		*this = W2A(w);
	}

	bool Evm::Address::FromPubKey(const ECC::Point& pk)
	{
		ECC::Point::Native pt_n;
		ECC::Point::Storage pt_s;
		if (!pt_n.ImportNnz(pk, &pt_s))
			return false;

		FromPubKey(pt_s);
		return true;
	}

	bool Evm::Address::FromPubKey(const PeerID& pid)
	{
		ECC::Point pt;
		pt.m_X = Cast::Down<ECC::uintBig>(pid);
		pt.m_Y = 0;
		return FromPubKey(pt);
	}

	void Evm::Address::FromPubKey(const ECC::Point::Native& pt)
	{
		ECC::Point::Storage pt_s;
		pt.Export(pt_s);
		FromPubKey(pt_s);
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

	void Block::NumberRange::Reset()
	{
		m_Min.v = 0;
		m_Max.v = MaxHeight;
	}

	bool Block::NumberRange::IsEmpty() const
	{
		return m_Min.v > m_Max.v;
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

	Input& Input::operator = (const Input& v)
	{
		Cast::Down<TxElement>(*this) = v;
		return *this;
	}

	Input& Input::operator = (Input&& v) noexcept
	{
		if (*this != v)
		{
			Cast::Down<TxElement>(*this) = std::move(v);
		}
		return *this;
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
		Key::Index iScheme = get_Scheme();

		if (iScheme < Scheme::V3)
		{
			if (!iSubkey)
				return false; // by convention: before V3 , Subkey=0 - is a master key

			if (Scheme::BB21 == iScheme)
				return false; // BB2.1 workaround
		}

		if ((Scheme::V_Miner0 == iScheme) && !iSubkey)
			return false;

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
		switch (nScheme)
		{
		case Scheme::V0:
			Cast::Down<Key::ID>(*this).get_Hash(hv); // legacy
			break;

		case Scheme::BB21:
			{
				// BB2.1 workaround
				CoinID cid2 = *this;
				cid2.set_Subkey(get_Subkey(), Scheme::V0);
				cid2.get_Hash(hv);
			}
			break;

		case Scheme::V_Miner0:
			{
				// miner0 workaround
				CoinID cid2 = *this;
				cid2.set_Subkey(get_Subkey(), Scheme::V3);
				cid2.get_Hash(hv);
			}
			break;

		default:
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
	}

	std::ostream& operator << (std::ostream& s, const CoinID& x)
	{
		s
			<< "Key=" << x.m_Type
			<< "-" << x.get_Scheme()
			<< ":" << x.get_Subkey()
			<< ":" << x.m_Idx
			<< ", Value=" << AmountBig::Printable(x.m_Value);

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

		if (!Rules::get().IsEnabledCA(hScheme))
			return false;

		ECC::Point::Native hGen;
		return
			m_pAsset->IsValid(hScheme, hGen) &&
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

		return m_pPublic->IsValid(comm, oracle, pGen);
	}

	Output& Output::operator = (const Output& v)
	{
		Cast::Down<TxElement>(*this) = v;
		m_Coinbase = v.m_Coinbase;
		m_Incubation = v.m_Incubation;
		ClonePtr(m_pConfidential, v.m_pConfidential);
		ClonePtr(m_pPublic, v.m_pPublic);
		ClonePtr(m_pAsset, v.m_pAsset);
		return *this;
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
		//CMP_MEMBER_PTR(m_pAsset)

		return 0;
	}

	void Output::AddStats(TxStats& s) const
	{
		s.m_Outputs++;

		if (m_Coinbase && m_pPublic)
			s.m_Coinbase += MultiWord::From(m_pPublic->m_Value);
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

		bool isPublic = (OpCode::Public == eOp) || m_Coinbase;

		ECC::Scalar::Native skSign = sk;
		if (!isPublic && Asset::Proof::Params::IsNeeded(cid.m_AssetID, hScheme))
		{
			ECC::Hash::Value hv;
			if (!bUseCoinKdf)
				cid.get_Hash(hv);

			m_pAsset = std::make_unique<Asset::Proof>();
			m_pAsset->Create(hScheme, wrk.m_hGen, skSign, cid.m_Value, cid.m_AssetID, wrk.m_hGen, bUseCoinKdf ? nullptr : &hv);
		}

		ECC::Oracle oracle;
		Prepare(oracle, hScheme);

		ECC::RangeProof::Params::Create cp;
		cp.m_Value = cid.m_Value;
		GenerateSeedKid(cp.m_Seed.V, m_Commitment, tagKdf);

		if (isPublic)
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

		if (Rules::get().IsPastFork_<1>(hScheme))
		{
			oracle << m_Commitment;
			Asset::Proof::Expose(oracle, hScheme, m_pAsset);
		}
	}

	bool Output::Recover(Height hScheme, Key::IPKdf& tagKdf, CoinID& cid, User* pUser) const
	{
		ECC::RangeProof::Params::Recover cp;
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
		if (m_Coinbase)
			HeightAdd(h, Rules::get().Maturity.Coinbase);
		HeightAdd(h, m_Incubation);
		return h;
	}

	Amount SplitAmountSigned(AmountSigned val, bool& isPositive)
	{
		isPositive = (val >= 0);
		if (isPositive)
			return val;

		return static_cast<Amount>(-val);
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
		for (auto it = m_vNested.begin(); ; ++it)
		{
			bool bBreak = (m_vNested.end() == it);
			hp << bBreak;

			if (bBreak)
				break;

			hp << (*it)->get_ID();
		}
	}

	bool TxKernelStd::HasNonStd() const
	{
		for (auto it = m_vNested.begin(); m_vNested.end() != it; ++it)
		{
			const TxKernel& v = *(*it);
			if (v.HasNonStd())
				return true;
		}

		return false;
	}

	void TxKernelStd::CalculateID() const
	{
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
		hp >> m_Lazy_ID.m_Value;
		m_Lazy_ID.m_Valid = true;
	}

	bool TxKernel::IsValid(Height hScheme, std::string* psErr /* = nullptr */) const
	{
		try {
			ECC::Point::Native exc;
			TestValid(hScheme, exc);
		} catch (const std::exception& e) {
			if (psErr)
				(*psErr) = e.what();
			return false;
		}

		return true;
	}

	HeightRange TxKernel::get_EffectiveHeightRange() const
	{
		HeightRange hr = m_Height;
		if (!hr.IsEmpty())
		{
			auto& r = Rules::get();
			if (r.IsPastFork_<2>(hr.m_Min) && (hr.m_Max - hr.m_Min > r.MaxKernelValidityDH))
				hr.m_Max = hr.m_Min + r.MaxKernelValidityDH;
		}

		return hr;
	}

	void TxKernel::TestValidBase(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent, ECC::Point::Native* pComm) const
	{
		const Rules& r = Rules::get(); // alias
		if (m_CanEmbed)
			r.TestForkAtLeast_<1>(hScheme);

		if (pParent)
		{
			if (!m_CanEmbed)
				Exc::Fail();

			// nested kernel restrictions
			if ((m_Height.m_Min > pParent->m_Height.m_Min) ||
				(m_Height.m_Max < pParent->m_Height.m_Max))
				Exc::Fail(); // parent Height range must be contained in ours.
		}

		if (!m_vNested.empty())
		{
			ECC::Point::Native excNested(Zero);

			const TxKernel* p0Krn = nullptr;
			for (auto it = m_vNested.begin(); m_vNested.end() != it; ++it)
			{
				const TxKernel& v = *(*it);
				TxKernel::Checkpoint cp(v);

				// sort for nested kernels is not important. But for 'historical' reasons it's enforced up to Fork2
				// Remove this code once Fork2 is reached iff no multiple nested kernels
				if (!r.IsPastFork_<2>(hScheme) && p0Krn && (*p0Krn > v))
					TxBase::Fail_Order();
				p0Krn = &v;

				v.TestValid(hScheme, excNested, this);
			}

			if (!r.IsPastFork_<2>(hScheme))
			{
				// Prior to Fork2 the parent commitment was supposed to include the nested. But nested kernels are unlikely to be seen up to Fork2.
				// Remove this code once Fork2 is reached iff no such kernels exist
				if (!pComm)
					Exc::Fail();
				excNested = -excNested;
				(*pComm) += excNested;
			}
			else
				exc += excNested;
		}
	}

#define THE_MACRO(id, name) \
	TxKernel::Subtype::Enum TxKernel##name::get_Subtype() const \
	{ \
		return Subtype::name; \
	}

	BeamKernelsAll(THE_MACRO)
#undef THE_MACRO


	void TxKernelStd::TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		const Rules& r = Rules::get(); // alias
		if (m_pRelativeLock)
		{
			r.TestForkAtLeast_<1>(hScheme);

			if (r.IsPastFork_<2>(hScheme) && !m_pRelativeLock->m_LockHeight)
				Exc::Fail(); // zero m_LockHeight makes no sense, but allowed prior to Fork2
		}

		ECC::Point::Native pt;
		pt.ImportNnzStrict(m_Commitment);

		exc += pt;

		TestValidBase(hScheme, exc, pParent, &pt);

		if (!m_Signature.IsValid(get_ID(), pt))
			TxBase::Fail_Signature();
	}

	void TxKernel::AddStats(TxStats& s) const
	{
		s.m_Kernels++;
		s.m_Fee += MultiWord::From(m_Fee);

		for (auto it = m_vNested.begin(); m_vNested.end() != it; ++it)
			(*it)->AddStats(s);
	}

	int TxKernel::cmp(const TxKernel& v) const
	{
		bool b1 = HasNonStd();
		bool b2 = v.HasNonStd();
		CMP_SIMPLE(b1, b2)

		if (b1)
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

		for ( ; m_vNested.end() != it0; ++it0, ++it1)
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
		CalculateID();

		m_Signature.Sign(m_Lazy_ID.get(), sk);
	}

	void TxKernel::CopyFrom(const TxKernel& v)
	{
		m_Lazy_ID = v.m_Lazy_ID;
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

	bool TxKernelNonStd::HasNonStd() const
	{
		return true;
	}

	void TxKernelNonStd::CalculateID() const
	{
		ECC::Hash::Processor hp;
		hp << get_Msg();
		HashSelfForID(hp);

		hp >> m_Lazy_ID.m_Value;
		m_Lazy_ID.m_Valid = true;
	}

	void TxKernelNonStd::CalculateMsg() const
	{
		ECC::Hash::Processor hp;

		HashBase(hp);

		ECC::Point comm(Zero); // invalid point, avoid collision with Std kernel, which provides the commitment here
		bool bFlag = m_CanEmbed && Rules::get().IsPastFork_<3>(m_Height.m_Min);
		comm.m_Y = bFlag ? 2 : 1;

		hp
			<< comm
			<< static_cast<uint32_t>(get_Subtype());

		HashNested(hp);
		HashSelfForMsg(hp);

		hp >> m_Lazy_Msg.m_Value;
		m_Lazy_Msg.m_Valid = true;

		m_Lazy_ID.Invalidate();
	}

	void TxKernelNonStd::CopyFrom(const TxKernelNonStd& v)
	{
		TxKernel::CopyFrom(v);
		m_Lazy_Msg = v.m_Lazy_Msg;
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

	void TxKernelAssetControl::TestValidAssetCtl(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent) const
	{
		TestValidBase(hScheme, exc, pParent);

		const Rules& r = Rules::get(); // alias
		r.TestForkAtLeast_<2>(hScheme);
		r.TestEnabledCA();

		ECC::Point::Native pPt[2];
		pPt[0].ImportNnzStrict(m_Commitment);

		exc += pPt[0];

		if (!m_Owner.ExportNnz(pPt[1]))
			ECC::Point::Native::Fail();

		assert(m_Owner != Zero); // the above ensures this

		// prover must prove knowledge of excess AND m_Owner sk
		if (!m_Signature.IsValid(ECC::Context::get().m_Sig.m_CfgG2, get_Msg(), m_Signature.m_pK, pPt))
			TxBase::Fail_Signature();
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
		CalculateMsg();

		ECC::Scalar::Native pSk[2] = { sk, skAsset };
		ECC::Scalar::Native res;
		m_Signature.Sign(ECC::Context::get().m_Sig.m_CfgG2, m_Lazy_Msg.get(), m_Signature.m_pK, pSk, &res);
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
		CalculateMsg();

		Merkle::Hash hv;

		ECC::Hash::Processor()
			<< "ac.sk"
			<< m_Lazy_Msg.get()
			>> hv;

		kdf.DeriveKey(sk, hv);
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

	void TxKernelAssetEmit::TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		TestValidAssetCtl(hScheme, exc, pParent);

		if (!m_Value)
			Exc::Fail();

		bool isPositive;
		auto val = SplitAmountSigned(m_Value, isPositive);

		if (!m_AssetID)
		{
			// this is allowed for bridged networks
			ECC::Mode::Scope scope(ECC::Mode::Fast);

			ECC::Point::Native pt = ECC::Context::get().H * val;
			if (isPositive)
				pt = -pt;

			exc += pt;
		}
		else
		{
			CoinID::Generator g(m_AssetID);

			// In case of block validation with multiple asset instructions it's better to calculate this via MultiMac than multiplying each point separately
			if (isPositive)
				g.m_hGen = -g.m_hGen;

			g.AddValue(exc, val);
		}
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
	void TxKernelAssetCreate::TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		TestValidAssetCtl(hScheme, exc, pParent);

		if (m_MetaData.m_Value.size() > Asset::Info::s_MetadataMaxSize)
			Exc::Fail();

		ECC::Point::Native pt = ECC::Context::get().H * Rules::get().get_DepositForCA(hScheme);
		exc += pt;
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

		if (IsCustomDeposit())
			hp << m_Deposit;
	}

	bool TxKernelAssetDestroy::IsCustomDeposit() const
	{
		return Rules::get().IsPastFork_<5>(m_Height.m_Min);
	}

	Amount TxKernelAssetDestroy::get_Deposit() const
	{
		return IsCustomDeposit() ? m_Deposit : Rules::get().CA.DepositForList2;
	}

	void TxKernelAssetDestroy::TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		TestValidAssetCtl(hScheme, exc, pParent);

		ECC::Point::Native pt = ECC::Context::get().H * get_Deposit();

		pt = -pt;
		exc += pt;
	}

	void TxKernelAssetDestroy::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelAssetDestroy);
		TxKernelAssetDestroy& v = Cast::Up<TxKernelAssetDestroy>(*p);

		v.CopyFrom(*this);
		v.m_AssetID = m_AssetID;
		v.m_Deposit = m_Deposit;
	}

	/////////////
	// TxKernelShieldedOutput
	void TxKernelShieldedOutput::TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		TestValidBase(hScheme, exc, pParent);

		const Rules& r = Rules::get(); // alias
		r.TestForkAtLeast_<2>(hScheme);
		r.TestEnabledShielded();

		if (m_Txo.m_pAsset)
			r.TestEnabledCA();

		ECC::Oracle oracle;
		oracle << get_Msg();

		ECC::Point::Native comm, ser;
		if (!m_Txo.IsValid(oracle, hScheme, comm, ser))
			TxBase::Fail_Signature();

		exc += comm;
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
	void TxKernelShieldedInput::TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		TestValidBase(hScheme, exc, pParent);

		const Rules& r = Rules::get(); // alias
		r.TestForkAtLeast_<2>(hScheme);
		r.TestEnabledShielded();

		ECC::Point ptNeg = m_SpendProof.m_Commitment;
		ptNeg.m_Y = !ptNeg.m_Y; // probably faster than negating the result

		ECC::Point::Native comm;
		if (m_pAsset)
		{
			r.TestEnabledCA();

			if (!m_pAsset->IsValid(hScheme, comm))
				TxBase::Fail_Signature();
		}

		comm.ImportNnzStrict(ptNeg);
		exc += comm;
		// Spend proof verification is not done here
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

	void TxKernelShieldedInput::Sign(Lelantus::Prover& p, Asset::ID aid)
	{
		ECC::Oracle oracle;
		oracle << get_Msg();

		if (Rules::get().IsPastFork_<3>(m_Height.m_Min))
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

		if (Asset::Proof::Params::IsNeeded(aid, m_Height.m_Min))
		{
			m_pAsset = std::make_unique<Asset::Proof>();
			w.m_R_Adj = w.m_R_Output;
			m_pAsset->Create(m_Height.m_Min, hGen, w.m_R_Adj, w.m_V, aid, hGen, &hvSeed.V);
		}

		Asset::Proof::Expose(oracle, m_Height.m_Min, m_pAsset);

		p.Generate(hvSeed.V, oracle, &hGen);

		m_Lazy_ID.Invalidate();
	}

	/////////////
	// TxKernelContractControl
	void TxKernelContractControl::TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		TestValidBase(hScheme, exc, pParent);

		const Rules& r = Rules::get(); // alias
		r.TestForkAtLeast_<3>(hScheme);

		ECC::Point::Native comm;
		comm.ImportNnzStrict(m_Commitment);
		exc += comm;
		// the rest is deferred till the context-bound validation
	}

	void TxKernelContractControl::CopyFrom(const TxKernelContractControl& v)
	{
		TxKernelNonStd::CopyFrom(v);
		m_Commitment = v.m_Commitment;
		m_Signature = v.m_Signature;
		m_Args = v.m_Args;
		m_Dependent = v.m_Dependent;
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

	void TxKernelContractControl::Prepare(ECC::Hash::Processor& hp, const Merkle::Hash* pParentCtx) const
	{
		hp << get_Msg();
		if (m_Dependent)
		{
			assert(pParentCtx);
			if (Rules::get().IsPastFork_<4>(m_Height.m_Min))
				hp << *pParentCtx;
		}
	}

	void TxKernelContractControl::Sign(const ECC::Scalar::Native* pK, uint32_t nKeys, const ECC::Point::Native& ptFunds, const Merkle::Hash* pParentCtx)
	{
		assert(nKeys);
		ECC::Point::Native pt = ECC::Context::get().G * pK[nKeys - 1];
		pt += ptFunds;
		m_Commitment = pt;

		CalculateMsg();

		ECC::Hash::Processor hp;
		Prepare(hp, pParentCtx);

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
	// TxKernelEvmInvoke
	void TxKernelEvmInvoke::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelEvmInvoke);
		TxKernelEvmInvoke& v = Cast::Up<TxKernelEvmInvoke>(*p);

		v.CopyFrom(*this);
		v.m_From = m_From;
		v.m_To = m_To;
		v.m_Nonce = m_Nonce;
		v.m_CallValue = m_CallValue;
		v.m_Subsidy = m_Subsidy;
	}

	void TxKernelEvmInvoke::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
		TxKernelContractControl::HashSelfForMsg(hp);
		hp
			<< Cast::Down<Evm::Address::Base>(m_From)
			<< Cast::Down<Evm::Address::Base>(m_To)
			<< m_Nonce
			<< m_CallValue
			<< static_cast<Amount>(m_Subsidy);
	}

	void TxKernelEvmInvoke::get_SubsidyCorrection(ECC::Point::Native& pt, bool isVerifying) const
	{
		if (m_Subsidy)
		{
			bool isPositive;
			Amount val = SplitAmountSigned(m_Subsidy, isPositive);

			ECC::Mode::Scope scope(ECC::Mode::Fast);

			ECC::Point::Native ptFunds = ECC::Context::get().H * val;
			if (isPositive == isVerifying)
				ptFunds = -ptFunds;

			pt += ptFunds;
		}
	}

	void TxKernelEvmInvoke::TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		TestValidBase(hScheme, exc, pParent);

		const Rules& r = Rules::get(); // alias
		r.TestForkAtLeast_<6>(hScheme);

		ECC::Point::Native pt;
		pt.ImportNnzStrict(m_Commitment);
		exc += pt;

		// Compute pubkey from the signature, and compare to the caller address
		get_SubsidyCorrection(pt, true);

		ECC::Point::Native ptPk;
		if (!m_Signature.RecoverPubKey(ECC::Context::get().m_Sig.m_CfgG2, get_Msg(), &m_Signature.m_k, &pt, 0, ptPk))
			TxBase::Fail_Signature();

		Evm::Address addr;
		addr.FromPubKey(ptPk);

		if (addr != m_From)
			TxBase::Fail_Signature();
	}

	void TxKernelEvmInvoke::Sign(const ECC::Scalar::Native& skFrom, const ECC::Scalar::Native& skBlind)
	{
		ECC::Point::Native pt = ECC::Context::get().G * skFrom;
		m_From.FromPubKey(pt);

		pt = ECC::Context::get().G * skBlind;
		get_SubsidyCorrection(pt, false);
		m_Commitment = pt;

		CalculateMsg();

		ECC::Scalar::Native pSk[] = { skFrom, skBlind };
		ECC::Scalar::Native nnc;

		Cast::Down<ECC::SignatureBase>(m_Signature).Sign(ECC::Context::get().m_Sig.m_CfgG2, m_Lazy_Msg.get(), &m_Signature.m_k, pSk, &nnc);
	}

	/////////////
	// TxKernelPbftUpdate
	void TxKernelPbftUpdate::TestValid(Height hScheme, ECC::Point::Native& exc, const TxKernel* pParent /* = nullptr */) const
	{
		if (pParent || !m_vNested.empty() || m_Fee)
			Exc::Fail();

		if (Rules::Consensus::Pbft != Rules::get().m_Consensus)
			Exc::Fail();
	}

	void TxKernelPbftUpdate::HashSelfForMsg(ECC::Hash::Processor& hp) const
	{
		hp
			<< "pbft.upd"
			<< m_Address
			<< m_Flags;
	}

	void TxKernelPbftUpdate::HashSelfForID(ECC::Hash::Processor&) const
	{
	}

	void TxKernelPbftUpdate::Clone(TxKernel::Ptr& p) const
	{
		p.reset(new TxKernelPbftUpdate);
		TxKernelPbftUpdate& v = Cast::Up<TxKernelPbftUpdate>(*p);

		v.CopyFrom(*this);
		v.m_Address = m_Address;
		v.m_Flags = m_Flags;
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
		return Rules::get().IsPastFork_<3>(h) ?
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
		// NOTE
		// By convention we should not reorder non-standard kernels, we only need to move the standard ones and sort them.
		// However std::sort is *NOT* guaranteed to keep the order of 'equal' elements. Means - it may reorder the non-standard kernels as well!
		//
		// It's possible to use std::stable_sort. But we prefer to manually split std/non-std, and then sort std normally.
		//std::stable_sort(m_vKernels.begin(), m_vKernels.end());

		size_t nStd = m_vKernels.size();
		for (size_t i = nStd; i--; )
		{
			auto& pKrn = m_vKernels[i];
			if (pKrn->HasNonStd())
			{
				nStd--;
				if (i != nStd)
					std::swap(pKrn, m_vKernels[nStd]);
			}
		}

		std::sort(m_vKernels.begin(), m_vKernels.begin() + nStd);
	}

	size_t TxVectors::Full::Normalize()
	{
		NormalizeE();
		return NormalizeP();
	}

	bool TxVectors::Full::IsEmpty() const
	{
		return
			m_vInputs.empty() &&
			m_vOutputs.empty() &&
			m_vKernels.empty();
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

	void Transaction::TestValid(Context& ctx) const
	{
		ctx.ValidateAndSummarizeStrict(*this, get_Reader());
		ctx.TestValidTransaction();
	}

	bool Transaction::IsValid(Context& ctx, std::string* psErr /* = nullptr */) const
	{
		try {
			TestValid(ctx);
		}
		catch (const std::exception& e) {
			if (psErr)
				(*psErr) = e.what();
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
		Amount get_Lo(const Number& x)
		{
			static_assert(sizeof(x) == sizeof(Amount) * 2);
			return x.get_Element<Amount, 0>();
		}

		Amount get_Hi(const Number& x)
		{
			return x.get_Element<Amount, 1>();
		}

		void AddTo(ECC::Point::Native& res, const Number& x)
		{
			ECC::Mode::Scope scope(ECC::Mode::Fast);

			if (get_Hi(x))
			{
				ECC::Scalar s;
				s.m_Value.FromNumber(x);
				res += ECC::Context::get().H * ECC::Scalar::Native(s);
			}
			else
			{
				Amount lo = get_Lo(x);
				if (lo)
					res += ECC::Context::get().H * lo;
			}
		}

		void AddTo(ECC::Point::Native& res, const Number& x, const ECC::Point::Native& hGen)
        {
            ECC::Mode::Scope scope(ECC::Mode::Fast);
            ECC::Scalar s;
            s.m_Value.FromNumber(x);
			res += hGen * s;
        }

		uint32_t Text::Print(char* sz, const Number& x, bool bTrimGroths)
		{
			auto out = MultiWord::Factorization::MakePrintOut<10>(sz, nLenMax);
			x.DecomposeEx<10>(out, true);
			return Expand(sz, out.m_pE, nLenMax - out.get_Reserve(), bTrimGroths);
		}

		uint32_t Text::Print(char* sz, Amount val, bool bTrimGroths)
		{
			auto out = MultiWord::Factorization::MakePrintOut<10>(sz, nLenMax);
			MultiWord::From(val).DecomposeEx<10>(out, true);
			return Expand(sz, out.m_pE, nLenMax - out.get_Reserve(), bTrimGroths);
		}

		void ExpandGroths(char*& szPos, const char* szSrc, uint32_t nSrc, bool bTrim)
		{
			assert(nSrc <= Text::nGrothDigits);
			uint32_t nZeroes = Text::nGrothDigits - nSrc;

			if (bTrim)
			{
				for (; ; nSrc--)
				{
					if (!nSrc)
						return;
					if ('0' != szSrc[nSrc - 1])
						break;
				}
			}

			*szPos++ = '.';
			while (nZeroes--)
				*szPos++ = '0';

			memmove(szPos, szSrc, nSrc);
			szPos += nSrc;
		}

		uint32_t Text::Expand(char* szDst, const char* szSrc, uint32_t nSrc, bool bTrimGroths)
		{
			assert(nSrc <= nLenUndecorated);
			char* szPos = szDst;

			if (nSrc > nGrothDigits)
			{
				uint32_t nDelta = nSrc - nGrothDigits;
				szPos += Group3Base::Expand(szPos, szSrc, nDelta);

				ExpandGroths(szPos, szSrc + nDelta, nGrothDigits, bTrimGroths);
			}
			else
			{
				*szPos++ = '0';
				ExpandGroths(szPos, szSrc, nSrc, bTrimGroths);
			}

			*szPos = 0;
			return static_cast<uint32_t>(szPos - szDst);
		}

		void Print(std::ostream& os, const Number& x, bool bTrim /* = true */)
		{
			char sz[Text::nLenMax + 1];
			Text::Print(sz, x, bTrim);
			os << sz;
		}

		void Print(std::ostream& os, Amount x, bool bTrim /* = true */)
		{
			char sz[Text::nLenMax + 1];
			Text::Print(sz, x, bTrim);
			os << sz;
		}

	} // namespace AmountBig

	uint32_t Group3Base::Expand(char* szDst, const char* szSrc, uint32_t nSrc)
	{
		char* szPos = szDst;
		if (nSrc)
		{
			while (true)
			{
				*szPos++ = *szSrc++;

				if (!--nSrc)
					break;
				if (!(nSrc % 3))
					*szPos++ = ',';
			}
		}

		*szPos = 0;
		return static_cast<uint32_t>(szPos - szDst);
	}


	/////////////
	// Rules
	const Rules& Rules::get()
	{
		assert(s_pInstance);
		if (!s_pInstance)
			Exc::Fail("no rules");

		return *s_pInstance;
	}

	Rules::Scope::Scope(const Rules& r) {
		m_pPrev = s_pInstance;
		s_pInstance = &r;
	}

	Rules::Scope::~Scope() {
		s_pInstance = m_pPrev;
	}

	Rules::Rules()
	{
		Prehistoric = {
			// BTC Block #556833
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x25, 0x2d, 0x12, 0x33, 0xb4, 0x5d, 0xb2,
			0x39, 0x81, 0x47, 0x67, 0x6e, 0x16, 0x62, 0xf4,
			0x3c, 0x26, 0xa5, 0x26, 0xd2, 0xe2, 0x20, 0x63,
		};

		// emission parameters
		Emission.Value0 = Coin * 80; // Initial emission. Each drop it will be halved. In case of odd num it's rounded to the lower value.
		Emission.Drop0 = 1440 * 365; // 1 year roughly. This is the height of the last block that still has the initial emission, the drop is starting from the next block
		Emission.Drop1 = 1440 * 365 * 4; // 4 years roughly. Each such a cycle there's a new drop

		// maturity
		Maturity.Coinbase = 240; // 4 hours

		// timestamp & difficulty.
		DA.Target_ms = 60'000; // 1 minute
		DA.WindowWork = 120; // 2 hours roughly (under normal operation)
		DA.MaxAhead_s = 60 * 15; // 15 minutes. Timestamps ahead by more than 15 minutes won't be accepted
		DA.WindowMedian0 = 25; // Timestamp for a block must be (strictly) higher than the median of preceding window
		DA.WindowMedian1 = 7; // Num of blocks taken at both endings of WindowWork, to pick medians.
		// damp factor. Adjustment of actual dt toward expected, effectively dampens
		DA.Damp.M = 1; // Multiplier of the actual dt
		DA.Damp.N = 3; // Denominator. The goal is multiplied by (N-M)

		// CA
		CA.Enabled = true;
		CA.DepositForList5 = Coin * 10; // after HF5
		CA.LockPeriod = 1440; // how long it's locked (can't be destroyed) after it was completely burned
		CA.m_ProofCfg = { 4, 3 }; // 4^3 = 64
		CA.ForeignEnd = 0;

		// Shielded
		Shielded.Enabled = true; // past Fork2
		Shielded.m_ProofMax = { 4, 8 }; // 4^8 = 64K
		Shielded.m_ProofMin = { 4, 5 }; // 4^5 = 1K

		// Max distance of the specified window from the tip where the prover is allowed to use m_ProofMax.
		// For proofs with bigger distance only m_ProofMin is supported
		Shielded.MaxWindowBacklog = 0x10000; // 64K
		// Hence "big" proofs won't need more than 128K most recent elements

		// max shielded ins/outs per block
		Shielded.MaxIns = 20; // input processing is heavy
		Shielded.MaxOuts = 30; // dust protection

		// other
		MaxRollback = 1440; // 1 day roughly
		MaxBodySize = 0x100000; // 1MB
		MaxKernelValidityDH = 1440 * 30; // past Fork2
		AllowPublicUtxos = false;
		Magic.v2 = 2;

		m_Pbft.m_RequiredWhite = 0;
		m_Pbft.m_AidStake = 0;

		// 1 eth == 10^18 wei
		// 1 beam == 10^8 groth
		// for equivalency we define  1 groth == 10^0 wei
		Evm.Groth2Wei = 0; // EVM is currently disabled by default
		//Evm.Groth2Wei = 10'000'000'000ull;

		Evm.BaseGasPrice = 10ull * 1'000'000'000ull; // 10 gwei
		Evm.MinTxGasUnits = 21000;

		m_Network = Network::mainnet;
		SetNetworkParams();
	}

	void Rules::SetNetworkParams()
	{
		// treasury
		switch (m_Network)
		{
		case Network::masternet:
		case Network::dappnet:
			TreasuryChecksum = {
				0xcf, 0x9c, 0xc2, 0xdf, 0x67, 0xa2, 0x24, 0x19,
				0x2d, 0x2f, 0x88, 0xda, 0x20, 0x20, 0x00, 0xac,
				0x94, 0xb9, 0x11, 0x45, 0x26, 0x51, 0x3a, 0x8f,
				0xc0, 0x7c, 0xd2, 0x58, 0xcd, 0x7e, 0x50, 0x70,
			};

			break;

		case Network::dappnet2:
			TreasuryChecksum = {
				0x18, 0x82, 0x11, 0xa9, 0xd5, 0x73, 0x5c, 0x2c,
				0x12, 0x0c, 0x68, 0xbd, 0x3f, 0x70, 0x49, 0xee,
				0x3e, 0x98, 0x6f, 0x4a, 0x0e, 0x0d, 0xe2, 0x48,
				0x58, 0xca, 0xd4, 0xcd, 0x35, 0x7a, 0x7a, 0x48,
			};

			break;

		case Network::l2_test1:
			TreasuryChecksum = {
				0xf2, 0xae, 0x15, 0xd6, 0x5b, 0x60, 0x70, 0x52,
				0xf4, 0xcc, 0x14, 0x16, 0x5a, 0x3b, 0x1a, 0x09,
				0x55, 0x00, 0xf3, 0xc1, 0x1c, 0x1f, 0x79, 0xa4,
				0x98, 0x17, 0x4b, 0x0e, 0xf0, 0x0a, 0xf1, 0xf2
			};

			break;

		default: // testnet, mainnet
			TreasuryChecksum = {
				0x5d, 0x9b, 0x18, 0x78, 0x9c, 0x02, 0x1a, 0x1e,
				0xfb, 0x83, 0xd9, 0x06, 0xf4, 0xac, 0x7d, 0xce,
				0x99, 0x7d, 0x4a, 0xc5, 0xd4, 0x71, 0xd7, 0xb4,
				0x6f, 0x99, 0x77, 0x6e, 0x7a, 0xbd, 0x2e, 0xc9
			};
		}

		// forks and misc
		Magic.v0 = 15;
		Magic.IsTestnet = false;
		m_Consensus = Consensus::PoW;
		CA.DepositForList2 = Coin * 3000; // after HF2
		DA.Difficulty0 = Difficulty(8 << Difficulty::s_MantissaBits); // 2^8 = 256

		ZeroObject(pForks);
		DisableForksFrom(6); // future forks

		switch (m_Network)
		{
		case Network::masternet:
			pForks[1].m_Height = 30;
			pForks[2].m_Height = 30;
			pForks[3].m_Height = 1500;
			pForks[4].m_Height = 516700;
			pForks[5].m_Height = 676330;

			break;

		case Network::testnet:

			pForks[1].m_Height = 270910;
			pForks[2].m_Height = 690000;
			pForks[3].m_Height = 1135300;
			pForks[4].m_Height = 1670000;
			pForks[5].m_Height = 1780000;

			Magic.IsTestnet = true;
			CA.DepositForList2 = Coin * 1000;
			break;

		case Network::dappnet:
			pForks[1].m_Height = 30;
			pForks[2].m_Height = 30;
			pForks[3].m_Height = 100;
			pForks[4].m_Height = 100;
			pForks[5].m_Height = 599999;

			m_Consensus = Consensus::FakePoW;
			break;

		case Network::dappnet2:

			SetParamsPbft(15'000);
			SetForksFrom(1, 1);

			m_Pbft.m_AidStake = 0;
			m_Pbft.m_RequiredWhite = 1;
			m_Pbft.m_vE.resize(1);

			for (auto& v : m_Pbft.m_vE)
			{
				v.m_Stake = Rules::Coin * 1000;
				v.m_White = true;
			}

			Cast::Down<ECC::uintBig>(m_Pbft.m_vE[0].m_Addr) = {
				0x5f, 0xa7, 0x39, 0x27, 0xfe, 0x4f, 0x7c, 0xd2,
				0xea, 0xb3, 0x81, 0xe2, 0xb6, 0x8a, 0x60, 0xa3,
				0xf7, 0xd6, 0x13, 0x45, 0xcf, 0x69, 0x9d, 0x5d,
				0xbf, 0xcf, 0x06, 0xc5, 0x5a, 0x74, 0x0d, 0x23
			};

			break;

		case Network::l2_test1:

			SetParamsPbft(3'000);
			SetForksFrom(1, 0);

			CA.ForeignEnd = 1'000'000;

			m_Pbft.m_AidStake = 0;
			m_Pbft.m_RequiredWhite = 2;
			m_Pbft.m_vE.resize(4);

			for (auto& v : m_Pbft.m_vE)
			{
				v.m_Stake = Rules::Coin * 1000;
				v.m_White = true;
			}

			Cast::Down<ECC::uintBig>(m_Pbft.m_vE[0].m_Addr) = {
				0x9b, 0x4c, 0xc0, 0xb1, 0xf4, 0x29, 0x26, 0xcc,
				0xbf, 0x51, 0xd6, 0x4e, 0xec, 0x99, 0xd1, 0x7f,
				0xde, 0x79, 0x42, 0x49, 0x04, 0xb2, 0x6b, 0x32,
				0xf0, 0x53, 0xfe, 0x13, 0xfe, 0xf5, 0xb0, 0x22
			};
			Cast::Down<ECC::uintBig>(m_Pbft.m_vE[1].m_Addr) = {
				0xde, 0x90, 0xc5, 0xe5, 0x01, 0x53, 0xe6, 0x52,
				0x1b, 0xe0, 0x34, 0x6a, 0x81, 0x88, 0x9c, 0x95,
				0x2d, 0x96, 0x61, 0x47, 0xe8, 0xc5, 0x96, 0x9b,
				0x4b, 0x5a, 0xc4, 0x93, 0x5c, 0x91, 0x03, 0x76
			};
			Cast::Down<ECC::uintBig>(m_Pbft.m_vE[2].m_Addr) = {
				0x9e, 0x88, 0x59, 0xdd, 0xf4, 0x52, 0x85, 0x8e,
				0x6c, 0xd0, 0x4e, 0x4c, 0xb1, 0x9c, 0x29, 0xa3,
				0x26, 0x10, 0x7a, 0x53, 0x3b, 0xf0, 0x72, 0xfa,
				0xd3, 0xe1, 0x7f, 0x1b, 0x41, 0xca, 0xcf, 0x4d
			};
			Cast::Down<ECC::uintBig>(m_Pbft.m_vE[3].m_Addr) = {
				0xa6, 0xf0, 0x90, 0x63, 0x88, 0x51, 0x88, 0x40,
				0x75, 0xb9, 0xfe, 0xab, 0xcd, 0x66, 0xc9, 0xd4,
				0x7d, 0x55, 0x11, 0xd8, 0x21, 0xc8, 0x60, 0x2f,
				0x48, 0x6b, 0x0a, 0x86, 0xfe, 0xb5, 0x42, 0x50
			};

			break;

		default: // mainnet
			pForks[1].m_Height = 321321;
			pForks[2].m_Height = 777777;
			pForks[3].m_Height = 1280000;
			pForks[4].m_Height = 1820000;
			pForks[5].m_Height = 1920000;

			Magic.v0 = 14;
			DA.Difficulty0 = Difficulty(22 << Difficulty::s_MantissaBits); // 2^22 = 4,194,304. For GPUs producing 7 sol/sec this is roughly equivalent to 10K GPUs.
		}
	}

	bool Rules::IsEnabledCA(Height hScheme) const
	{
		return IsPastFork_<2>(hScheme) && CA.Enabled;
	}

	Amount Rules::get_EmissionEx(Height h, Height& hEnd, Amount base) const
	{
		h--; // may overflow, but it's ok. If h is zero (which must not happen anyway) - then it'll give a huge height, for which the emission would be zero anyway.

		// Current emission strategy:
		// at Emission.Drop0 - 1/2
		// at Emission.Drop1 - 5/8
		// each Emission.Drop1 cycle - 1/2

		if (h < Emission.Drop0)
		{
			hEnd = Emission.Drop0 + 1;
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

		hEnd = Emission.Drop0 + n * Emission.Drop1 + 1;

		if (n >= 2)
			base += (base >> 2); // the unusual part - add 1/4

		return base >> n;
	}

	Amount Rules::get_Emission(Height h) const
	{
		return get_EmissionEx(h, h, Emission.Value0);
	}

	void Rules::get_Emission(AmountBig::Number& res, const HeightRange& hr) const
	{
		get_Emission(res, hr, Emission.Value0);
	}

	void Rules::get_Emission(AmountBig::Number& res, const HeightRange& hr, Amount base) const
	{
		res = Zero;

		if (hr.IsEmpty())
			return;

		for (Height hPos = hr.m_Min; ; )
		{
			Height hEnd;
			Amount nCurrent = get_EmissionEx(hPos, hEnd, base);
			if (!nCurrent)
				break;

			assert(hEnd > hPos);

			if (hr.m_Max < hEnd)
			{
				res += MultiWord::From(nCurrent) * MultiWord::From(hr.m_Max - hPos + 1);
				break;
			}

			res += MultiWord::From(nCurrent) * MultiWord::From(hEnd - hPos);
			hPos = hEnd;
		}
	}

	bool Rules::IsForkHeightsConsistent() const
	{
		if (pForks[0].m_Height)
			return false;

		for (size_t i = 1; i < _countof(pForks); i++)
			if (pForks[i].m_Height < pForks[i - 1].m_Height)
				return false;

		return true;
	}

	uint64_t Rules::Pbft::T2S(uint64_t t_ms) const
	{
		assert(Consensus::Pbft == get_ParentObj().m_Consensus);
		auto tSlot_ms = get_ParentObj().DA.Target_ms;
		if (!tSlot_ms)
			return 0; // ?!

		return t_ms / tSlot_ms;
	}

	uint64_t Rules::Pbft::T2S_strict(uint64_t t_ms) const
	{
		auto iSlot = T2S(t_ms);
		if (iSlot * get_ParentObj().DA.Target_ms != t_ms)
			return 0; // inaccurate timestamp

		return iSlot;
	}

	uint64_t Rules::Pbft::S2T(uint64_t iSlot) const
	{
		assert(Consensus::Pbft == get_ParentObj().m_Consensus);
		return iSlot * get_ParentObj().DA.Target_ms; // don't care about overflow
	}

	void Rules::UpdateChecksum()
	{
		Exc::CheckpointTxt cp("Rules");

		if (!IsForkHeightsConsistent())
			Exc::Fail("Inconsistent Forks");

		if (!CA.m_ProofCfg.get_N())
			Exc::Fail("Bad CA/Sigma cfg");

		uint32_t n1 = Shielded.m_ProofMin.get_N();
		uint32_t n2 = Shielded.m_ProofMax.get_N();

		if (!n1 || (n1 > n2))
			Exc::Fail("Bad Shielded/Sigma cfg");

		if (!IsConstantSpan())
		{
			if (DA.Difficulty0.m_Packed)
				Exc::Fail("Bad D0 for irregular height network");
		}

		// all parameters, including const (in case they'll be hardcoded to different values in later versions)
		ECC::Oracle oracle;
		oracle
			<< ECC::Context::get().m_hvChecksum
			<< Prehistoric
			<< TreasuryChecksum
			<< static_cast<Height>(1) // HeightGenesis
			<< Coin
			<< Emission.Value0
			<< Emission.Drop0
			<< Emission.Drop1
			<< Maturity.Coinbase
			<< static_cast<Height>(0) // Maturity.Std
			<< MaxBodySize
			<< (uint32_t) m_Consensus
			<< AllowPublicUtxos
			<< false // deprecated CA.Enabled
			<< true // deprecated CA.Deposit
			<< DA.get_Target_s()
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

		if (Rules::Consensus::Pbft == m_Consensus)
		{
			const auto& x = m_Pbft; // alias
			Exc::CheckpointTxt cp2("pbft");

			if (!DA.Target_ms)
				Exc::Fail("slot duration not specified");

			oracle
				<< "pbft.1"
				<< DA.Target_ms
				<< x.m_vE.size();

			Amount stakeTotal = 0;
			uint32_t nWhite = 0;

			for (const auto& e : x.m_vE)
			{
				oracle
					<< e.m_Addr
					<< e.m_Stake
					<< e.m_White;

				stakeTotal += e.m_Stake;
				if (stakeTotal < e.m_Stake)
					Exc::Fail("stake overflow");
				if (e.m_White)
					nWhite++;
			}

			if (!stakeTotal)
				Exc::Fail("no stake");
			if (nWhite < x.m_RequiredWhite)
				Exc::Fail("not enough white");
		}

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

		uint8_t flagCA =
			(CA.Enabled ? 1 : 0) |
			(CA.ForeignEnd ? 2 : 0);

		oracle
			<< Shielded.m_ProofMax.n
			<< Shielded.m_ProofMax.M
			<< Shielded.m_ProofMin.n
			<< Shielded.m_ProofMin.M
			<< Shielded.MaxWindowBacklog
			<< flagCA
			<< CA.DepositForList2
			<< CA.LockPeriod
			<< CA.m_ProofCfg.n
			<< CA.m_ProofCfg.M
			<< Asset::ID(Asset::s_MaxCount);

		if (CA.ForeignEnd)
			oracle << CA.ForeignEnd;

		oracle
			// out
			>> pForks[2].m_Hash;

		oracle
			<< "fork3"
			<< pForks[3].m_Height
			<< (uint32_t) 5 // bvm version
			// TODO: bvm contraints
			>> pForks[3].m_Hash;

		oracle
			<< "fork4"
			<< pForks[4].m_Height
			// no more flexible parameters so far
			>> pForks[4].m_Hash;

		oracle
			<< "fork5"
			<< pForks[5].m_Height
			<< CA.DepositForList5
			>> pForks[5].m_Hash;

		oracle
			<< "fork6"
			<< pForks[6].m_Height
			<< Evm.Groth2Wei
			<< Evm.BaseGasPrice
			<< Evm.MinTxGasUnits
			>> pForks[6].m_Hash;
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

	void Rules::SetForksFrom(uint32_t iBegin, Height h)
	{
		for (; iBegin < _countof(pForks); iBegin++)
			pForks[iBegin].m_Height = h;
	}

	void Rules::DisableForksFrom(uint32_t i)
	{
		SetForksFrom(i, MaxHeight);
	}

	void Rules::SetParamsPbft(uint32_t nTarget_ms)
	{
		m_Consensus = Consensus::Pbft;
		DA.Target_ms = nTarget_ms;
		DA.Difficulty0.m_Packed = 0;
	}

	Amount Rules::get_DepositForCA(Height hScheme) const
	{
		return IsPastFork_<5>(hScheme) ? CA.DepositForList5 : CA.DepositForList2;
	}

	const char* Rules::get_NetworkName() const
	{
		switch (m_Network)
		{
#define THE_MACRO(name) case Network::name: return #name;
			RulesNetworks(THE_MACRO)
#undef THE_MACRO
		}

		return "unspecified";
	}

	std::string Rules::get_SignatureStr() const
	{
		std::ostringstream os;
		os << "network=" << get_NetworkName();

		for (size_t i = 0; i < _countof(pForks); i++)
		{
			const HeightHash& x = pForks[i];
			if (MaxHeight == x.m_Height)
				break; // skip those

			os << "\n\t" << x;
		}

		return os.str();
	}

	void Rules::Fail_Fork(uint32_t iFork)
	{
		std::ostringstream ss;
		ss << "Fork required: " << iFork;
		Exc::Fail(ss.str().c_str());
	}

	void Rules::Height2Difficulty(Difficulty::Raw& d, Height h) const
	{
		assert(!IsConstantSpan());
		d = Zero;
		d.AssignRange<Height, Difficulty::s_MantissaBits>(h);
	}

	Difficulty Rules::Span2Difficulty(uint32_t n) const
	{
		assert(!IsConstantSpan());
		Difficulty d;
		d.PackLo(n);
		return d;
	}

	uint32_t Rules::Difficulty2Span(Difficulty d) const
	{
		assert(!IsConstantSpan());

		uint32_t ret;
		d.UnpackLo(ret);
		return ret;
	}

	int HeightHash::cmp(const HeightHash& v) const
	{
		CMP_MEMBER(m_Height)
		CMP_MEMBER_EX(m_Hash)
		return 0;
	}

	int HeightPos::cmp(const HeightPos& v) const
	{
		CMP_MEMBER(m_Height)
		CMP_MEMBER(m_Pos)
		return 0;
	}

	void ExecutorMT_R::StartThread(MyThread& t, uint32_t iThread)
	{
		t = MyThread(&ExecutorMT_R::RunThreadInternal, this, iThread, Rules::get());
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
	// Pbft
	void Block::Pbft::State::Clear()
	{
		while (!m_lstVs.empty())
			Delete(m_lstVs.front());

		m_Totals.m_Revenue = 0;
	}

	void Block::Pbft::State::Delete(Validator& v)
	{
		m_mapVs.erase(Validator::Addr::Map::s_iterator_to(v.m_Addr));
		m_lstVs.erase(Validator::List::s_iterator_to(v));
		delete &v;
	}

	Block::Pbft::Validator* Block::Pbft::State::Find(const Address& addr, bool bCreate)
	{
		auto it = m_mapVs.find(addr, Validator::Addr::Comparator());
		if (m_mapVs.end() != it)
			return &it->get_ParentObj();

		if (!bCreate)
			return nullptr;

		auto sp = CreateValidator();
		Validator* pV = sp.release();
		m_lstVs.push_back(*pV);

		pV->m_Addr.m_Key = addr;
		m_mapVs.insert(pV->m_Addr);

		pV->m_Weight = 0;
		pV->m_Flags = 0;
		return pV;
	}

	std::unique_ptr<Block::Pbft::Validator> Block::Pbft::Validator::Create()
	{
		return std::make_unique<Validator>();
	}

	std::unique_ptr<Block::Pbft::Validator> Block::Pbft::State::CreateValidator()
	{
		return Validator::Create();
	}

	uint64_t Block::Pbft::State::get_Random(ECC::Oracle& oracle, uint64_t nBound)
	{
		assert(nBound);

		uint64_t nResid = ((static_cast<uint64_t>(-1) % nBound) + 1) % nBound;

		while (true)
		{
			Merkle::Hash hv;
			oracle >> hv;

			uint64_t w;
			hv.ExportWord<0>(w);

			if (w <= static_cast<uint64_t>(-1) - nResid)
				return w % nBound;
		}

		// unreachable
	}

	uint64_t Block::Pbft::State::get_Weight() const
	{
		uint64_t wTotal = 0;
		for (auto it = m_lstVs.begin(); m_lstVs.end() != it; it++)
			wTotal += it->m_Weight;

		return wTotal;
	}

	Block::Pbft::Validator* Block::Pbft::State::SelectLeader(const Merkle::Hash& hvInp, uint32_t iRound)
	{
		uint64_t wUnjailed = 0;
		for (const auto& v : m_lstVs)
		{
			if (!(Validator::Flags::Jailed & v.m_Flags))
				wUnjailed += v.m_Weight;
		}

		if (!wUnjailed)
			return nullptr;

		// Select in a pseudo-random way. Probability according to the validator weight
		ECC::Oracle oracle;
		oracle
			<< "vs.select"
			<< hvInp
			<< iRound;

		uint64_t w = get_Random(oracle, wUnjailed);
		assert(w < wUnjailed);

		for (auto it = m_lstVs.begin(); ; it++)
		{
			assert(m_lstVs.end() != it);
			auto& v = *it;
			if (Validator::Flags::Jailed & v.m_Flags)
				continue;

			if (w < v.m_Weight)
				return &v;

			w -= v.m_Weight;
		}

		// unreachable
	}

	void Block::Pbft::State::get_Hash(Merkle::Hash& hv) const
	{
		ECC::Hash::Processor hp;
		hp
			<< "vs.state"
			<< m_Totals.m_Revenue
			<< m_lstVs.size();

		for (auto it = m_lstVs.begin(); m_lstVs.end() != it; it++)
		{
			const auto& v = *it;
			hp
				<< v.m_Addr.m_Key
				<< v.m_Weight
				<< v.m_Flags;
		}

		hp >> hv;
	}

	bool Block::Pbft::State::IsMajorityReached(uint64_t wVoted, uint64_t wTotal, uint32_t nWhite)
	{
		if (nWhite < Rules::get().m_Pbft.m_RequiredWhite)
			return false;

		assert(wVoted <= wTotal);
		return (wVoted * 3 > wTotal * 2); // TODO: overflow test
	}

	bool Block::Pbft::State::CheckQuorum(const Merkle::Hash& msg, const Quorum& qc)
	{
		// check all signatures, and that the quorum is reached
		uint64_t wTotal = 0, wVoted = 0;
		uint32_t iIdx = 0, iSig = 0, nWhite = 0;
		for (auto it = m_lstVs.begin(); m_lstVs.end() != it; it++, iIdx++)
		{
			auto& v = *it;
			wTotal += v.m_Weight;

			uint32_t iByte = iIdx / 8;
			if (iByte < qc.m_vValidatorsMsk.size())
			{
				uint8_t msk = 1 << (iIdx & 7);
				if (qc.m_vValidatorsMsk[iByte] & msk)
				{
					if (iSig >= qc.m_vSigs.size())
						return false;

					if (!v.m_Addr.m_Key.CheckSignature(msg, qc.m_vSigs[iSig++]))
						return false;

					wVoted += v.m_Weight;

					if (Validator::Flags::White & v.m_Flags)
						nWhite++;
				}
			}
		}

		if (iSig != qc.m_vSigs.size())
			return false; // not all sigs used

		// is quorum reached?
		return IsMajorityReached(wVoted, wTotal, nWhite);
	}

	void Block::Pbft::State::SetInitial()
	{
		const auto& pars = Rules::get().m_Pbft;
		for (const auto& v : pars.m_vE)
		{
			if (!v.m_Stake)
				continue;

			auto* pV = Find(v.m_Addr, true);
			assert(pV);

			pV->m_Weight += v.m_Stake;

			if (v.m_White)
				pV->m_Flags |= Validator::Flags::White;
		}
	}

	void Block::Pbft::State::ResolveWhitelisted(const Rules& r)
	{
		for (auto& v : m_lstVs)
			v.m_Flags &= ~Validator::Flags::White;

		for (const auto& x : r.m_Pbft.m_vE)
		{
			if (x.m_White)
			{
				auto* pVal = Find(x.m_Addr, false);
				if (pVal)
					pVal->m_Flags |= Validator::Flags::White;
			}
		}
	}

	void Block::Pbft::DeriveValidatorAddress(Key::IKdf& kdf, Block::Pbft::Address& addr, ECC::Scalar::Native& sk)
	{
		kdf.DeriveKey(sk, Key::ID(0, Key::Type::Coinbase));
		addr.FromSk(sk);
	}

	/////////////
	// Block

	int Block::SystemState::Full::cmp(const Full& v) const
	{
		CMP_MEMBER(m_Number.v)
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
		if (m_Number.v + 1 != sNext.m_Number.v)
			return false;

		if (m_ChainWork + sNext.m_PoW.m_Difficulty != sNext.m_ChainWork) // for PoS this is equivalent to verifying height+span relation
			return false;

		Merkle::Hash hv;
		get_Hash(hv);
		return sNext.m_Prev == hv;
	}

	void Block::SystemState::Full::SetFirst(const Sequence::Prefix& prfx, const Element& x)
	{
		Cast::Down<Sequence::Prefix>(*this) = prfx;
		Cast::Down<Sequence::Element>(*this) = x;
	}

	void Block::SystemState::Full::SetNext(const Full& s, const Element& x)
	{
		s.get_Hash(m_Prev);
		m_Number.v = s.m_Number.v + 1;
		m_ChainWork = s.m_ChainWork + x.m_PoW.m_Difficulty;

		Cast::Down<Sequence::Element>(*this) = x;
	}

	void Block::SystemState::Full::get_HashInternal(Merkle::Hash& out, bool bTotal) const
	{
		// Our formula:
		ECC::Hash::Processor hp;
		hp
			<< m_Number.v
			<< m_Prev
			<< m_ChainWork
			<< m_Kernels
			<< m_Definition
			<< m_TimeStamp
			<< m_PoW.m_Difficulty.m_Packed;

		// Starting from Fork2: add Rules cfg. Make it harder to tamper using headers mined on different cfg
		const Rules& r = Rules::get();
		auto iFork = r.FindFork(get_Height());
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
		if (m_Number.v)
			get_HashInternal(hv, true);
		else
			hv = Rules::get().Prehistoric;
	}

	bool Block::SystemState::Full::IsSane() const
	{
		if (!m_Number.v)
			return false;

		if (m_Number.v == 1u)
		{
			if (m_Prev != Rules::get().Prehistoric)
				return false;

			if (m_ChainWork - m_PoW.m_Difficulty != Zero)
				return false;
		}

		return true;
	}

	void Block::SystemState::Full::get_ID(ID& out) const
	{
		out.m_Number = m_Number;
		get_Hash(out.m_Hash);
	}

	void Block::SystemState::Full::get_ID(HeightHash& out) const
	{
		out.m_Height = get_Height();
		get_Hash(out.m_Hash);
	}

	Height Block::SystemState::Full::get_Height() const
	{
		const Rules& r = Rules::get();
		if (r.IsConstantSpan())
			return m_Number.v;

		if (!m_Number.v)
			return 0;

		// extract height from chainwork
		uintBigFor<Height>::Type val;
		m_ChainWork.ShiftRight(Difficulty::s_MantissaBits, val);

		Height ret;
		val.Export(ret);

		return ret;
	}

	bool Block::SystemState::Full::IsValidPoW() const
	{
		const auto& r = Rules::get();
		switch (r.m_Consensus)
		{
		case Rules::Consensus::PoW:
			{
				Merkle::Hash hv;
				get_HashForPoW(hv);
				return m_PoW.IsValid(hv.m_pData, hv.nBytes, get_Height());
			}

		default:
			assert(false);
			// no break

		case Rules::Consensus::FakePoW:
		case Rules::Consensus::Pbft:
			return true;
		}
	}

	bool Block::SystemState::Full::GeneratePoW(const PoW::Cancel& fnCancel)
	{
		Merkle::Hash hv;
		get_HashForPoW(hv);

		return m_PoW.Solve(hv.m_pData, hv.nBytes, get_Height(), fnCancel);
	}

	uint64_t Block::SystemState::Sequence::Element::get_Timestamp_ms() const
	{
		const auto& d = Cast::Reinterpret<Block::Pbft::HdrData>(m_PoW);

		uint16_t frac_ms;
		d.m_Time_ms.Export(frac_ms);

		if (frac_ms >= 1000)
			return 0;

		// max value for which converting to ms won't overflow.
		// Realistic time is much much lower
		const uint64_t nMaxTime_s = std::numeric_limits<uint64_t>::max() / 1000;
		if (m_TimeStamp >= nMaxTime_s)
			return 0;

		return m_TimeStamp * 1000 + frac_ms; // guaranteed to not overflow
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

		if (r.IsPastFork_<3>(m_Height))
		{
			Merkle::Hash hvCSA;
			return Interpret(hv, hv, get_KL(hv), hvCSA, get_CSA(hvCSA));
		}

		bool bUtxo = get_Utxos(hv);
		if (!r.IsPastFork_<2>(m_Height))
			return bUtxo;

		Merkle::Hash hvSA;
		return Interpret(hv, hv, bUtxo, hvSA, get_SA(hvSA));
	}

	bool Block::SystemState::Evaluator::get_SA(Merkle::Hash& hv)
	{
		Merkle::Hash hvAssets;
		return Interpret(hv, hv, get_Shielded(hv), hvAssets, get_Assets(hvAssets));
	}

	bool Block::SystemState::Evaluator::get_KL(Merkle::Hash& hv)
	{
		Merkle::Hash hvL;
		return Interpret(hv, hv, get_Kernels(hv), hvL, get_Logs(hvL));
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

	bool Block::SystemState::Evaluator::get_Logs(Merkle::Hash&)
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

			m_Height = s.get_Height();
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
			m_Height = s.get_Height();
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

	void Block::get_HashContractLog(Merkle::Hash& hv, const Blob& key, const Blob& val, uint32_t nPos)
	{
		ECC::Hash::Processor()
			<< "beam.contract.log"
			<< nPos
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

		if (!Rules::get().IsPastFork_<3>(get_Height()))
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
		return IsValidProofKernel(krn.get_ID(), proof);
	}

	bool Block::SystemState::Full::IsValidProofKernel(const Merkle::Hash& hvID, const TxKernel::LongProof& proof) const
	{
		if (!proof.m_State.IsValid())
			return false;

		if (!proof.m_State.IsValidProofKernel(hvID, proof.m_Inner))
			return false;

		if (proof.m_State == *this)
			return true;

		ID id;
		proof.m_State.get_ID(id);
		return IsValidProofState(id, proof.m_Outer);
	}

	bool Block::SystemState::Full::IsValidProofKernel(const Merkle::Hash& hvID, const Merkle::Proof& proof) const
	{
		Merkle::Hash hv = hvID;
		if (!Rules::get().IsPastFork_<3>(get_Height()))
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

	bool Block::SystemState::Full::IsValidProofLog(const Merkle::Hash& hvLog, const Merkle::Proof& proof) const
	{
		Merkle::Hash hv = hvLog;

		struct MyVerifier
			:public ProofVerifier
		{
			virtual bool get_Logs(Merkle::Hash&) override {
				return true;
			}
		} v;

		return v.Verify(*this, hv, proof);
	}

	bool Block::SystemState::Full::IsValidProofState(const ID& id, const Merkle::HardProof& proof) const
	{
		if (!id.m_Number.v || (id.m_Number.v >= m_Number.v))
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
		return hver.Verify(*this, id.m_Number.v - 1, m_Number.v - 1);
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
			m_Map[s.get_Height()] = s;
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

		Amount val = Rules::get().get_Emission(m_Height);
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

	std::ostream& operator << (std::ostream& s, const Block::SystemState::ID& id)
	{
		s << "Num-" << id.m_Number.v << "-" << id.m_Hash;
		return s;
	}

	int Block::SystemState::ID::cmp(const Block::SystemState::ID& v) const
	{
		CMP_MEMBER(m_Number.v)
		CMP_MEMBER_EX(m_Hash)
		return 0;
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

	void LongAction::Reset(const char* sz, uint64_t nTotal)
	{
		m_Total = nTotal;
		m_Last_ms = GetTime_ms();
		BEAM_LOG_INFO() << sz;
		if (m_pExternal)
			m_pExternal->Reset(sz, nTotal);
	}

	void LongAction::SetTotal(uint64_t nTotal)
	{
		m_Total = nTotal;
		if (m_pExternal)
			m_pExternal->SetTotal(nTotal);
	}

	bool LongAction::OnProgress(uint64_t pos)
	{
		uint32_t dt_ms = GetTime_ms() - m_Last_ms;

		const uint32_t nWindow_ms = 10000; // 10 sec
		uint32_t n = dt_ms / nWindow_ms;
		if (n)
		{
			m_Last_ms += n * nWindow_ms;

			uint32_t nDone = 0;

			if (m_Total)
			{
				if (pos >= m_Total)
					nDone = 100;
				else
					nDone = (uint32_t) (pos * 100ull / m_Total);
			}

			BEAM_LOG_INFO() << "\t" << nDone << "%...";
		}
		if (m_pExternal)
			return m_pExternal->OnProgress(pos);

		return true;
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
		m_Cid = Zero;
		m_LockHeight = 0;
		m_Metadata.Reset();
	}

	void Asset::CreateInfo::SetCid(const ContractID* pCid)
	{
		if (pCid)
			m_Cid = *pCid;
		else
			m_Cid = Zero;
	}

	bool Asset::Info::IsEmpty() const
	{
	    return m_Value == Zero && m_Owner == Zero && m_LockHeight == Zero && m_Metadata.m_Value.empty();
	}

	bool Asset::Info::IsValid() const
    {
	    return m_Owner != Zero && m_LockHeight != Zero;
    }

	bool Asset::CreateInfo::IsDefDeposit() const
	{
		return (Rules::get().CA.DepositForList2 == m_Deposit);
	}

	void Asset::Full::get_Hash(ECC::Hash::Value& hv) const
	{
		ECC::Hash::Processor hp;
		hp
			<< "B.Asset.V1"
			<< m_ID
			<< m_Value
			<< m_Owner
			<< m_LockHeight
			<< m_Metadata.m_Hash;

		if (!IsDefDeposit())
		{
			hp
				<< "deposit"
				<< m_Deposit;
		}

		hp >> hv;
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

	void Asset::Metadata::get_Owner(PeerID& res, const ContractID& cid) const
	{
		ECC::Hash::Processor()
			<< "bvm.a.own"
			<< cid
			<< m_Hash
			>> res;
	}

	bool Asset::CreateInfo::Recognize(Key::IPKdf& pkdf) const
	{
		PeerID pid;
		m_Metadata.get_Owner(pid, pkdf);
		return pid == m_Owner;
	}

	const ECC::Point::Compact& Asset::Base::get_H()
	{
		return ECC::Context::get().m_Ipp.H_.m_Fast.m_pPt[0];
	}

	void Asset::Base::get_GeneratorSafe(ECC::Point::Storage& pt_s) const
	{
		if (m_ID)
			get_Generator(pt_s);
		else
		{
			secp256k1_ge ge;
			get_H().Assign(ge);
			pt_s.FromNnz(ge);
		}
	}

	thread_local Asset::ID Asset::Proof::Params::s_AidMax_Override = 0;

	bool Asset::Proof::Params::IsNeeded(Asset::ID aid, Height hScheme)
	{
		if (aid)
			return true;

		if (!Rules::get().IsEnabledCA(hScheme))
			return false;

		return true;
	}


	struct Asset::Proof::CmList
		:public Sigma::CmList
	{
		Asset::ID m_Aid0;
		bool m_IsPastHF6;

		CmList(const Rules& r, Height hScheme)
		{
			m_IsPastHF6 = r.IsPastFork_<6>(hScheme);
		}

		bool get_At(ECC::Point::Storage& pt_s, uint32_t iIdx) override
		{
			Asset::ID aid = (m_IsPastHF6 && !iIdx) ? 0 : (m_Aid0 + iIdx);
			Base(aid).get_GeneratorSafe(pt_s);
			return true;
		}

		static void MakeRandom(ECC::Hash::Value&, const ECC::Scalar::Native& skGen);
		void SelectWindow(Asset::ID, const Rules&, const ECC::Scalar::Native& skGen);
		uint32_t SelectOffset(uint32_t n, uint32_t nMax, uint32_t N, ECC::Hash::Value&, const ECC::Scalar::Native& skGen, bool b);
	};

	void Asset::Proof::Create(Height hScheme, ECC::Point::Native& genBlinded, ECC::Scalar::Native& skInOut, Amount val, Asset::ID aid, const ECC::Hash::Value* phvSeed)
	{
		ECC::Point::Native gen;
		if (aid)
			Base(aid).get_Generator(gen);

		Create(hScheme, genBlinded, skInOut, val, aid, gen, phvSeed);
	}

	void Asset::Proof::Create(Height hScheme, ECC::Point::Native& genBlinded, ECC::Scalar::Native& skInOut, Amount val, Asset::ID aid, const ECC::Point::Native& gen, const ECC::Hash::Value* phvSeed)
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

		Create(hScheme, genBlinded, skAsset, aid, gen);
	}

	void Asset::Proof::Create(Height hScheme, ECC::Point::Native& genBlinded, const ECC::Scalar::Native& skGen, Asset::ID aid, const ECC::Point::Native& gen)
	{
		if (aid)
			genBlinded = gen;
		else
			Base::get_H().Assign(genBlinded, true); // not always specified explicitly for aid==0

		genBlinded += ECC::Context::get().G * skGen;
		m_hGen = genBlinded;

		const Rules& r = Rules::get();
		CmList lst(r, hScheme);

		lst.SelectWindow(aid, r, skGen);
		m_Begin = lst.m_Aid0;

		Sigma::Prover prover(lst, r.CA.m_ProofCfg, *this);
		prover.m_Witness.m_L = aid ? (aid - lst.m_Aid0) : 0; // should be correct, with both schemes
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

	void Asset::Proof::CmList::MakeRandom(ECC::Hash::Value& hv, const ECC::Scalar::Native& skGen)
	{
		ECC::Hash::Processor() << skGen >> hv; // pseudo-random
	}

	uint32_t Asset::Proof::CmList::SelectOffset(uint32_t n, uint32_t nMax, uint32_t N, ECC::Hash::Value& hv, const ECC::Scalar::Native& skGen, bool b)
	{
		assert(N);
		assert(n);

		if (nMax <  n)
			std::setmax(nMax, n + N / 2); // guess

		if (nMax < N)
			return 0;

		// the whole range [0, nMax] is larger than N, means there's a degree of freedom to choose the window
		if (!b)
			MakeRandom(hv, skGen);

		// select the pos of our element within the window as uniform random
		uint32_t nPos;
		hv.ExportWord<0>(nPos);

		if (m_IsPastHF6)
			nPos = 1u + (nPos % (N - 1)); // our element position within [1, N)
		else
			nPos %= N; // our element position within [0, N)

		if (n <= nPos)
			return 0; // start saturated

		if (n - nPos + (N - 1) >= nMax)
			return nMax - (N - 1); // end saturated

		return n - nPos;
	}

	void Asset::Proof::CmList::SelectWindow(Asset::ID aid, const Rules& r, const ECC::Scalar::Native& skGen)
	{
		// Randomize m_Aid0
		m_Aid0 = 0;

		// 2 nuances to take into consideration:
		// (1) after HF6 the 0th element of the list is always the default asset (aid==0), the window affects only remaining elements
		// (2) For bridged network with foreign assets, there's a gap (usually very large). So basically we have 2 distinct ranges.

		uint32_t N = r.CA.m_ProofCfg.get_N();
		if (N <= 1)
			return; // should not happen

		uint32_t nCountForeign = std::min(N - 1, r.CA.ForeignEnd); // we have no reliable information, this is a guess
		uint32_t nCountLocal = (Params::s_AidMax_Override > r.CA.ForeignEnd) ? (Params::s_AidMax_Override - r.CA.ForeignEnd) : 0;

		ECC::Hash::Value hv;
		bool bHv = false;

		if (!aid)
		{
			if (!m_IsPastHF6)
				return;

			if (!nCountLocal && (nCountForeign < N))
				; // leave it as-is
			else
			{
				if (!nCountForeign && (nCountLocal < N))
					aid = r.CA.ForeignEnd;
				else
				{
					// choose a pseudo-random aid from either foreign or local ranges
					uint32_t nCountTotal = nCountForeign + nCountLocal; // won't overflow
					assert(nCountTotal);

					MakeRandom(hv, skGen);
					bHv = true;

					hv.ExportWord<1>(aid);
					aid %= nCountTotal;

					if (aid >= nCountForeign)
						aid += (r.CA.ForeignEnd - nCountForeign);
				}
			}

			aid++;
		}

		if (aid < r.CA.ForeignEnd)
			m_Aid0 = SelectOffset(aid, nCountForeign, N, hv, skGen, bHv);
		else
		{
			aid -= r.CA.ForeignEnd;
			m_Aid0 = SelectOffset(aid, nCountLocal, N, hv, skGen, bHv);
			m_Aid0 += r.CA.ForeignEnd;
		}
	}

	bool Asset::Proof::IsValidPrepare(ECC::Point::Native& hGen, ECC::InnerProduct::BatchContext& bc, ECC::Scalar::Native* pKs) const
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

	bool Asset::Proof::IsValid(Height hScheme, ECC::Point::Native& hGen) const
	{
		if (BatchContext::s_pInstance)
			return BatchContext::s_pInstance->IsValid(hScheme, hGen, *this);

		ECC::Mode::Scope scope(ECC::Mode::Fast);

		ECC::InnerProduct::BatchContextEx<1> bc;
		std::vector<ECC::Scalar::Native> vKs;

		const Rules& r = Rules::get();
		const Sigma::Cfg& cfg = r.CA.m_ProofCfg;
		uint32_t N = cfg.get_N();
		assert(N);
		vKs.resize(N);

		if (!IsValidPrepare(hGen, bc, &vKs.front()))
			return false;

		CmList lst(r, hScheme);
		lst.m_Aid0 = m_Begin;

		lst.Calculate(bc.m_Sum, 0, N, &vKs.front());

		return bc.Flush();
	}

	void Asset::Proof::Clone(Ptr& p) const
	{
		p = std::make_unique<Proof>();
		*p = *this;
	}

	void Asset::Proof::Expose(ECC::Oracle& oracle, Height hScheme, const Ptr& p)
	{
		if (Rules::get().IsPastFork_<3>(hScheme))
		{
			bool bAsset = !!p;
			oracle << bAsset;
			if (p)
				oracle << p->m_hGen;
		}
	}

	thread_local Asset::Proof::BatchContext* Asset::Proof::BatchContext::s_pInstance = nullptr;

	/////////////////////////////////////////////
	// FundsChangeMap
	void FundsChangeMap::Add(Amount val, Asset::ID aid, bool bSpend)
	{
		if (!val)
			return;

		AmountBig::Number valBig(val);
		if (bSpend)
			valBig = -valBig;

		Add(valBig, aid);
	}

	void FundsChangeMap::Add(const AmountBig::Number& valBig, Asset::ID aid)
	{
		assert(valBig != Zero);

		auto it = m_Map.find(aid);
		if (m_Map.end() == it)
			m_Map[aid] = valBig;
		else
		{
			auto& dst = it->second;
			dst += valBig;

			if (dst == Zero)
				m_Map.erase(it);
		}
	}

	void FundsChangeMap::ToCommitment(ECC::Point::Native& res) const
	{
		if (m_Map.empty())
			res = Zero;
		else
		{
			ECC::MultiMac_Dyn mm;
			mm.Prepare(static_cast<uint32_t>(m_Map.size()), 1);

			for (auto it = m_Map.begin(); m_Map.end() != it; it++)
			{
				ECC::Scalar::Native* pK;
				if (it->first)
				{
					CoinID::Generator gen(it->first);

					pK = mm.m_pKCasual + mm.m_Casual;
					mm.m_pCasual[mm.m_Casual++].Init(gen.m_hGen);
				}
				else
				{
					pK = mm.m_pKPrep + mm.m_Prepared;
					mm.m_ppPrepared[mm.m_Prepared++] = &ECC::Context::get().m_Ipp.H_;
				}

				ECC::Scalar s;

				auto& valBig = it->second;
				auto bNegative = valBig.get_Msb();
				if (bNegative)
				{
					AmountBig::Number dup = -valBig;
					s.m_Value.FromNumber(dup);
				}
				else
					s.m_Value.FromNumber(valBig);

				pK->Import(s);
				if (bNegative)
					*pK = -*pK;
			}

			mm.Calculate(res);
		}
	}

} // namespace beam
