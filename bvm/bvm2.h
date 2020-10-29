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

namespace Shaders {

    typedef ECC::Point PubKey;
    typedef beam::Asset::ID AssetID;
    typedef ECC::uintBig ContractID;
	typedef ECC::uintBig ShaderID;
    using beam::Amount;
    using beam::Height;

    template<bool bToShader, typename T>
    inline void ConvertOrd(T& x)
    {
        if constexpr (bToShader)
            x = beam::ByteOrder::to_le(x);
        else
            x = beam::ByteOrder::from_le(x);
    }

#include "bvm2_shared.h"
}

namespace beam {
namespace bvm2 {

	using Shaders::PubKey;
	using Shaders::AssetID;
	using Shaders::ContractID;
	using Shaders::ShaderID;
	using Shaders::Amount;
	using Shaders::Height;
	using Shaders::FundsChange;
	using Shaders::SigRequest;

	struct Limits
	{
		static const uint32_t FarCallDepth = 32;
		static const uint32_t VarKeySize = 256;
		static const uint32_t VarSize = 0x2000; // 8K

		static const uint32_t StackSize = 0x10000; // 64K
		static const uint32_t HeapSize = 0x10000; // 64K

		struct Cost
		{
			static const Amount UnitPrice = 100; // groth

			static const uint32_t Cycle = 5;
			static const uint32_t MemOpPerByte = 1;
			static const uint32_t HeapOp = 150; // in addition to per-byte price
			static const uint32_t LoadVar = 1000;
			static const uint32_t LoadVarPerByte = 2;
			static const uint32_t SaveVar = 3000;
			static const uint32_t SaveVarPerByte = 5;
			static const uint32_t CallFar = 5000;
			static const uint32_t AddSig = 5000;
			static const uint32_t AssetEmit = 10000;
		};

		struct Charge
		{
			static void Fail();
			static void Test(bool);

			uint32_t m_Units = 5000000; // max, regardless to the fee. Equivalent of ~1mln cycles
			uint32_t m_CallFar = 32;
			uint32_t m_AddSig = 1024;
			uint32_t m_AssetOps = 128;
		};
	};

	// Contract unique identifier 
	void get_ShaderID(ShaderID&, const Blob& data);
	void get_Cid(ContractID&, const Blob& data, const Blob& args);
	void get_CidViaSid(ContractID&, const ShaderID&, const Blob& args);

	void get_AssetOwner(PeerID&, const ContractID&, const Asset::Metadata&);

	class ProcessorContract;

	class Processor
		:public Wasm::Processor
	{
	protected:

		void InitBase(Wasm::Word* pStack, uint32_t nStackBytes, uint8_t nFill);

		class Heap
		{
			struct Entry
			{
				struct Size
					:public intrusive::set_base_hook<uint32_t>
				{
					IMPLEMENT_GET_PARENT_OBJ(Entry, m_Size)
				} m_Size;

				struct Pos
					:public intrusive::set_base_hook<uint32_t>
				{
					IMPLEMENT_GET_PARENT_OBJ(Entry, m_Pos)
				} m_Pos;
			};

			typedef intrusive::multiset<Entry::Size> MapSize;
			typedef intrusive::multiset<Entry::Pos> MapPos;

			MapSize m_mapSize;
			MapPos m_mapFree;
			MapPos m_mapAllocated;

			void Insert(Entry&, bool bFree);
			void Remove(Entry&, bool bFree);
			void Delete(Entry&, bool bFree);
			void UpdateSizeFree(Entry&, uint32_t newVal);
			void TryMerge(Entry&);
			Entry* Create(uint32_t nPos, uint32_t nSize, bool bFree);

		public:

			~Heap() { Clear(); }

			bool Alloc(uint32_t&, uint32_t size);
			void Free(uint32_t);
			void Clear();
			void OnGrow(uint32_t nOld, uint32_t nNew);

			uint32_t get_UnusedAtEnd(uint32_t nEnd) const;

		} m_Heap;

		std::vector<uint8_t> m_vHeap;

		bool HeapAllocEx(uint32_t&, uint32_t size);
		void HeapFreeEx(uint32_t);

		struct VarKey
		{
			struct Tag
			{
				static const uint8_t Internal = 0;
				static const uint8_t LockedAmount = 1;
				static const uint8_t Refs = 2;
				static const uint8_t OwnedAsset = 3;

				static const uint8_t ShaderID = 16; // not really a contract var. Needed to distinguish between contract vars and sid-cid pairs
			};

			uint8_t m_p[ContractID::nBytes + 1 + Limits::VarKeySize];
			uint32_t m_Size;

			void Set(const ContractID&);
			void Append(uint8_t nTag, const Blob&);
		};


		virtual void InvokeExt(uint32_t) override;

		virtual uint32_t get_HeapLimit() { return 0; }
		virtual Height get_Height() { return 0; }

		template <typename T> const T& get_AddrAsR(uint32_t nOffset) {
			return *reinterpret_cast<const T*>(get_AddrR(nOffset, sizeof(T)));
		}
		template <typename T> T& get_AddrAsW(uint32_t nOffset) {
			return *reinterpret_cast<T*>(get_AddrW(nOffset, sizeof(T)));
		}

		template <typename T> const T* get_ArrayAddrAsR(uint32_t nOffset, uint32_t nCount) {
			uint32_t nSize = sizeof(T) * nCount;
			Wasm::Test(nSize / sizeof(T) == nCount); // overflow test
			return reinterpret_cast<const T*>(get_AddrR(nOffset, nSize));
		}

		struct Header;
		const Header& ParseMod();

		const char* RealizeStr(Wasm::Word, uint32_t& nLenOut);
		const char* RealizeStr(Wasm::Word);

		void DischargeMemOp(uint32_t size);
		virtual void DischargeUnits(uint32_t size) {}

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

	class FundsChangeMap
	{
		static void Set(ECC::Scalar::Native&, Amount, bool bLock);

	public:
		std::map<Asset::ID, ECC::Scalar::Native> m_Map;

		void Process(Amount val, Asset::ID, bool bLock);
		void ToCommitment(ECC::Point::Native&) const;
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
		virtual uint32_t get_HeapLimit() override;
		virtual void DischargeUnits(uint32_t size) override;

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

		FundsChangeMap m_FundsIO;

		static void DischargeVar(uint32_t& trg, uint32_t val);

	public:

		Kind get_Kind() override { return Kind::Contract; }

		void InitStack(uint8_t nFill = 0);

		ECC::Hash::Processor* m_pSigValidate = nullptr; // assign it to allow sig validation
		void CheckSigs(const ECC::Point& comm, const ECC::Signature&);

		bool IsDone() const { return m_FarCalls.m_Stack.empty(); }

		Limits::Charge m_Charge;

		virtual void CallFar(const ContractID&, uint32_t iMethod, Wasm::Word pArgs); // can override to invoke host code instead of interpretator (for debugging)
	};


	class ProcessorManager
		:public Processor
	{
	protected:

		std::vector<Wasm::Word> m_vStack; // too large to have it as a member (this obj may be allocated on stack)

		uint32_t m_LocalDepth;

		// aux mem we've allocated on heap
		struct AuxAlloc {
			Wasm::Word m_pPtr;
			uint32_t m_Size;
		} m_AuxAlloc;

		bool m_EnumVars;

		void FreeAuxAllocGuarded();

		void DocOnNext();
		void DocEncodedText(const char*);
		void DocQuotedText(const char*);
		void DocID(const char*);
		const std::string* FindArg(const char*);

		void DeriveKeyPreimage(ECC::Hash::Value&, const Blob&);

		virtual void InvokeExt(uint32_t) override;
		virtual void OnCall(Wasm::Word nAddr) override;
		virtual void OnRet(Wasm::Word nRetAddr) override;
		virtual uint32_t get_HeapLimit();

		virtual void VarsEnum(const Blob& kMin, const Blob& kMax) {}
		virtual bool VarsMoveNext(Blob& key, Blob& val) { return false; }
		virtual void DerivePk(ECC::Point& pubKey, const ECC::Hash::Value&) { ZeroObject(pubKey);  }
		virtual void GenerateKernel(const ContractID*, uint32_t iMethod, const Blob& args, const Shaders::FundsChange*, uint32_t nFunds, const ECC::Hash::Value* pSig, uint32_t nSig, Amount fee) {}

	public:

		std::ostream* m_pOut;
		bool m_NeedComma = false;

		std::map<std::string, std::string> m_Args;
		void set_ArgBlob(const char* sz, const Blob&);
		uint32_t AddArgs(char* szCommaSeparatedPairs);

		Kind get_Kind() override { return Kind::Manager; }

		void InitMem();
		void Call(Wasm::Word addr);
		void Call(Wasm::Word addr, Wasm::Word retAddr);
		void CallMethod(uint32_t iMethod);
	};


} // namespace bvm2
} // namespace beam
