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
#include "invoke_data.h"

namespace Shaders {

    typedef ECC::Point PubKey;
	typedef ECC::Point Secp_point_data;
	typedef ECC::Scalar Secp_scalar_data;
    typedef beam::Asset::ID AssetID;
    typedef ECC::uintBig ContractID;
	typedef ECC::uintBig ShaderID;
	typedef ECC::uintBig HashValue;
	typedef beam::uintBig_t<64> HashValue512;
    using beam::Amount;
    using beam::Height;
	using beam::Timestamp;
	using beam::HeightPos;

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
	using Shaders::Secp_point_data;
	using Shaders::Secp_scalar_data;
	using Shaders::AssetID;
	using Shaders::ContractID;
	using Shaders::ShaderID;
	using Shaders::Amount;
	using Shaders::Height;
	using Shaders::FundsChange;
	using Shaders::SigRequest;
	using Shaders::HashObj;
	using Shaders::Secp_scalar;
	using Shaders::Secp_point;
	using Shaders::HashValue;
	using Shaders::HashValue512;
	using Shaders::BlockHeader;

	namespace Merkle {
		using namespace Shaders::Merkle;
	}

	struct ErrorSubType
	{
		static const uint32_t NoCharge = 1;
		static const uint32_t Internal = 2; // shader called Halt
		static const uint32_t BadSignature = 3;
		static const uint32_t FundsIO = 4;
	};

	struct Limits
	{
		static const uint32_t FarCallDepth = 32;
		static const uint32_t VarKeySize = 256;
		static const uint32_t VarSize_0 = 0x2000; // 8K
		static const uint32_t VarSize_4 = 0x100000; // 1MB, past HF4

		static const uint32_t StackSize = 0x10000; // 64K
		static const uint32_t HeapSize = 0x100000; // 1MB

		static const uint32_t HashObjects = 8;
		static const uint32_t SecScalars = 16;
		static const uint32_t SecPoints = 16;

		static const uint32_t BlockCharge = 100*1000*1000; // 100 mln units

		template <uint32_t nMaxOps>
		struct ChargeFor {
			static const uint32_t V = (BlockCharge + nMaxOps - 1) / nMaxOps;
			static_assert(V, "");
		};

		struct Cost
		{
			static const uint32_t Cycle				= ChargeFor<20*1000*1000>::V;
			static const uint32_t MemOp				= ChargeFor<2*1000*1000>::V;
			static const uint32_t MemOpPerByte		= ChargeFor<50*1000*1000>::V;
			static const uint32_t HeapOp			= ChargeFor<1000*1000>::V;
			static const uint32_t LoadVar			= ChargeFor<20*1000>::V;
			static const uint32_t LoadVarPerByte	= ChargeFor<2*1000*1000>::V;
			static const uint32_t SaveVar			= ChargeFor<5*1000>::V;
			static const uint32_t SaveVarPerByte	= ChargeFor<1000*1000>::V;
			static const uint32_t Log				= ChargeFor<20*1000>::V;
			static const uint32_t LogPerByte		= ChargeFor<1000*1000>::V;
			static const uint32_t UpdateShader		= ChargeFor<1*1000>::V;
			static const uint32_t CallFar			= ChargeFor<10*1000>::V;
			static const uint32_t AddSig			= ChargeFor<10*1000>::V;
			static const uint32_t AssetManage		= ChargeFor<1000>::V;
			static const uint32_t AssetEmit			= ChargeFor<20*1000>::V;
			static const uint32_t FundsLock			= ChargeFor<50*1000>::V;
			static const uint32_t HashOp			= ChargeFor<1000*1000>::V;
			static const uint32_t HashWrite			= ChargeFor<5 * 1000*1000>::V;
			static const uint32_t HashWritePerByte	= ChargeFor<50*1000*1000>::V;

			static const uint32_t Secp_ScalarInv		= ChargeFor<5*1000>::V;
			static const uint32_t Secp_Point_Import		= ChargeFor<5*1000>::V;
			static const uint32_t Secp_Point_Export		= ChargeFor<5*1000>::V;
			static const uint32_t Secp_Point_Multiply	= ChargeFor<2*1000>::V;

			static const uint32_t BeamHashIII		= ChargeFor<20*1000>::V;
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

		std::vector<Wasm::Word> m_vStack;
		void InitBase(uint32_t nStackBytes);

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
			void Test(uint32_t ptr, uint32_t size);
			void Clear();
			void OnGrow(uint32_t nOld, uint32_t nNew);

			uint32_t get_UnusedAtEnd(uint32_t nEnd) const;

		} m_Heap;

		std::vector<uint8_t> m_vHeap;

		bool HeapAllocEx(uint32_t&, uint32_t size);
		void HeapFreeEx(uint32_t);
		void HeapReserveStrict(uint32_t);

		struct VarKey
		{
			typedef Shaders::KeyTag Tag;

			uint8_t m_p[ContractID::nBytes + 1 + Limits::VarKeySize];
			uint32_t m_Size;

			void Set(const ContractID&);
			void Append(uint8_t nTag, const Blob&);

			Blob ToBlob() const { return Blob(m_p, m_Size); }
		};


		virtual void InvokeExt(uint32_t) override;

		virtual uint32_t get_HeapLimit() { return 0; }
		virtual Height get_Height() { return 0; }
		virtual bool get_HdrAt(Block::SystemState::Full& s) { return false; }

		template <typename T> const T& get_AddrAsR(uint32_t nOffset) {
			return *reinterpret_cast<const T*>(get_AddrR(nOffset, sizeof(T)));
		}
		template <typename T> T& get_AddrAsW(uint32_t nOffset) {
			return *reinterpret_cast<T*>(get_AddrW(nOffset, sizeof(T)));
		}

		template <typename T>
		static uint32_t ToArrSize(uint32_t nCount)
		{
			uint32_t nSize = sizeof(T) * nCount;
			Wasm::Test(nSize / sizeof(T) == nCount); // overflow test
			return nSize;
		}

		template <typename T> const T* get_ArrayAddrAsR(uint32_t nOffset, uint32_t nCount) {
			return reinterpret_cast<const T*>(get_AddrR(nOffset, ToArrSize<T>(nCount)));
		}

		template <typename T> T* get_ArrayAddrAsW(uint32_t nOffset, uint32_t nCount) {
			return reinterpret_cast<T*>(get_AddrW(nOffset, ToArrSize<T>(nCount)));
		}

		struct Header;
		const Header& ParseMod();

		const char* RealizeStr(Wasm::Word, uint32_t& nLenOut);
		const char* RealizeStr(Wasm::Word);

		void DischargeMemOp(uint32_t size);
		virtual void DischargeUnits(uint32_t size) {}

		struct DataProcessor
		{
			struct Base
				:public intrusive::set_base_hook<uint32_t>
			{
				virtual ~Base() {}
				virtual void Write(const uint8_t*, uint32_t) = 0;
				virtual uint32_t Read(uint8_t*, uint32_t) = 0;
			};

			typedef intrusive::multiset_autoclear<Base> Map;
			Map m_Map;

			struct Instance;

			Base& FindStrict(uint32_t);
			Base& FindStrict(HashObj*);

		} m_DataProcessor;

		uint32_t AddHash(std::unique_ptr<DataProcessor::Base>&&);

		static void CvtHdr(Shaders::BlockHeader::InfoBase&, const Block::SystemState::Full&);

		struct Secp
		{
			struct Scalar
			{
				struct Item
					:public intrusive::set_base_hook<uint32_t>
				{
					ECC::Scalar::Native m_Val;
				};

				typedef intrusive::multiset_autoclear<Item> Map;
				Map m_Map;

				Item& FindStrict(uint32_t);
				static uint32_t From(const Secp_scalar&);

			} m_Scalar;

			struct Point
			{
				struct Item
					:public intrusive::set_base_hook<uint32_t>
				{
					ECC::Point::Native m_Val;
				};

				typedef intrusive::multiset_autoclear<Item> Map;
				Map m_Map;

				Item& FindStrict(uint32_t);
				static uint32_t From(const Secp_point&);

			} m_Point;

		} m_Secp;

		const HeightPos* FromWasmOpt(Wasm::Word pPos, HeightPos& buf);

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

		void SetVarKey(VarKey&);
		void SetVarKey(VarKey&, uint8_t nTag, const Blob&);
		void SetVarKeyFromShader(VarKey&, uint8_t nTag, const Blob&, bool bW);

		struct FarCalls
		{
			struct Frame
				:public boost::intrusive::list_base_hook<>
			{
				ContractID m_Cid;
				ByteBuffer m_Body;
				Wasm::Word m_FarRetAddr;
				Wasm::Word m_StackPosMin;
				Wasm::Word m_StackBytesMax;
				Wasm::Word m_StackBytesRet;
			};

			intrusive::list_autoclear<Frame> m_Stack;

		} m_FarCalls;

		bool LoadFixedOrZero(const VarKey&, uint8_t* pVal, uint32_t);
		uint32_t SaveNnz(const VarKey&, const uint8_t* pVal, uint32_t);

		template <uint32_t nBytes>
		bool Load_T(const VarKey& vk, uintBig_t<nBytes>& x) {
			return LoadFixedOrZero(vk, x.m_pData, x.nBytes);
		}

		template <uint32_t nBytes>
		uint32_t Save_T(const VarKey& vk, const uintBig_t<nBytes>& x) {
			return SaveNnz(vk, x.m_pData, x.nBytes);
		}


		virtual void InvokeExt(uint32_t) override;
		virtual void OnRet(Wasm::Word nRetAddr) override;
		virtual uint32_t get_HeapLimit() override;
		virtual void DischargeUnits(uint32_t size) override;

		virtual void LoadVar(const Blob&, Blob& res) { res.n = 0; } // res is temporary
		virtual uint32_t SaveVar(const Blob&, const Blob& val) { return 0; }
		virtual uint32_t OnLog(const Blob&, const Blob& val) { return 0; }

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

		bool IsPastHF4() {
			// current heught does not include the current being-interpreted block
			return get_Height() + 1 >= Rules::get().pForks[4].m_Height;
		}

		void TestVarSize(uint32_t n)
		{
			uint32_t nMax = IsPastHF4() ? Limits::VarSize_4 : Limits::VarSize_0;
			Wasm::Test(n <= nMax);
		}

		void ToggleSidEntry(const ShaderID& sid, const ContractID& cid, bool bSet);

	public:

		Kind get_Kind() override { return Kind::Contract; }

		void InitStackPlus(uint32_t nStackBytesExtra);

		FundsChangeMap m_FundsIO;

		ECC::Hash::Processor* m_pSigValidate = nullptr; // assign it to allow sig validation
		void CheckSigs(const ECC::Point& comm, const ECC::Signature&);

		void AddRemoveShader(const ContractID&, const Blob*);

		std::vector<ECC::Point>* m_pvSigs = nullptr;

		bool IsDone() const { return m_FarCalls.m_Stack.empty(); }

		uint32_t m_Charge = Limits::BlockCharge;

		virtual void CallFar(const ContractID&, uint32_t iMethod, Wasm::Word pArgs, uint8_t bInheritContext); // can override to invoke host code instead of interpretator (for debugging)
	};


	class ProcessorManager
		:public Processor
	{
	protected:

		// aux mem we've allocated on heap
		struct AuxAlloc {
			Wasm::Word m_pPtr;
			uint32_t m_Size;
		} m_AuxAlloc;

		
		uint8_t* ResizeAux(uint32_t);
		void FreeAuxAllocGuarded();

		void DocOnNext();
		void DocEncodedText(const char*);
		void DocQuotedText(const char*);
		void DocID(const char*);
		const std::string* FindArg(const char*);

		static void DeriveKeyPreimage(ECC::Hash::Value&, const Blob&);
		void DerivePkInternal(ECC::Point::Native&, const Blob&);

		void get_SlotPreimageInternal(ECC::Hash::Value&, uint32_t);
		void get_Sk(ECC::Scalar::Native&, const ECC::Hash::Value&);
		void get_BlindSkInternal(uint32_t iRes, uint32_t iMul, uint32_t iSlot, const Blob&);

		ContractInvokeEntry& GenerateKernel(const ContractID*, uint32_t iMethod, const Blob& args, const Shaders::FundsChange*, uint32_t nFunds, bool bCvtFunds, const char* szComment, uint32_t nCharge);
		void SetKernelAdv(Height hMin, Height hMax, const PubKey& ptFullBlind, const PubKey& ptFullNonce, const ECC::Scalar& skForeignSig, uint32_t iSlotBlind, uint32_t iSlotNonce, const PubKey* pSig, uint32_t nSig, ECC::Scalar* pE);

		uint32_t VarGetProofInternal(const void* pKey, uint32_t nKey, Wasm::Word& pVal, Wasm::Word& nVal, Wasm::Word& pProof);
		uint32_t LogGetProofInternal(const HeightPos&, Wasm::Word& pProof);

		virtual void InvokeExt(uint32_t) override;
		virtual uint32_t get_HeapLimit() override;

		struct IReadVars
			:public intrusive::set_base_hook<uint32_t>
		{
			typedef std::unique_ptr<IReadVars> Ptr;
			typedef intrusive::multiset_autoclear<IReadVars> Map;

			Blob m_LastKey;
			Blob m_LastVal;

			virtual ~IReadVars() {}
			virtual bool MoveNext() = 0;
		};

		struct IReadLogs
			:public intrusive::set_base_hook<uint32_t>
		{
			typedef std::unique_ptr<IReadLogs> Ptr;
			typedef intrusive::multiset_autoclear<IReadLogs> Map;

			Blob m_LastKey;
			Blob m_LastVal;
			HeightPos m_LastPos;

			virtual ~IReadLogs() {}
			virtual bool MoveNext() = 0;
		};

		IReadVars::Map m_mapReadVars;
		IReadLogs::Map m_mapReadLogs;

		virtual void VarsEnum(const Blob& kMin, const Blob& kMax, IReadVars::Ptr&) {}
		virtual void LogsEnum(const Blob& kMin, const Blob& kMax, const HeightPos* pPosMin, const HeightPos* pPosMax, IReadLogs::Ptr&) {}

		virtual bool VarGetProof(Blob& key, ByteBuffer& val, beam::Merkle::Proof&) { return false; }
		virtual bool LogGetProof(const HeightPos&, beam::Merkle::Proof&) { return false; }
		virtual void get_ContractShader(ByteBuffer&) {} // needed when app asks to deploy a contract
		virtual bool get_SpecialParam(const char*, Blob&) { return false; }

		virtual bool SlotLoad(ECC::Hash::Value&, uint32_t iSlot) { return false; }
		virtual void SlotSave(const ECC::Hash::Value&, uint32_t iSlot) { }
		virtual void SlotErase(uint32_t iSlot) { }

		struct Comm
		{
			struct Channel
				:public intrusive::set_base_hook<ECC::Hash::Value>
			{
				uint32_t m_Cookie;

				virtual ~Channel() {} // auto

				typedef std::unique_ptr<Channel> Ptr;
				typedef intrusive::multiset_autoclear<Channel> Map;

			};

			Channel::Map m_Map;

			struct Rcv
				:public boost::intrusive::list_base_hook<>
			{
				uint32_t m_Cookie;
				ByteBuffer m_Msg;

				typedef intrusive::list_autoclear<Rcv> List;
			};

			Rcv::List m_Rcv;

			void Clear()
			{
				m_Map.Clear();
				m_Rcv.Clear();
			}

		} m_Comms;

		virtual void Comm_CreateListener(Comm::Channel::Ptr&, const ECC::Hash::Value&) {}
		virtual void Comm_Send(const ECC::Point&, const Blob&) {}
		virtual void Comm_Wait() { Wasm::Fail(); }

	public:

		static const uint32_t s_Slots = 10;

		std::ostream* m_pOut;
		bool m_NeedComma = false;
		bool m_RawText = false; // don't perform json-style decoration

		Key::IPKdf::Ptr m_pPKdf; // required for user-related info (account-specific pubkeys, etc.)

		Key::IKdf::Ptr m_pKdf; // gives more access to the keys. Set only when app runs in a privileged mode

		ContractInvokeData m_vInvokeData;

		std::map<std::string, std::string> m_Args;
		void set_ArgBlob(const char* sz, const Blob&);
		uint32_t AddArgs(char* szCommaSeparatedPairs);

		Kind get_Kind() override { return Kind::Manager; }

		bool IsDone() const { return m_Instruction.m_p0 == (const uint8_t*)m_Code.p; }

		void InitMem();
		void Call(Wasm::Word addr);
		void Call(Wasm::Word addr, Wasm::Word retAddr);
		void CallMethod(uint32_t iMethod);
	};


} // namespace bvm2
} // namespace beam
