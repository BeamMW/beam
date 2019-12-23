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

#pragma once
#include "block_crypt.h"

namespace beam
{

	struct ShieldedTxo::PublicGen
	{
		Key::IPKdf::Ptr m_pGen;
		Key::IPKdf::Ptr m_pSer;
	};

	struct ShieldedTxo::Viewer
	{
		Key::IKdf::Ptr m_pGen;
		Key::IPKdf::Ptr m_pSer;

		void FromOwner(Key::IPKdf&);
		static void GenerateSerPrivate(Key::IKdf::Ptr&, Key::IKdf&);

	private:
		static void GenerateSerSrc(ECC::Hash::Value&, Key::IPKdf&);
	};

	struct ShieldedTxo::Data
	{
		ECC::Scalar::Native m_kSerG; // blinding factor for the serial
		ECC::Scalar::Native m_kOutG; // blinding factor for the Output
		Amount m_Value;
		Height m_hScheme = 0; // must set

		// Generates Shielded from nonce
		// Sets both m_kOutG and m_kSerG
		void GenerateS(Serial&, const PublicGen&, const ECC::Hash::Value& nonce);
		void GenerateO(ShieldedTxo&, ECC::Oracle&, const PublicGen&); // generate UTXO from m_kOutG
		void Generate(ShieldedTxo&, ECC::Oracle&, const PublicGen&, const ECC::Hash::Value& nonce); // generate everything nonce

		bool Recover(const ShieldedTxo&, ECC::Oracle&, const Viewer&);

		struct HashTxt;

		void GetSpendKey(ECC::Scalar::Native&, Key::IKdf& ser) const;
		void GetSpendPKey(ECC::Point::Native&, Key::IPKdf& ser) const;

		void GetOutputSeed(Key::IPKdf& gen, ECC::Hash::Value&) const;

	private:
		static void GenerateS1(Key::IPKdf& gen, const ECC::Point& ptShared, ECC::Scalar::Native& nG, ECC::Scalar::Native& nJ);
		void GetSerialPreimage(ECC::Hash::Value& res) const;
		void GetSerial(ECC::Scalar::Native& kJ, Key::IPKdf& ser) const;
		void ToSk(Key::IPKdf& gen);
		static void GetDH(ECC::Hash::Value&, const ECC::Point&);
		static void DoubleBlindedCommitment(ECC::Point::Native&, const ECC::Scalar::Native& kG, const ECC::Scalar::Native& kJ);
		static bool IsEqual(const ECC::Point::Native& pt0, const ECC::Point& pt1);
		static bool IsEqual(const ECC::Point::Native& pt0, const ECC::Point::Native& pt1);
	};

} // namespace beam
