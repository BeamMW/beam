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

		void ExportS(const Key::IKdf&);
		void ExportP(const Key::IPKdf&);
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
		struct Writer
		{
			std::FStream m_Stream;

			void Open(const char*, const Block::ChainWorkProof&);
		};

		struct IParser
		{
			// each of the following returns false to abort
			virtual bool OnProgress(uint64_t nPos, uint64_t nTotal) { return true; }
			virtual bool OnStates(std::vector<Block::SystemState::Full>&) { return true; }
			virtual bool OnUtxo(Height, const Output&) { return true; }
			virtual bool OnShieldedOut(const ShieldedTxo::DescriptionOutp& , const ShieldedTxo&, const ECC::Hash::Value& hvMsg) { return true; }
			virtual bool OnShieldedIn(const ShieldedTxo::DescriptionInp&) { return true; }
			virtual bool OnAsset(Asset::Full&) { return true; }

			bool Proceed(const char*);

			struct Context;
		};

		struct IRecognizer
			:public IParser
		{
			Key::IPKdf::Ptr m_pOwner;
			const ShieldedTxo::Viewer* m_pViewer = nullptr;

			virtual bool OnUtxo(Height, const Output&) override;
			virtual bool OnShieldedOut(const ShieldedTxo::DescriptionOutp&, const ShieldedTxo&, const ECC::Hash::Value& hvMsg) override;

			virtual bool OnUtxoRecognized(Height, const Output&, CoinID&) { return true; }
			virtual bool OnShieldedOutRecognized(const ShieldedTxo::DescriptionOutp&, const ShieldedTxo::DataParams&) { return true; }
		};
	};

}
