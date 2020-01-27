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

		void FromViewer(const Viewer&);
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
		struct SerialParams
		{
			ECC::Scalar::Native m_pK[2]; // kG, kJ

			ECC::Hash::Value m_SharedSecret;
			ECC::Hash::Value m_SerialPreimage;
			ECC::Point m_SpendPk;

			bool m_IsCreatedByViewer;

			void Generate(Serial&, const PublicGen&, const ECC::Hash::Value& nonce);
			void Generate(Serial&, const Viewer&, const ECC::Hash::Value& nonce);

			bool Recover(const Serial&, const Viewer&);

			void Restore(const Viewer&); // must set kG and m_IsCreatedByViewer before calling

		protected:
			void GenerateInternal(Serial&, const ECC::Hash::Value& nonce, Key::IPKdf& gen, Key::IKdf* pGenPriv, Key::IPKdf& ser);
			void set_PreimageFromkG(Key::IPKdf& gen, Key::IKdf* pGenPriv, Key::IPKdf& ser);
			void set_FromkG(Key::IPKdf& gen, Key::IKdf* pGenPriv, Key::IPKdf& ser);
			void set_SharedSecret(const ECC::Point::Native&);
			static void DoubleBlindedCommitment(ECC::Point::Native&, const ECC::Scalar::Native*);
			static void get_DH(ECC::Hash::Value&, const Serial&);
			void get_Nonces(Key::IPKdf& gen, ECC::Scalar::Native*) const;
		};

		struct OutputParams
		{
			Amount m_Value;
			ECC::Scalar::Native m_k;
			PeerID m_Sender;
			ECC::uintBig m_Message;

			void Generate(ShieldedTxo&, const ECC::Hash::Value& hvShared, ECC::Oracle&, const PublicGen&);
			void Generate(ShieldedTxo&, const ECC::Hash::Value& hvShared, ECC::Oracle&, const Viewer&);
			bool Recover(const ShieldedTxo&, const ECC::Hash::Value& hvShared, ECC::Oracle&, const Viewer&);

		protected:
			void GenerateInternal(ShieldedTxo&, const ECC::Hash::Value& hvShared, ECC::Oracle&, Key::IPKdf& gen);
			static void get_Seed(ECC::uintBig&, const ECC::Hash::Value& hvShared);
			static uint32_t Msg2Scalar(ECC::Scalar::Native&, const ECC::uintBig&);
			static void Scalar2Msg(ECC::uintBig&, const ECC::Scalar::Native&, uint32_t);
		};

		struct HashTxt;
	};

} // namespace beam
