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
#include "radixtree.h"

namespace beam
{
	struct KeyString
	{
		std::string m_sRes;
		std::string m_sMeta;
		ECC::NoLeak<Merkle::Hash> m_hvSecret;

		void Export(const ECC::HKdf&);
		void Export(const ECC::HKdfPub&);
		bool Import(ECC::HKdf&);
		bool Import(ECC::HKdfPub&);
		void SetPassword(const std::string&);
		void SetPassword(const Blob&);

	private:
		typedef uintBig_t<8> MacValue;
		void XCrypt(MacValue&, uint32_t nSize, bool bEnc) const;

		void Export(void*, uint32_t, uint8_t nCode);
		bool Import(void*, uint32_t, uint8_t nCode);
	};

	// Full recovery info. Includes ChainWorkProof, and all the UTXO set which hash should correspond to the tip commitment
	struct RecoveryInfo
	{
		struct Entry
		{
			Height m_CreateHeight;
			Output m_Output; // recovery-only piece

			template <typename Archive>
			void serialize(Archive& ar)
			{
				ar
					& m_CreateHeight
					& m_Output;
			}
		};

		struct Writer
		{
			std::FStream m_Stream;

			void Open(const char*, const Block::ChainWorkProof&);
			void Write(const Entry&);
		};

		struct Reader
		{
			std::FStream m_Stream;
			Block::ChainWorkProof m_Cwp;
			Block::SystemState::Full m_Tip;

			void Open(const char*);
			bool Read(Entry&);
			void Finalyze();

			UtxoTree::Compact m_UtxoTree;

			static void ThrowRulesMismatch();
		};
	};

}
