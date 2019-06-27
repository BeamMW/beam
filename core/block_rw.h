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
	class Block::BodyBase::RW
		:public Block::BodyBase::IMacroReader
		,public Block::BodyBase::IMacroWriter
	{

	public:

#define MBLOCK_DATA_Types(macro) \
		macro(hd) \
		macro(ui) \
		macro(uo) \
		macro(ko) \
		macro(kx)

		struct Type
		{
			enum Enum {
#define THE_MACRO(x) x,
				MBLOCK_DATA_Types(THE_MACRO)
#undef THE_MACRO
				count
			};
		};

		static const char* const s_pszSufix[Type::count];

	private:

		std::FStream m_pS[Type::count];

		Input::Ptr m_pGuardUtxoIn[2];
		Output::Ptr m_pGuardUtxoOut[2];
		TxKernel::Ptr m_pGuardKernel[2];

		Height m_pMaturity[Type::count]; // some are used as maturity, some have different meaning.
		// Those are aliases, used in read mode
		uint64_t& m_KrnSizeTotal() { return m_pMaturity[Type::hd]; }
		uint64_t& m_KrnThresholdPos() { return m_pMaturity[Type::kx]; }

		template <typename T>
		void LoadInternal(const T*& pPtr, int, typename T::Ptr* ppGuard);
		bool LoadMaturity(int);
		void NextKernelThreshold();

		template <typename T>
		void WriteInternal(const T&, int);
		void WriteMaturity(const TxElement&, int);

		bool OpenInternal(int iData);
		void PostOpen(int iData);
		void Open(bool bRead);

	public:

		RW() :m_bAutoDelete(false) {}
		~RW();

		// do not modify between Open() and Close()
		bool m_bRead;
		bool m_bAutoDelete;
		std::string m_sPath;
		Merkle::Hash m_hvContentTag; // needed to make sure all the files indeed belong to the same data set

		void GetPath(std::string&, int iData) const;

		void ROpen();
		void WCreate();

		void Flush();
		void Close();
		void Delete(); // must be closed

		void NextKernelFF(Height hMin);

		// IReader
		virtual void Clone(Ptr&) override;
		virtual void Reset() override;
		virtual void NextUtxoIn() override;
		virtual void NextUtxoOut() override;
		virtual void NextKernel() override;
		// IMacroReader
		virtual void get_Start(BodyBase&, SystemState::Sequence::Prefix&) override;
		virtual bool get_NextHdr(SystemState::Sequence::Element&) override;
		// IWriter
		virtual void Write(const Input&) override;
		virtual void Write(const Output&) override;
		virtual void Write(const TxKernel&) override;
		// IMacroWriter
		virtual void put_Start(const BodyBase&, const SystemState::Sequence::Prefix&) override;
		virtual void put_NextHdr(const SystemState::Sequence::Element&) override;
	};

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
