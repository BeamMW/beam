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

	bool ShieldedTxo::Serial::IsValid() const
	{
		ECC::Point::Native comm;
		if (!comm.Import(m_SerialPub))
			return false;

		ECC::Hash::Value hv;
		get_Hash(hv);

		return m_Signature.IsValid(ECC::Context::get().m_Sig.m_CfgGJ1, hv, m_Signature.m_pK, &comm);
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

	void ShieldedTxo::Data::DoubleBlindedCommitment(ECC::Point::Native& res, const ECC::Scalar::Native& kG, const ECC::Scalar::Native& kJ)
	{
		res = ECC::Context::get().G * kG;
		res += ECC::Context::get().J * kJ;
	}

	bool ShieldedTxo::Data::IsEqual(const ECC::Point::Native& pt0, const ECC::Point& pt1)
	{
		// Import/Export seems to be the same complexity
		ECC::Point pt2;
		pt0.Export(pt2);
		return pt2 == pt1;
	}

	bool ShieldedTxo::Data::IsEqual(const ECC::Point::Native& pt0, const ECC::Point::Native& pt1)
	{
		ECC::Point::Native pt = -pt0;
		pt += pt1;
		return pt == Zero;
	}

	void ShieldedTxo::Data::GenerateS1(Key::IPKdf& gen, const ECC::Point& ptShared, ECC::Scalar::Native& nG, ECC::Scalar::Native& nJ)
	{
		gen.DerivePKey(nG, HashTxt("nG") << ptShared);
		gen.DerivePKey(nJ, HashTxt("nJ") << ptShared);
	}

	void ShieldedTxo::Data::ToSk(Key::IPKdf& gen)
	{
		gen.DerivePKey(m_kOutG, HashTxt("sG") << m_kSerG);
	}

	void ShieldedTxo::Data::GetOutputSeed(Key::IPKdf& gen, ECC::Hash::Value& res) const
	{
		ECC::Scalar::Native k;
		gen.DerivePKey(k, HashTxt("seed") << m_kOutG);
		ECC::Hash::Processor() << k >> res;
	}

	void ShieldedTxo::Data::GetDH(ECC::Hash::Value& res, const ECC::Point& pt)
	{
		HashTxt("DH") << pt >> res;
	}

	void ShieldedTxo::Data::GenerateS(Serial& s, const PublicGen& gen, const ECC::Hash::Value& nonce)
	{
		ECC::Scalar::Native pSk[2];

		gen.m_pSer->DerivePKey(m_kSerG, HashTxt("kG") << nonce);
		pSk[0] = m_kSerG;
		GetSerial(pSk[1], *gen.m_pSer);
		ToSk(*gen.m_pGen);

		ECC::Point::Native pt, pt1;
		DoubleBlindedCommitment(pt, pSk[0], pSk[1]);

		s.m_SerialPub = pt;

		// DH
		ECC::Hash::Value hv;
		GetDH(hv, s.m_SerialPub);

		gen.m_pGen->DerivePKeyG(pt, hv);
		gen.m_pGen->DerivePKeyJ(pt1, hv);

		pt = pt * pSk[0];
		pt += pt1 * pSk[1]; // shared point

		ECC::Scalar::Native pN[2];

		GenerateS1(*gen.m_pGen, pt, pN[0], pN[1]);

		// generalized Schnorr's sig
		s.get_Hash(hv);

		s.m_Signature.SetNoncePub(ECC::Context::get().m_Sig.m_CfgGJ1, pN);
		s.m_Signature.SignRaw(ECC::Context::get().m_Sig.m_CfgGJ1, hv, s.m_Signature.m_pK, pSk, pN);
	}

	void ShieldedTxo::Data::Generate(ShieldedTxo& txo, ECC::Oracle& oracle, const PublicGen& gen, const ECC::Hash::Value& nonce)
	{
		GenerateS(txo.m_Serial, gen, nonce);
		GenerateO(txo, oracle, gen);
	}

	void ShieldedTxo::Data::GenerateO(ShieldedTxo& txo, ECC::Oracle& oracle, const PublicGen& gen)
	{
		ECC::Point::Native pt = ECC::Commitment(m_kOutG, m_Value);
		txo.m_Commitment = pt;

		ECC::RangeProof::CreatorParams cp;
		GetOutputSeed(*gen.m_pGen, cp.m_Seed.V);

		ZeroObject(cp.m_Kidv);
		cp.m_Kidv.set_Subkey(0);
		cp.m_Kidv.m_Value = m_Value;

		txo.m_RangeProof.Create(m_kOutG, cp, oracle);
	}

	void ShieldedTxo::Data::GetSerialPreimage(ECC::Hash::Value& res) const
	{
		ECC::NoLeak<ECC::Scalar> sk;
		sk.V = m_kSerG;
		HashTxt("kG-ser") << sk.V.m_Value >> res;
	}

	void ShieldedTxo::Data::GetSerial(ECC::Scalar::Native& kJ, Key::IPKdf& ser) const
	{
		ECC::Point::Native pt;
		GetSpendPKey(pt, ser);
		Lelantus::SpendKey::ToSerial(kJ, pt);
	}

	void ShieldedTxo::Data::GetSpendPKey(ECC::Point::Native& pt, Key::IPKdf& ser) const
	{
		ECC::Hash::Value hv;
		GetSerialPreimage(hv);
		ser.DerivePKeyG(pt, hv);
	}

	void ShieldedTxo::Data::GetSpendKey(ECC::Scalar::Native& sk, Key::IKdf& ser) const
	{
		ECC::Hash::Value hv;
		GetSerialPreimage(hv);
		ser.DeriveKey(sk, hv);
	}

	bool ShieldedTxo::Data::Recover(const ShieldedTxo& txo, ECC::Oracle& oracle, const Viewer& v)
	{
		ECC::Point::Native ptSer;
		if (!ptSer.Import(txo.m_Serial.m_SerialPub))
			return false;

		ECC::Hash::Value hv;
		GetDH(hv, txo.m_Serial.m_SerialPub);

		ECC::Scalar::Native kJ, nG, nJ;
		v.m_pGen->DeriveKey(kJ, hv);

		ECC::Point::Native pt = ptSer * kJ; // shared point
		GenerateS1(*v.m_pGen, pt, nG, nJ);

		DoubleBlindedCommitment(pt, nG, nJ);
		if (!IsEqual(pt, txo.m_Serial.m_Signature.m_NoncePub))
			return false;

		m_kSerG = txo.m_Serial.m_Signature.m_pK[0];
		m_kSerG += nG;

		kJ = txo.m_Serial.m_Signature.m_pK[1];
		kJ += nJ;

		txo.m_Serial.get_Hash(hv);
		txo.m_Serial.m_Signature.get_Challenge(nJ, hv);
		nJ.Inv();
		nJ = -nJ;

		m_kSerG *= nJ;
		kJ *= nJ;

		DoubleBlindedCommitment(pt, m_kSerG, kJ);
		if (!IsEqual(ptSer, pt))
			return false;

		GetSerial(nJ, *v.m_pSer);
		if (!(nJ == kJ))
			return false;

		// looks good!
		ToSk(*v.m_pGen);

		ECC::RangeProof::CreatorParams cp;
		GetOutputSeed(*v.m_pGen, cp.m_Seed.V);

		if (!txo.m_RangeProof.Recover(oracle, cp))
			return false; // oops?

		m_Value = cp.m_Kidv.m_Value;

		pt = ECC::Commitment(m_kOutG, m_Value);
		return IsEqual(pt, txo.m_Commitment);
	}

} // namespace beam
