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
		uint32_t ExportP(void* p) const;
	};

	struct ShieldedTxo::Viewer
	{
		Key::IKdf::Ptr m_pGen;
		Key::IPKdf::Ptr m_pSer;

		void FromOwner(Key::IPKdf&, Key::Index);
		static void GenerateSerPrivate(Key::IKdf::Ptr&, Key::IKdf&, Key::Index);

	private:
		static void GenerateSerSrc(ECC::Hash::Value&, Key::IPKdf&, Key::Index);
	};

	struct ShieldedTxo::Data
	{
		struct TicketParams
		{
			ECC::Scalar::Native m_pK[2]; // kG, kJ

			ECC::Hash::Value m_SharedSecret;
			ECC::Hash::Value m_SerialPreimage;
			ECC::Point m_SpendPk;

			bool m_IsCreatedByViewer;

			void Generate(Ticket&, const PublicGen&, const ECC::Hash::Value& nonce);
			void Generate(Ticket&, const Viewer&, const ECC::Hash::Value& nonce);

			bool Recover(const Ticket&, const Viewer&);

			void Restore(const Viewer&); // must set kG and m_IsCreatedByViewer before calling

		protected:
			void GenerateInternal(Ticket&, const ECC::Hash::Value& nonce, Key::IPKdf& gen, Key::IKdf* pGenPriv, Key::IPKdf& ser);
			void set_FromkG(Key::IPKdf& gen, Key::IKdf* pGenPriv, Key::IPKdf& ser);
			void set_SharedSecretFromKs(ECC::Point& ptSerialPub, Key::IPKdf& gen);
			void set_SharedSecret(const ECC::Point::Native&);
			static void DoubleBlindedCommitment(ECC::Point::Native&, const ECC::Scalar::Native*);
			static void get_DH(ECC::Hash::Value&, const ECC::Point& ptSerialPub);
			void get_Nonces(Key::IPKdf& gen, ECC::Scalar::Native*) const;
		};

		struct OutputParams
		{
			Amount m_Value;
			Asset::ID m_AssetID = 0;
			ECC::Scalar::Native m_k;
			User m_User;

			void Generate(ShieldedTxo&, const ECC::Hash::Value& hvShared, ECC::Oracle&, bool bHideAssetAlways = false);
			bool Recover(const ShieldedTxo&, const ECC::Hash::Value& hvShared, ECC::Oracle&);
			void Restore_kG(const ECC::Hash::Value& hvShared); // restores m_k, all other members must be set

		protected:
			static void get_Seed(ECC::uintBig&, const ECC::Hash::Value& hvShared, const ECC::Oracle&);
			static uint8_t Msg2Scalar(ECC::Scalar::Native&, const ECC::uintBig&);
			static void Scalar2Msg(ECC::uintBig&, const ECC::Scalar::Native&, uint32_t);
			void get_sk(ECC::Scalar::Native&, const ECC::Hash::Value& hvShared) const;
			void get_skGen(ECC::Scalar::Native&, const ECC::Hash::Value& hvShared) const;
			uint8_t set_kG(const ECC::Hash::Value& hvShared, ECC::Scalar::Native& kTmp); // returns overflow flag

			struct Packed;
		};

		struct Params
		{
			TicketParams m_Ticket;
			OutputParams m_Output;

			void GenerateOutp(ShieldedTxo&, ECC::Oracle&, bool bHideAssetAlways = false);
			bool Recover(const ShieldedTxo&, ECC::Oracle&, const Viewer&);

			void ToID(ID&) const;

			void Set(Key::IPKdf& ownerKey, const ShieldedTxo::ID& id);

			struct Plus
			{
				ECC::Point::Native m_hGen;
				ECC::Scalar::Native m_skFull;

				Plus(const Params&);
			};
		};

		struct HashTxt;
	};

	struct ShieldedTxo::DataParams :public ShieldedTxo::Data::Params {};

} // namespace beam
