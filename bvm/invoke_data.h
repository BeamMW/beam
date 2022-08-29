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

#include <core/block_crypt.h>
#include <map>

namespace beam::bvm2 {

	struct FundsMap: public std::map<Asset::ID, AmountSigned>
	{
		void AddSpend(Asset::ID aid, AmountSigned val);
		void operator += (const FundsMap&);
	};

	struct ContractInvokeEntry
	{
		uint32_t m_Flags = 0;
		ECC::uintBig m_Cid;
		uint32_t m_iMethod = 0;
		ByteBuffer m_Data;
		ByteBuffer m_Args;
		std::vector<ECC::Hash::Value> m_vSig;
		uint32_t m_Charge = 0;
		HeightHash m_ParentCtx;

		struct Advanced
		{
			HeightRange m_Height;
			Amount m_Fee;
			ECC::Signature m_Sig;
			ECC::Hash::Value m_hvSk;
			ECC::Point m_Commitment;
			std::vector<ECC::Point> m_vCosigners;

		} m_Adv;

		struct Flags {
			static const uint8_t Adv = 1;
			static const uint8_t Dependent = 2;
			static const uint8_t HasPeers = 4;
			static const uint8_t RoleCosigner = 8;
			static const uint8_t HasCommitment = 0x10;
		};

		bool IsAdvanced() const {
			return !!(Flags::Adv & m_Flags);
		}

		bool IsCoSigner() const {
			return !!(Flags::RoleCosigner & m_Flags);
		}

		FundsMap m_Spend; // ins - outs, not including fee
		std::string m_sComment;

		template <typename Archive>
		void serialize(Archive& ar)
		{
			static const uint32_t nHasFlags = 0x80000000;

			uint32_t nVal = m_Flags ? (nHasFlags | m_Flags) : m_iMethod;
			ar & nVal;

			if (nHasFlags & nVal)
			{
				m_Flags = nVal & ~nHasFlags;

				m_iMethod &= ~nHasFlags; // for more safety, malicious app shader can specify this method number
				ar & m_iMethod;
			}
			else
				m_iMethod = nVal;

			ar
				& m_Args
				& m_vSig
				& m_Charge
				& m_sComment
				& Cast::Down< std::map<Asset::ID, AmountSigned> >(m_Spend);

			if (m_iMethod)
				ar & m_Cid;
			else
			{
				m_Cid = Zero;
				ar & m_Data;
			}

			if (IsAdvanced())
			{
				ar
					& m_Adv.m_Height
					& m_Adv.m_Fee
					& m_Adv.m_Sig
					& m_Adv.m_hvSk;
			}

			if (Flags::Dependent & m_Flags)
				ar & m_ParentCtx;

			if (Flags::HasPeers & m_Flags)
				ar & m_Adv.m_vCosigners;

			if (Flags::HasCommitment & m_Flags)
				ar & m_Adv.m_Commitment;
		}

		void Generate(Transaction&, Key::IKdf&, const HeightRange& hr, Amount fee) const;

		void GenerateAdv(Key::IKdf*, ECC::Scalar* pE, const ECC::Point& ptFullBlind, const ECC::Point& ptFullNonce, const ECC::Hash::Value* phvNonce, const ECC::Scalar* pForeignSig,
			const ECC::Point* pPks, uint32_t nPks, uint8_t nFlags, const ECC::Point* pForeign, uint32_t nForeign);


		[[nodiscard]] Amount get_FeeMin(Height) const;

	private:

		void CreateKrnUnsigned(std::unique_ptr<TxKernelContractControl>&, ECC::Point::Native& ptFunds, const HeightRange& hr, Amount fee) const;

		void get_SigPreimage(ECC::Hash::Value&, const ECC::Hash::Value& krnMsg) const;
	};

	struct ContractInvokeData
	{
		std::vector<ContractInvokeEntry> m_vec;
		// for multisig
		std::vector<ECC::Point> m_vPeers;
		ECC::Hash::Value m_hvKey;
		bool m_IsSender = true;

		std::string get_FullComment() const;
		beam::Amount get_FullFee(Height) const;
		bvm2::FundsMap get_FullSpend() const;

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar & m_vec;

		}
	};
}
