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
	void ShieldedTxo::Serial::get_Hash(ECC::Hash::Value& hv) const
	{
		ECC::Hash::Processor()
			<< "Out-S"
			<< m_SerialPub
			>> hv;
	}

	bool ShieldedTxo::Serial::IsValid(ECC::Point::Native& comm) const
	{
		if (!comm.Import(m_SerialPub))
			return false;

		ECC::Hash::Value hv;
		get_Hash(hv);

		return m_Signature.IsValid(ECC::Context::get().m_Sig.m_CfgGJ1, hv, m_Signature.m_pK, &comm);
	}

	void ShieldedTxo::Prepare(ECC::Oracle& oracle) const
	{
		// Since m_Serial doesn't contribute to the transaction balance, it MUST be exposed to the Oracle used with m_RangeProof.
		// m_Commitment also should be used (for the same reason it's used in regular Output)
		oracle
			<< m_Serial.m_SerialPub
			<< m_Serial.m_Signature.m_NoncePub
			<< m_Commitment;
	}

	bool ShieldedTxo::IsValid(ECC::Oracle& oracle, ECC::Point::Native& comm, ECC::Point::Native& ser) const
	{
		if (!(m_Serial.IsValid(ser) && comm.Import(m_Commitment)))
			return false;

		Prepare(oracle);
		return m_RangeProof.IsValid(comm, oracle);
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
	// SerialParams
	void ShieldedTxo::Data::SerialParams::DoubleBlindedCommitment(ECC::Point::Native& res, const ECC::Scalar::Native* pK)
	{
		res = ECC::Context::get().G * pK[0];
		res += ECC::Context::get().J * pK[1];
	}

	void ShieldedTxo::Data::SerialParams::Generate(Serial& s, const PublicGen& gen, const ECC::Hash::Value& nonce)
	{
		GenerateInternal(s, nonce, *gen.m_pGen, nullptr, *gen.m_pSer);
	}

	void ShieldedTxo::Data::SerialParams::Generate(Serial& s, const Viewer& v, const ECC::Hash::Value& nonce)
	{
		GenerateInternal(s, nonce, *v.m_pGen, v.m_pGen.get(), *v.m_pSer);
	}

	void ShieldedTxo::Data::SerialParams::set_PreimageFromkG(Key::IPKdf& gen, Key::IKdf* pGenPriv, Key::IPKdf& ser)
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
	}

	void ShieldedTxo::Data::SerialParams::set_FromkG(Key::IPKdf& gen, Key::IKdf* pGenPriv, Key::IPKdf& ser)
	{
		set_PreimageFromkG(gen, pGenPriv, ser);

		ECC::Point::Native pt;
		ser.DerivePKeyG(pt, m_SerialPreimage);
		m_SpendPk = pt;

		Lelantus::SpendKey::ToSerial(m_pK[1], m_SpendPk);
	}

	void ShieldedTxo::Data::SerialParams::get_DH(ECC::Hash::Value& res, const Serial& s)
	{
		HashTxt("DH") << s.m_SerialPub >> res;
	}

	void ShieldedTxo::Data::SerialParams::get_Nonces(Key::IPKdf& gen, const ECC::Point::Native& pt, ECC::Scalar::Native* pN)
	{
		ECC::Point ptShared = pt;
		gen.DerivePKey(pN[0], HashTxt("nG") << ptShared);
		gen.DerivePKey(pN[1], HashTxt("nJ") << ptShared);
	}

	void ShieldedTxo::Data::SerialParams::GenerateInternal(Serial& s, const ECC::Hash::Value& nonce, Key::IPKdf& gen, Key::IKdf* pGenPriv, Key::IPKdf& ser)
	{
		gen.DerivePKey(m_pK[0], HashTxt("kG") << nonce);
		set_FromkG(gen, pGenPriv, ser);
		Export(s, gen, ser);
	}

	void ShieldedTxo::Data::SerialParams::Export(Serial& s, Key::IPKdf& gen, Key::IPKdf& ser) const
	{
		ECC::Point::Native pt, pt1;
		DoubleBlindedCommitment(pt, m_pK);
		s.m_SerialPub = pt;

		ECC::Hash::Value hv;
		get_DH(hv, s);

		gen.DerivePKeyG(pt, hv);
		gen.DerivePKeyJ(pt1, hv);

		pt = pt * m_pK[0];
		pt += pt1 * m_pK[1]; // shared point

		ECC::Scalar::Native pN[2];
		get_Nonces(gen, pt, pN);

		// generalized Schnorr's sig
		s.get_Hash(hv);
		s.m_Signature.SetNoncePub(ECC::Context::get().m_Sig.m_CfgGJ1, pN);
		s.m_Signature.SignRaw(ECC::Context::get().m_Sig.m_CfgGJ1, hv, s.m_Signature.m_pK, m_pK, pN);
	}

	bool ShieldedTxo::Data::SerialParams::Recover(const Serial& s, const Viewer& v)
	{
		ECC::Mode::Scope scope(ECC::Mode::Fast);

		ECC::Point::Native pt;
		if (!pt.Import(s.m_SerialPub))
			return false;

		ECC::Hash::Value hv;
		get_DH(hv, s);

		ECC::Scalar::Native k;
		v.m_pGen->DeriveKey(k, hv);

		pt = pt * k; // shared point

		ECC::Scalar::Native pN[2];
		get_Nonces(*v.m_pGen, pt, pN);

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

	/////////////
	// OutputParams
	void ShieldedTxo::Data::OutputParams::get_DH(ECC::Hash::Value& res, const ShieldedTxo& txo)
	{
		HashTxt("DH-o") << txo.m_Commitment >> res;
	}

	void ShieldedTxo::Data::OutputParams::get_Seed(ECC::uintBig& res, const ECC::Point::Native& pt)
	{
		HashTxt("bp-s") << pt >> res;
	}

	void ShieldedTxo::Data::OutputParams::Generate(ShieldedTxo& txo, ECC::Oracle& oracle, const PublicGen& gen, const ECC::Hash::Value& nonce)
	{
		GenerateInternal(txo, oracle, *gen.m_pGen, nullptr, &gen.m_ptImgH, nonce);
	}

	void ShieldedTxo::Data::OutputParams::Generate(ShieldedTxo& txo, ECC::Oracle& oracle, const Viewer& v, const ECC::Hash::Value& nonce)
	{
		GenerateInternal(txo, oracle, *v.m_pGen, v.m_pGen.get(), nullptr, nonce);
	}

	void ShieldedTxo::Data::OutputParams::GenerateInternal(ShieldedTxo& txo, ECC::Oracle& oracle, Key::IPKdf& gen, Key::IKdf* pGenPriv, const ECC::Point::Native* pImgH, const ECC::Hash::Value& nonce)
	{
		gen.DerivePKey(m_k, HashTxt("kG-O") << nonce);

		ECC::RangeProof::CreatorParams cp;
		ZeroObject(cp.m_Kidv);
		cp.m_Kidv.m_Value = m_Value;

		ECC::Scalar k;
		k.m_Value = m_Sender;

		ECC::Scalar::Native pExtra[2] = { k, Zero };
		cp.m_pExtra = pExtra;
		k = pExtra[0];

		uint32_t nOverflow = (k.m_Value != m_Sender); // overflow (highly unlikely, but possible).
		cp.m_Kidv.set_Subkey(nOverflow);

		ECC::Point::Native pt = ECC::Commitment(m_k, m_Value);
		txo.m_Commitment = pt;

		ECC::Hash::Value hv;
		get_DH(hv, txo);

		if (pGenPriv)
		{
			pGenPriv->DeriveKey(pExtra[1], hv);
			pt = pt * pExtra[1]; // shared point
		}
		else
		{
			gen.DerivePKeyG(pt, hv);

			gen.DerivePKey(pExtra[1], hv);
			pExtra[1] *= m_Value;

			pt = pt * m_k;
			pt += (*pImgH) * pExtra[1]; // shared point
		}

		pExtra[1] = Zero;

		get_Seed(cp.m_Seed.V, pt);

		txo.Prepare(oracle);
		txo.m_RangeProof.CoSign(cp.m_Seed.V, m_k, cp, oracle, ECC::RangeProof::Confidential::Phase::SinglePass);
	}

	bool ShieldedTxo::Data::OutputParams::Recover(const ShieldedTxo& txo, ECC::Oracle& oracle, const Viewer& v)
	{
		ECC::Point::Native pt;
		if (!pt.Import(txo.m_Commitment))
			return false;

		ECC::Hash::Value hv;
		get_DH(hv, txo);
		v.m_pGen->DeriveKey(m_k, hv);

		pt = pt * m_k; // shared point

		ECC::RangeProof::CreatorParams cp;
		get_Seed(cp.m_Seed.V, pt);

		ECC::Scalar::Native pExtra[2];

		cp.m_pSeedSk = &cp.m_Seed.V; // same seed
		cp.m_pSk = &m_k;
		cp.m_pExtra = pExtra;

		txo.Prepare(oracle);
		if (!txo.m_RangeProof.Recover(oracle, cp))
			return false;

		m_Value = cp.m_Kidv.m_Value;

		pt = ECC::Commitment(m_k, m_Value);
		if (!(pt == txo.m_Commitment))
			return false;

		static_assert(sizeof(m_Sender) == sizeof(ECC::Scalar));
		reinterpret_cast<ECC::Scalar&>(m_Sender) = pExtra[0];

		if (cp.m_Kidv.get_Subkey())
			m_Sender += ECC::Scalar::s_Order;

		return true;
	}

	/////////////
	// Generators
	void ShieldedTxo::Viewer::FromOwner(Key::IPKdf& key)
	{
		ECC::Scalar::Native sk;
		key.DerivePKey(sk, Data::HashTxt("Own.Gen"));
		ECC::NoLeak<ECC::Scalar> s;
		s.V = sk;

		ECC::HKdf::Create(m_pGen, s.V.m_Value);

		GenerateSerSrc(s.V.m_Value, key);

		m_pSer.reset(new ECC::HKdfPub);
		Cast::Up<ECC::HKdfPub>(*m_pSer).GenerateChildParallel(key, s.V.m_Value);
	}

	void ShieldedTxo::Viewer::GenerateSerSrc(ECC::Hash::Value& res, Key::IPKdf& key)
	{
		ECC::Scalar::Native sk;
		key.DerivePKey(sk, Data::HashTxt("Own.Ser"));

		static_assert(sizeof(res) == sizeof(ECC::Scalar));
		((ECC::Scalar&) res) = sk;
	}

	void ShieldedTxo::Viewer::GenerateSerPrivate(Key::IKdf::Ptr& pOut, Key::IKdf& key)
	{
		ECC::NoLeak<ECC::Hash::Value> hv;
		GenerateSerSrc(hv.V, key);

		pOut.reset(new ECC::HKdf);
		Cast::Up<ECC::HKdf>(*pOut).GenerateChildParallel(key, hv.V);
	}

	void ShieldedTxo::PublicGen::FromViewer(const Viewer& v)
	{
		m_pSer = v.m_pSer;
		m_pGen = v.m_pGen;

		// extract co-factor
		ECC::Scalar::Native k0, k1;
		v.m_pGen->DerivePKey(k0, 0U);
		v.m_pGen->DeriveKey(k1, 0U);

		k0.Inv();
		k1 *= k0; // co-factor

		m_ptImgH = ECC::Context::get().H_Big * k1;
	}

} // namespace beam
