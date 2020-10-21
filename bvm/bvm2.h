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
#include "../core/block_crypt.h"

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

	class ProcessorContract;

	class Processor
		:public Wasm::Processor
	{
	protected:

		void InitBase(Wasm::Word* pStack, uint32_t nStackBytes, uint8_t nFill);

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


		virtual void InvokeExt(uint32_t) override;

		virtual Height get_Height() { return 0; }

		template <typename T> const T& get_AddrAsR(uint32_t nOffset) {
			return *reinterpret_cast<const T*>(get_AddrR(nOffset, sizeof(T)));
		}

		struct Header;
		const Header& ParseMod();

	public:

		enum struct Kind {
			Contract,
			Manager,

			count
		};

		virtual Kind get_Kind() = 0;

		static void Compile(ByteBuffer&, const Blob&, Kind);

	private:
		static void ResolveBinding(Wasm::Compiler& c, uint32_t iFunction, Kind);
		static void ResolveBindings(Wasm::Compiler&, Kind);
		static int32_t get_PublicMethodIdx(const Wasm::Compiler::Vec<char>& sName);
	};

	struct ProcessorPlus;
	struct ProcessorPlusEnv;

	class ProcessorContract
		:public Processor
	{
	protected:
		Wasm::Word m_pStack[Limits::StackSize / sizeof(Wasm::Word)];

		void SetVarKey(VarKey&);
		void SetVarKey(VarKey&, uint8_t nTag, const Blob&);
		void SetVarKeyInternal(VarKey&, const void* pKey, Wasm::Word nKey);

		struct FarCalls
		{
			struct Frame
				:public boost::intrusive::list_base_hook<>
			{
				ContractID m_Cid;
				ByteBuffer m_Body;
				uint32_t m_LocalDepth;
			};

			intrusive::list_autoclear<Frame> m_Stack;

		} m_FarCalls;

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


		virtual void InvokeExt(uint32_t) override;
		virtual void OnCall(Wasm::Word nAddr) override;
		virtual void OnRet(Wasm::Word nRetAddr) override;

		virtual void LoadVar(const VarKey&, uint8_t* pVal, uint32_t& nValInOut) {}
		virtual void LoadVar(const VarKey&, ByteBuffer&) {}
		virtual bool SaveVar(const VarKey&, const uint8_t* pVal, uint32_t nVal) { return false; }
		virtual bool get_HdrAt(Block::SystemState::Full& s) { return false; }

		virtual Asset::ID AssetCreate(const Asset::Metadata&, const PeerID&) { return 0; }
		virtual bool AssetEmit(Asset::ID, const PeerID&, AmountSigned) { return false; }
		virtual bool AssetDestroy(Asset::ID, const PeerID&) { return false; }

		void HandleAmount(Amount, Asset::ID, bool bLock);
		void HandleAmountInner(Amount, Asset::ID, bool bLock);
		void HandleAmountOuter(Amount, Asset::ID, bool bLock);

		uint8_t HandleRef(const ContractID&, bool bAdd);
		bool HandleRefRaw(const VarKey&, bool bAdd);

		struct AssetVar {
			VarKey m_vk;
			PeerID m_Owner;
		};

		void get_AssetStrict(AssetVar&, Asset::ID);
		void SetAssetKey(AssetVar&, Asset::ID);

		std::vector<ECC::Point::Native> m_vPks;
		ECC::Point::Native& AddSigInternal(const ECC::Point&);

		ECC::Point::Native m_FundsIO;

	public:

		Kind get_Kind() override { return Kind::Contract; }

		void InitStack(uint8_t nFill = 0);

		ECC::Hash::Processor* m_pSigValidate = nullptr; // assign it to allow sig validation
		void CheckSigs(const ECC::Point& comm, const ECC::Signature&);

		bool IsDone() const { return m_FarCalls.m_Stack.empty(); }
		Amount m_Charge = 0;

		virtual void CallFar(const ContractID&, uint32_t iMethod, Wasm::Word pArgs); // can override to invoke host code instead of interpretator (for debugging)
	};


} // namespace bvm2
} // namespace beam
