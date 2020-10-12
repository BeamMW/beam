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
#include "wasm_interpreter.h"
#include "../utility/containers.h"

namespace beam {
namespace bvm2 {

	struct Limits
	{
		static const uint32_t FarCallDepth = 32;
		static const uint32_t VarKeySize = 256;
		static const uint32_t VarSize = 0x2000; // 8K

		static const uint32_t StackSize = 0x10000; // 64K
	};

	typedef ECC::uintBig ContractID;
	// Contract unique identifier 
	void get_Cid(ContractID&, const Blob& data, const Blob& args);
	void get_AssetOwner(PeerID&, const ContractID&, const Asset::Metadata&);


	class Processor
		:public Wasm::Processor
	{
		Wasm::Word m_pStack[Limits::StackSize / sizeof(Wasm::Word)];

	protected:

		struct VarKey
		{
			struct Tag
			{
				static const uint8_t Internal = 0;
				static const uint8_t LockedAmount = 1;
				static const uint8_t Refs = 2;
				static const uint8_t OwnedAsset = 3;
			};

			uint8_t m_p[ContractID::nBytes + 1 + Limits::VarKeySize];
			uint32_t m_Size;

			void Set(const ContractID&);
			void Append(uint8_t nTag, const Blob&);
		};

		void SetVarKey(VarKey&);
		void SetVarKey(VarKey&, uint8_t nTag, const Blob&);
		void SetVarKey(VarKey&, Wasm::Word pKey, Wasm::Word nKey);

		struct FarCalls
		{
			struct Frame
				:public boost::intrusive::list_base_hook<>
			{
				ContractID m_Cid;
				ByteBuffer m_Body;
				Blob m_Data;
				uint32_t m_LocalDepth;
			};

			intrusive::list_autoclear<Frame> m_Stack;

		} m_FarCalls;

		virtual void InvokeExt(uint32_t) override;
		virtual void OnCall(Wasm::Word nAddr) override;
		virtual void OnRet(Wasm::Word nRetAddr) override;

		virtual void LoadVar(const VarKey&, uint8_t* pVal, uint32_t& nValInOut) {}
		virtual void LoadVar(const VarKey&, ByteBuffer&) {}
		virtual bool SaveVar(const VarKey&, const uint8_t* pVal, uint32_t nVal) { return false; }

		virtual void get_Hdr(Block::SystemState::Full& s) { ZeroObject(s); }
		virtual Asset::ID AssetCreate(const Asset::Metadata&, const PeerID&) { return 0; }
		virtual bool AssetEmit(Asset::ID, const PeerID&, AmountSigned) { return false; }
		virtual bool AssetDestroy(Asset::ID, const PeerID&) { return false; }

		bool LoadFixedOrZero(const VarKey&, uint8_t* pVal, uint32_t);
		bool SaveNnz(const VarKey&, const uint8_t* pVal, uint32_t);

		template <uint32_t nBytes>
		bool Load_T(const VarKey& vk, uintBig_t<nBytes>& x) {
			return LoadFixedOrZero(vk, x.m_pData, x.nBytes);
		}

		template <uint32_t nBytes>
		bool Save_T(const VarKey& vk, const uintBig_t<nBytes>& x) {
			return SaveNnz(vk, x.m_pData, x.nBytes);
		}
		std::vector<ECC::Point::Native> m_vPks;
		ECC::Point::Native& AddSigInternal(const ECC::Point&);

		ECC::Point::Native m_FundsIO;

		struct AssetVar {
			VarKey m_vk;
			PeerID m_Owner;
		};

		void get_AssetStrict(AssetVar&, Asset::ID);
		void SetAssetKey(AssetVar&, Asset::ID);

	private:
		static void ResolveBindings(Wasm::Compiler&);
		static int32_t get_PublicMethodIdx(const Wasm::Compiler::Vec<char>& sName);

	public:

		bool IsDone() const { return m_FarCalls.m_Stack.empty(); }
		Amount m_Charge = 0;

		ECC::Hash::Processor* m_pSigValidate = nullptr; // assign it to allow sig validation
		void CheckSigs(const ECC::Point& comm, const ECC::Signature&);

		std::ostringstream* m_pDbg = nullptr;

		void InitStack(const Blob& args, uint8_t nFill = 0); // initial arguments
		void CallFar(const ContractID&, uint32_t iMethod, Wasm::Word pArgs);

		static void Compile(ByteBuffer&, const Blob&);
	};


} // namespace bvm2
} // namespace beam
