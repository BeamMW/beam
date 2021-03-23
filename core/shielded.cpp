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

#include "shielded.h"

namespace beam
{
	void ShieldedTxo::Ticket::get_Hash(ECC::Hash::Value& hv) const
	{
		ECC::Hash::Processor()
			<< "Out-S"
			<< m_SerialPub
			>> hv;
	}

	bool ShieldedTxo::Ticket::IsValid(ECC::Point::Native& comm) const
	{
		if (!comm.Import(m_SerialPub))
			return false;

		ECC::Hash::Value hv;
		get_Hash(hv);

		return m_Signature.IsValid(ECC::Context::get().m_Sig.m_CfgGJ1, hv, m_Signature.m_pK, &comm);
	}

	void ShieldedTxo::Prepare(ECC::Oracle& oracle) const
	{
		// Since m_Ticket doesn't contribute to the transaction balance, it MUST be exposed to the Oracle used with m_RangeProof.
		// m_Commitment also should be used (for the same reason it's used in regular Output)
		oracle
			<< m_Ticket.m_SerialPub
			<< m_Ticket.m_Signature.m_NoncePub
			<< m_Commitment;
	}

	bool ShieldedTxo::IsValid(ECC::Oracle& oracle, ECC::Point::Native& comm, ECC::Point::Native& ser) const
	{
		if (!(m_Ticket.IsValid(ser) && comm.Import(m_Commitment)))
			return false;

		ECC::Point::Native hGen;
		if (m_pAsset && !m_pAsset->IsValid(hGen))
			return false;

		Prepare(oracle);
		return m_RangeProof.IsValid(comm, oracle, &hGen);
	}

	void ShieldedTxo::operator = (const ShieldedTxo& v)
	{
		m_Ticket = v.m_Ticket;
		m_Commitment = v.m_Commitment;
		m_RangeProof = v.m_RangeProof;

		if (v.m_pAsset)
			v.m_pAsset->Clone(m_pAsset);
		else
			m_pAsset.reset();
	}

	void ShieldedTxo::UpdateState(ECC::Hash::Value& hv, const ECC::Point::Storage& pt_s)
	{
		ECC::Hash::Processor()
			<< "shielded.el"
			<< hv
			<< pt_s.m_X
			<< pt_s.m_Y
			>> hv;
	}

	void ShieldedTxo::Voucher::get_Hash(ECC::Hash::Value& hv) const
	{
		ECC::Hash::Processor()
			<< "voucher.1"
			<< m_Ticket.m_SerialPub
			<< m_Ticket.m_Signature.m_NoncePub
			<< m_SharedSecret
			>> hv;
	}

	bool ShieldedTxo::Voucher::IsValid(const ECC::Point::Native& pk) const
	{
		ECC::Point::Native pt;
		if (!m_Ticket.IsValid(pt))
			return false;

		ECC::Hash::Value hv;
		get_Hash(hv);
		return m_Signature.IsValid(hv, pk);
	}

	bool ShieldedTxo::Voucher::IsValid(const PeerID& pid) const
	{
		ECC::Point::Native pk;
		return
			pid.ExportNnz(pk) &&
			IsValid(pk);
	}

	bool ShieldedTxo::ID::operator==(const ShieldedTxo::ID& other) const
	{
		return
			m_Value == other.m_Value &&
			m_Key == other.m_Key &&
			m_User == other.m_User &&
			m_AssetID == other.m_AssetID;
	}

	void ShieldedTxo::ID::get_SkOutPreimage(ECC::Hash::Value& hv, Amount fee) const
	{
		// seed should account for meaningful kernel params, i.e. min/max heights, fee, etc.
		ECC::Hash::Processor()
			<< "sh.skout"
			<< m_Value
			<< m_AssetID
			<< fee
			<< m_Key.m_kSerG
			<< m_Key.m_IsCreatedByViewer
			<< m_Key.m_nIdx
			>> hv;
	}

	void ShieldedTxo::ID::get_SkOut(ECC::Scalar::Native& out, Amount fee, Key::IKdf& kdf) const
	{
		ECC::Hash::Value hv;
		get_SkOutPreimage(hv, fee);
		kdf.DeriveKey(out, hv);
	}

	/////////////
	// Shielded keygen
	struct ShieldedTxo::Data::HashTxt
	{
		ECC::Hash::Processor m_Processor;
		ECC::Hash::Value m_hv;

		template <uint32_t n>
		HashTxt(const char(&sz)[n])
		{
			m_Processor
				<< "Output.Shielded."
				<< sz;
		}

		template <typename T>
		HashTxt& operator << (const T& t) { m_Processor << t; return *this; }

		void operator >> (ECC::Hash::Value& hv)
		{
			m_Processor >> m_hv;
			hv = m_hv;
		}

		operator const ECC::Hash::Value& ()
		{
			m_Processor >> m_hv;
			return m_hv;
		}
	};

	/////////////
	// TicketParams
	void ShieldedTxo::Data::TicketParams::DoubleBlindedCommitment(ECC::Point::Native& res, const ECC::Scalar::Native* pK)
	{
		res = ECC::Context::get().G * pK[0];
		res += ECC::Context::get().J * pK[1];
	}

	void ShieldedTxo::Data::TicketParams::Generate(Ticket& s, const PublicGen& gen, const ECC::Hash::Value& nonce)
	{
		GenerateInternal(s, nonce, *gen.m_pGen, nullptr, *gen.m_pSer);
	}

	void ShieldedTxo::Data::TicketParams::Generate(Ticket& s, const Viewer& v, const ECC::Hash::Value& nonce)
	{
		GenerateInternal(s, nonce, *v.m_pGen, v.m_pGen.get(), *v.m_pSer);
	}

	void ShieldedTxo::Data::TicketParams::set_FromkG(Key::IPKdf& gen, Key::IKdf* pGenPriv, Key::IPKdf& ser)
	{
		ECC::NoLeak<ECC::Scalar> sk;
		sk.V = m_pK[0];

		HashTxt htxt("kG-k");
		htxt << sk.V.m_Value;
		const ECC::Hash::Value& hv = htxt;

		ECC::Scalar::Native k;

		m_IsCreatedByViewer = !!pGenPriv;
		if (pGenPriv)
			pGenPriv->DeriveKey(k, hv);
		else
			gen.DerivePKey(k, hv);

		sk.V = k;
		m_SerialPreimage = Cast::Down<const ECC::Hash::Value>(HashTxt("k-pI") << sk.V.m_Value);

		ECC::Point::Native pt;
		ser.DerivePKeyG(pt, m_SerialPreimage);
		m_SpendPk = pt;

		Lelantus::SpendKey::ToSerial(m_pK[1], m_SpendPk);
	}

	void ShieldedTxo::Data::TicketParams::get_DH(ECC::Hash::Value& res, const ECC::Point& ptSerialPub)
	{
		HashTxt("DH") << ptSerialPub >> res;
	}

	void ShieldedTxo::Data::TicketParams::get_Nonces(Key::IPKdf& gen, ECC::Scalar::Native* pN) const
	{
		gen.DerivePKey(pN[0], HashTxt("nG") << m_SharedSecret);
		gen.DerivePKey(pN[1], HashTxt("nJ") << m_SharedSecret);
	}

	void ShieldedTxo::Data::TicketParams::GenerateInternal(Ticket& s, const ECC::Hash::Value& nonce, Key::IPKdf& gen, Key::IKdf* pGenPriv, Key::IPKdf& ser)
	{
		gen.DerivePKey(m_pK[0], HashTxt("kG") << nonce);
		set_FromkG(gen, pGenPriv, ser);

		set_SharedSecretFromKs(s.m_SerialPub, gen);

		ECC::Scalar::Native pN[2];
		get_Nonces(gen, pN);

		// generalized Schnorr's sig
		ECC::Hash::Value hv;
		s.get_Hash(hv);
		s.m_Signature.SetNoncePub(ECC::Context::get().m_Sig.m_CfgGJ1, pN);
		s.m_Signature.SignRaw(ECC::Context::get().m_Sig.m_CfgGJ1, hv, s.m_Signature.m_pK, m_pK, pN);
	}

	void ShieldedTxo::Data::TicketParams::set_SharedSecretFromKs(ECC::Point& ptSerialPub, Key::IPKdf& gen)
	{
		ECC::Point::Native pt, pt1;
		DoubleBlindedCommitment(pt, m_pK);
		ptSerialPub = pt;

		ECC::Hash::Value hv;
		get_DH(hv, ptSerialPub);

		gen.DerivePKeyG(pt, hv);
		gen.DerivePKeyJ(pt1, hv);

		pt = pt * m_pK[0];
		pt += pt1 * m_pK[1]; // shared point
		set_SharedSecret(pt);
	}


	void ShieldedTxo::Data::TicketParams::set_SharedSecret(const ECC::Point::Native& pt)
	{
		HashTxt("sp-sec") << pt >> m_SharedSecret;
	}

	bool ShieldedTxo::Data::TicketParams::Recover(const Ticket& s, const Viewer& v)
	{
		ECC::Mode::Scope scope(ECC::Mode::Fast);

		ECC::Point::Native pt;
		if (!pt.Import(s.m_SerialPub))
			return false;

		ECC::Hash::Value hv;
		get_DH(hv, s.m_SerialPub);

		ECC::Scalar::Native k;
		v.m_pGen->DeriveKey(k, hv);

		pt = pt * k; // shared point
		set_SharedSecret(pt);

		ECC::Scalar::Native pN[2];
		get_Nonces(*v.m_pGen, pN);

		DoubleBlindedCommitment(pt, pN);
		if (!(pt == s.m_Signature.m_NoncePub))
			return false;

		// there's a match with high probability. Reverse-engineer the keys
		s.get_Hash(hv);
		s.m_Signature.get_Challenge(k, hv);
		k.Inv();
		k = -k;

		for (size_t i = 0; i < _countof(m_pK); i++)
		{
			m_pK[i] = s.m_Signature.m_pK[i];
			m_pK[i] += pN[i];
			m_pK[i] *= k;
		}

		k = m_pK[1];

		set_FromkG(*v.m_pGen, v.m_pGen.get(), *v.m_pSer);
		if (k == m_pK[1])
			return true;

		set_FromkG(*v.m_pGen, nullptr, *v.m_pSer);
		if (k == m_pK[1])
			return true;

		return false;
	}

	void ShieldedTxo::Data::TicketParams::Restore(const Viewer& v)
	{
		set_FromkG(*v.m_pGen, m_IsCreatedByViewer ? v.m_pGen.get() : nullptr, *v.m_pSer);

		ECC::Point ptSerialPub;
		set_SharedSecretFromKs(ptSerialPub, *v.m_pGen);

	}

	/////////////
	// OutputParams
	void ShieldedTxo::Data::OutputParams::get_Seed(ECC::uintBig& res, const ECC::Hash::Value& hvShared, const ECC::Oracle& oracle)
	{
		ECC::Oracle(oracle) // copy
			<< "bp-s"
			<< hvShared
			>> res;
	}

	void ShieldedTxo::Data::OutputParams::get_skGen(ECC::Scalar::Native& skGen, const ECC::Hash::Value& hvShared) const
	{
		ECC::NonceGenerator("skG-O")
			<< hvShared
			>> skGen;
	}

	void ShieldedTxo::Data::OutputParams::get_sk(ECC::Scalar::Native& sk, const ECC::Hash::Value& hvShared) const
	{
		ECC::NonceGenerator("kG-O")
			<< hvShared
			>> sk;
	}

#pragma pack (push, 1)
	struct ShieldedTxo::Data::OutputParams::Packed
	{
		uintBigFor<Asset::ID>::Type m_Asset;
		uint8_t m_Flags;
	};
#pragma pack (pop)

	uint8_t ShieldedTxo::Data::OutputParams::set_kG(const ECC::Hash::Value& hvShared, ECC::Scalar::Native& kTmp)
	{
		uint8_t nFlag = Msg2Scalar(kTmp, m_User.m_Sender);

		get_sk(m_k, hvShared);
		m_k += kTmp;

		return nFlag;

	}

	void ShieldedTxo::Data::OutputParams::Restore_kG(const ECC::Hash::Value& hvShared)
	{
		ECC::Scalar::Native kTmp;
		set_kG(hvShared, kTmp);
	}

	void ShieldedTxo::Data::OutputParams::Generate(ShieldedTxo& txo, const ECC::Hash::Value& hvShared, ECC::Oracle& oracle, bool bHideAssetAlways /* = false */)
	{
		ECC::Scalar::Native pExtra[2];

		Packed p;
		p.m_Asset = m_AssetID;
		p.m_Flags = set_kG(hvShared, pExtra[0]);

		static_assert(_countof(m_User.m_pMessage) == _countof(pExtra));

		for (size_t i = 0; i < _countof(m_User.m_pMessage); i++)
			p.m_Flags |= (Msg2Scalar(pExtra[i], m_User.m_pMessage[i]) << (i + 1));

		ECC::RangeProof::CreatorParams cp;
		cp.m_Value = m_Value;

		CoinID::Generator g(m_AssetID);

		ECC::Point::Native pt = ECC::Context::get().G * m_k;
		g.AddValue(pt, m_Value);
		txo.m_Commitment = pt;

		cp.m_pExtra = pExtra;
		cp.m_Blob.p = &p;
		cp.m_Blob.n = sizeof(p);

		ECC::Scalar::Native skSign = m_k;
		if (m_AssetID || bHideAssetAlways)
		{
			ECC::Scalar::Native skGen;
			get_skGen(skGen, hvShared);

			txo.m_pAsset = std::make_unique<Asset::Proof>();
			txo.m_pAsset->Create(g.m_hGen, skGen, m_AssetID, g.m_hGen);

			Asset::Proof::ModifySk(skSign, skGen, m_Value);
		}
		else
			txo.m_pAsset.reset();

		txo.Prepare(oracle);
		get_Seed(cp.m_Seed.V, hvShared, oracle);

		txo.m_RangeProof.CoSign(cp.m_Seed.V, skSign, cp, oracle, ECC::RangeProof::Confidential::Phase::SinglePass, &g.m_hGen);
	}

	bool ShieldedTxo::Data::OutputParams::Recover(const ShieldedTxo& txo, const ECC::Hash::Value& hvShared, ECC::Oracle& oracle)
	{
		txo.Prepare(oracle);

		ECC::RangeProof::CreatorParams cp;
		get_Seed(cp.m_Seed.V, hvShared, oracle);

		ECC::Scalar::Native pExtra[2];

		cp.m_pSeedSk = &cp.m_Seed.V; // same seed
		cp.m_pSk = &m_k;
		cp.m_pExtra = pExtra;

		Packed p;
		cp.m_Blob.p = &p;
		cp.m_Blob.n = sizeof(p);

		if (!txo.m_RangeProof.Recover(oracle, cp))
			return false;

		m_Value = cp.m_Value;
		p.m_Asset.Export(m_AssetID);

		CoinID::Generator g(m_AssetID);

		if (txo.m_pAsset)
		{
			ECC::Scalar::Native skGen;
			get_skGen(skGen, hvShared);

			skGen = -skGen;
			Asset::Proof::ModifySk(m_k, skGen, m_Value);
		}

		ECC::Point::Native pt = ECC::Context::get().G * m_k;
		ECC::Tag::AddValue(pt, &g.m_hGen, m_Value);
		if (!(pt == txo.m_Commitment))
			return false;

		for (size_t i = 0; i < _countof(m_User.m_pMessage); i++)
			Scalar2Msg(m_User.m_pMessage[i], pExtra[i], (2 << i) & p.m_Flags);

		get_sk(pExtra[0], hvShared);
		pExtra[0] = m_k - pExtra[0];

		Scalar2Msg(m_User.m_Sender, pExtra[0], 1 & p.m_Flags);

		return true;
	}

	uint8_t ShieldedTxo::Data::OutputParams::Msg2Scalar(ECC::Scalar::Native& s, const ECC::uintBig& x)
	{
		static_assert(sizeof(x) == sizeof(ECC::Scalar));
		s = reinterpret_cast<const ECC::Scalar&>(x);
		return (x >= ECC::Scalar::s_Order);
	}

	void ShieldedTxo::Data::OutputParams::Scalar2Msg(ECC::uintBig& x, const ECC::Scalar::Native& s, uint32_t nOverflow)
	{
		static_assert(sizeof(x) == sizeof(ECC::Scalar));
		reinterpret_cast<ECC::Scalar&>(x) = s;

		if (nOverflow)
			x += ECC::Scalar::s_Order;
	}

	/////////////
	// Params (both)
	void ShieldedTxo::Data::Params::GenerateOutp(ShieldedTxo& txo, ECC::Oracle& oracle, bool bHideAssetAlways /* = false */)
	{
		m_Output.Generate(txo, m_Ticket.m_SharedSecret, oracle, bHideAssetAlways);
	}
	bool ShieldedTxo::Data::Params::Recover(const ShieldedTxo& txo, ECC::Oracle& oracle, const Viewer& v)
	{
		return
			m_Ticket.Recover(txo.m_Ticket, v) &&
			m_Output.Recover(txo, m_Ticket.m_SharedSecret, oracle);
	}

	void ShieldedTxo::Data::Params::ToID(ID& cid) const
	{
		cid.m_Value = m_Output.m_Value;
		cid.m_AssetID = m_Output.m_AssetID;
		cid.m_User = m_Output.m_User;
		cid.m_Key.m_IsCreatedByViewer = m_Ticket.m_IsCreatedByViewer;
		cid.m_Key.m_kSerG = m_Ticket.m_pK[0];
	}

	void ShieldedTxo::Data::Params::Set(Key::IPKdf& ownerKey, const ShieldedTxo::ID& id)
	{
		ShieldedTxo::Viewer viewer;
		viewer.FromOwner(ownerKey, id.m_Key.m_nIdx);

		m_Ticket.m_IsCreatedByViewer = id.m_Key.m_IsCreatedByViewer;
		m_Ticket.m_pK[0] = id.m_Key.m_kSerG;
		m_Ticket.Restore(viewer);

		m_Output.m_Value = id.m_Value;
		m_Output.m_AssetID = id.m_AssetID;
		m_Output.m_User = id.m_User;
		m_Output.Restore_kG(m_Ticket.m_SharedSecret);
	}

	ShieldedTxo::Data::Params::Plus::Plus(const Params& pars)
	{
		m_skFull = pars.m_Output.m_k + pars.m_Ticket.m_pK[0]; // full blinding factor

		if (pars.m_Output.m_AssetID)
			Asset::Base(pars.m_Output.m_AssetID).get_Generator(m_hGen);
	}

	/////////////
	// Generators
	void ShieldedTxo::Viewer::FromOwner(Key::IPKdf& key, Key::Index nIdx/* = 0 */)
	{
		ECC::Scalar::Native sk;
		key.DerivePKey(sk, Data::HashTxt("Own.Gen") << nIdx);
		ECC::NoLeak<ECC::Scalar> s;
		s.V = sk;

		ECC::HKdf::Create(m_pGen, s.V.m_Value);

		GenerateSerSrc(s.V.m_Value, key, nIdx);

		m_pSer.reset(new ECC::HKdfPub);
		Cast::Up<ECC::HKdfPub>(*m_pSer).GenerateChildParallel(key, s.V.m_Value);
	}

	void ShieldedTxo::Viewer::GenerateSerSrc(ECC::Hash::Value& res, Key::IPKdf& key, Key::Index nIdx)
	{
		ECC::Scalar::Native sk;
		key.DerivePKey(sk, Data::HashTxt("Own.Ser") << nIdx);

		static_assert(sizeof(res) == sizeof(ECC::Scalar));
		((ECC::Scalar&) res) = sk;
	}

	void ShieldedTxo::Viewer::GenerateSerPrivate(Key::IKdf::Ptr& pOut, Key::IKdf& key, Key::Index nIdx /* = 0 */)
	{
		ECC::NoLeak<ECC::Hash::Value> hv;
		GenerateSerSrc(hv.V, key, nIdx);

		pOut.reset(new ECC::HKdf);
		Cast::Up<ECC::HKdf>(*pOut).GenerateChildParallel(key, hv.V);
	}

	void ShieldedTxo::PublicGen::FromViewer(const Viewer& v)
	{
		m_pSer = v.m_pSer;
		m_pGen = v.m_pGen;
	}

	uint32_t ShieldedTxo::PublicGen::ExportP(void* p) const
	{
		if (!m_pSer || !m_pGen)
			return 0;

		uint32_t offset = m_pGen->ExportP(p);
		if (p)
			p = static_cast<uint8_t*>(p) + offset;

		return offset + m_pSer->ExportP(p);
	}

} // namespace beam
