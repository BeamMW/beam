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

#define _CRT_SECURE_NO_WARNINGS // sprintf
#include "bvm2_impl.h"
#include <sstream>

#if defined(__ANDROID__) || !defined(BEAM_USE_AVX)
#include "crypto/blake/ref/blake2.h"
#else
#include "crypto/blake/sse/blake2.h"
#endif

#include "ethash/include/ethash/keccak.h"
#include "ethash/include/ethash/ethash.h"

namespace beam {
namespace bvm2 {

	namespace Impl
	{
		namespace Env {
			void Memcpy(void* pDst, const void* pSrc, uint32_t n) {
				memcpy(pDst, pSrc, n);
			}
			void Memset(void* pDst, char c, uint32_t n) {
				memset(pDst, c, n);
			}
			uint8_t Memis0(const void* p, uint32_t n) {
				return !!memis0(p, n);
			}
		}

		namespace Utils {
			template <typename T>
			inline T FromLE(T x) {
				return beam::ByteOrder::from_le(x);
			}
		}

		struct HashProcessor {

			struct Blake2b_Base
			{
				blake2b_state m_State;

				bool Init(const void* pPersonal, uint32_t nPersonal, uint32_t nResultSize)
				{
					blake2b_param pars = { 0 };
					pars.digest_length = static_cast<uint8_t>(nResultSize);
					pars.fanout = 1;
					pars.depth = 1;

					memcpy(pars.personal, pPersonal, std::min<size_t>(sizeof(pars.personal), nPersonal));

					return !blake2b_init_param(&m_State, &pars);
				}

				void Write(const void* p, uint32_t n)
				{
					blake2b_update(&m_State, p, n);
				}

				uint32_t Read(void* p, uint32_t n)
				{
					if (blake2b_final(&m_State, p, n))
						return 0;

					assert(m_State.outlen <= n);

					return static_cast<uint32_t>(m_State.outlen);
				}

				template <typename T>
				void operator >> (T& res) {
					Read(&res, sizeof(res));

				}
			};

			struct Blake2b
				:public Blake2b_Base
			{
				Blake2b(const void* pPersonal, uint32_t nPersonal, uint32_t nResultSize)
				{
					BEAM_VERIFY(Init(pPersonal, nPersonal, nResultSize)); // should no fail with internally specified params
				}
			};
		};

#include "Shaders/BeamHashIII.h"
	}

	void get_ShaderID(ShaderID& sid, const Blob& data)
	{
		ECC::Hash::Processor()
			<< "bvm.shader.id"
			<< data.n
			<< data
			>> sid;
	}

	void get_CidViaSid(ContractID& cid, const ShaderID& sid, const Blob& args)
	{
		ECC::Hash::Processor()
			<< "bvm.cid"
			<< sid
			<< args.n
			<< args
			>> cid;
	}

	void get_Cid(ContractID& cid, const Blob& data, const Blob& args)
	{
		ShaderID sid;
		get_ShaderID(sid, data);
		get_CidViaSid(cid, sid, args);
	}

	void get_AssetOwner(PeerID& pidOwner, const ContractID& cid, const Asset::Metadata& md)
	{
		ECC::Hash::Processor()
			<< "bvm.a.own"
			<< cid
			<< md.m_Hash
			>> pidOwner;
	}

	/////////////////////////////////////////////
	// Processor
#pragma pack (push, 1)
	struct Processor::Header
	{
		static const uint32_t s_Version = 2;

		uint32_t m_Version;
		uint32_t m_NumMethods;
		uint32_t m_hdrData0;
		uint32_t m_hdrTable0;

		static const uint32_t s_MethodsMin = 2; // c'tor and d'tor
		static const uint32_t s_MethodsMax = 1U << 28; // would be much much less of course, this is just to ensure no-overflow for offset calculation

		uint32_t m_pMethod[s_MethodsMin]; // var size
	};
#pragma pack (pop)

	void Processor::InitBase(Wasm::Word* pStack, uint32_t nStackBytes, uint8_t nFill)
	{
		ZeroObject(m_Code);
		ZeroObject(m_Data);
		ZeroObject(m_LinearMem);
		ZeroObject(m_Instruction);

		m_Stack.m_pPtr = pStack;
		m_Stack.m_BytesMax = nStackBytes;
		m_Stack.m_BytesCurrent = m_Stack.m_BytesMax;
		m_Stack.m_Pos = 0;
		m_Stack.m_PosMin = 0;

		memset(pStack, nFill, nStackBytes);

        decltype(m_vHeap)().swap(m_vHeap);
		m_Heap.Clear();

		m_DataProcessor.m_Map.Clear();
	}

	void ProcessorContract::InitStack(uint8_t nFill /* = 0 */)
	{
		InitBase(m_pStack, sizeof(m_pStack), nFill);
	}

	void ProcessorManager::InitMem()
	{
		const uint32_t nStackBytes = 0x20000; // 128K

		m_vStack.resize(nStackBytes / sizeof(Wasm::Word));
		InitBase(&m_vStack.front(), nStackBytes, 0);

		ZeroObject(m_AuxAlloc);
		m_EnumType = EnumType::None;
		m_NeedComma = false;
	}

	const Processor::Header& Processor::ParseMod()
	{
		Wasm::Test(m_Code.n >= sizeof(Header));
		const Header& hdr = *reinterpret_cast<const Header*>(m_Code.p);

		Wasm::Test(ByteOrder::from_le(hdr.m_Version) == hdr.s_Version);
		uint32_t nMethods = ByteOrder::from_le(hdr.m_NumMethods);
		Wasm::Test((nMethods - Header::s_MethodsMin <= Header::s_MethodsMax - Header::s_MethodsMin));

		uint32_t nHdrSize = sizeof(Header) + sizeof(Wasm::Word) * (nMethods - Header::s_MethodsMin);
		Wasm::Test(nHdrSize <= m_Code.n);

		m_Data.n = m_Code.n - nHdrSize;
		m_Data.p = reinterpret_cast<const uint8_t*>(m_Code.p) + nHdrSize;
		m_prData0 = ByteOrder::from_le(hdr.m_hdrData0);

		m_prTable0 = ByteOrder::from_le(hdr.m_hdrTable0);
		Wasm::Test(m_prTable0 <= m_Code.n);

		return hdr;
	}

	void ProcessorContract::CallFar(const ContractID& cid, uint32_t iMethod, Wasm::Word pArgs)
	{
		struct MyCheckpoint :public Wasm::Checkpoint
		{
			const ContractID& m_Cid;
			uint32_t m_iMethod;

			MyCheckpoint(const ContractID& cid, uint32_t iMethod)
				:m_Cid(cid)
				,m_iMethod(iMethod)
			{
			}

			void Dump(std::ostream& os) override {
				os << "CallFar cid=" << m_Cid << ", iMethod=" << m_iMethod;
			}
		} cp(cid, iMethod);

		DischargeUnits(Limits::Cost::CallFar);

		auto nRetAddr = get_Ip();

		Wasm::Test(m_FarCalls.m_Stack.size() < Limits::FarCallDepth);
		auto& x = *m_FarCalls.m_Stack.Create_back();

		x.m_Cid = cid;
		x.m_FarRetAddr = nRetAddr;
		x.m_StackBytesMax = m_Stack.m_BytesMax;
		x.m_StackBytesRet = m_Stack.m_BytesCurrent;

		x.m_StackPosMin = m_Stack.m_PosMin;
		m_Stack.m_PosMin = m_Stack.m_Pos;

		VarKey vk;
		SetVarKey(vk);
		LoadVar(vk, x.m_Body);

		m_Code = x.m_Body;
		const Header& hdr = ParseMod();
		Wasm::Test(iMethod < ByteOrder::from_le(hdr.m_NumMethods));

		m_Stack.Push(pArgs);
		m_Stack.Push(0); // retaddr, set dummy for far call

		uint32_t nAddr = ByteOrder::from_le(hdr.m_pMethod[iMethod]);
		OnCall(nAddr);
	}

	void ProcessorContract::OnRet(Wasm::Word nRetAddr)
	{
		if (!nRetAddr)
		{
			Wasm::Test(m_Stack.m_Pos == m_Stack.m_PosMin);

			auto& x = m_FarCalls.m_Stack.back();
			Wasm::Test(m_Stack.m_BytesCurrent == x.m_StackBytesRet);

			nRetAddr = x.m_FarRetAddr;

			m_Stack.m_PosMin = x.m_StackPosMin;
			m_Stack.m_BytesMax = x.m_StackBytesMax;

			m_FarCalls.m_Stack.Delete(x);
			if (m_FarCalls.m_Stack.empty())
				return; // finished

			m_Code = m_FarCalls.m_Stack.back().m_Body;
			ParseMod(); // restore code/data sections
		}

		Processor::OnRet(nRetAddr);
	}

	uint32_t ProcessorContract::get_HeapLimit()
	{
		return Limits::HeapSize;
	}

	void Processor::VarKey::Set(const ContractID& cid)
	{
		memcpy(m_p, cid.m_pData, ContractID::nBytes);
		m_Size = ContractID::nBytes;
	}

	void Processor::VarKey::Append(uint8_t nTag, const Blob& blob)
	{
		m_p[m_Size++] = nTag;

		assert(m_Size + blob.n <= _countof(m_p));
		memcpy(m_p + m_Size, blob.p, blob.n);
		m_Size += blob.n;
	}

	void ProcessorContract::SetVarKey(VarKey& vk)
	{
		vk.Set(m_FarCalls.m_Stack.back().m_Cid);
	}

	void ProcessorContract::SetVarKey(VarKey& vk, uint8_t nTag, const Blob& blob)
	{
		SetVarKey(vk);
		vk.Append(nTag, blob);
	}

	/////////////////////////////////////////////
	// Compilation

#define STR_MATCH(vec, txt) ((vec.n == sizeof(txt)-1) && !memcmp(vec.p, txt, sizeof(txt)-1))

	int32_t Processor::get_PublicMethodIdx(const Wasm::Compiler::Vec<char>& sName)
	{
		if (STR_MATCH(sName, "Ctor"))
			return 0;
		if (STR_MATCH(sName, "Dtor"))
			return 1;

		static const char szPrefix[] = "Method_";
		if (sName.n < sizeof(szPrefix) - 1)
			return -1;

		if (memcmp(sName.p, szPrefix, sizeof(szPrefix) - 1))
			return -1;

		// TODO - rewrite this
		char szTxt[11];
		size_t nLen = std::min(sizeof(szTxt) - 1, sName.n - (sizeof(szPrefix) - 1));
		memcpy(szTxt, sName.p + sizeof(szPrefix) - 1, nLen);
		szTxt[nLen] = 0;

		return atoi(szTxt);
	}

	void Processor::DischargeMemOp(uint32_t size)
	{
		// don't care about overflow. Assume avail max mem size multiplied by cost won't overflow
		DischargeUnits(size * Limits::Cost::MemOpPerByte);
	}

	void ProcessorContract::DischargeUnits(uint32_t n)
	{
		if (m_Charge < n)
		{
			struct MyCheckpoint :public Wasm::Checkpoint
			{
				void Dump(std::ostream& os) override {
					os << "Discharge";
				}

				uint32_t get_Type() override {
					return ErrorSubType::NoCharge;
				}

			} cp;

			Wasm::Fail("no Charge");
		}

		m_Charge -= n;
	}
	void Processor::Compile(ByteBuffer& res, const Blob& src, Kind kind)
	{
		Wasm::CheckpointTxt cp("Wasm/compile");

		Wasm::Reader inp;
		inp.m_p0 = reinterpret_cast<const uint8_t*>(src.p);
		inp.m_p1 = inp.m_p0 + src.n;

		Wasm::Compiler c;
		c.Parse(inp);

		ResolveBindings(c, kind);

		typedef std::map<uint32_t, uint32_t> MethodMap; // method num -> func idx
		MethodMap hdrMap;

		for (uint32_t i = 0; i < c.m_Exports.size(); i++)
		{
			auto& x = c.m_Exports[i];
			if (x.m_Kind)
				continue; // not a function

			int32_t iMethod = get_PublicMethodIdx(x.m_sName);
			if (iMethod < 0)
				continue;

			Wasm::Test(hdrMap.end() == hdrMap.find(iMethod)); // duplicates check
			hdrMap[iMethod] = x.m_Idx;
		}

		uint32_t nNumMethods = static_cast<uint32_t>(hdrMap.size());

		Wasm::Test(nNumMethods >= Header::s_MethodsMin);
		Wasm::Test(hdrMap.rbegin()->first == nNumMethods - 1); // should be no gaps

		// header
		uint32_t nSizeHdr = sizeof(Header) + sizeof(Wasm::Word) * (nNumMethods - Header::s_MethodsMin);
		c.m_Result.resize(nSizeHdr + c.m_Data.size());
		auto* pHdr = reinterpret_cast<Header*>(&c.m_Result.front());

		pHdr->m_Version = ByteOrder::to_le(Header::s_Version);
		pHdr->m_NumMethods = ByteOrder::to_le(nNumMethods);
		pHdr->m_hdrData0 = ByteOrder::to_le(c.m_cmplData0);

		if (!c.m_Data.empty())
			memcpy(&c.m_Result.front() + nSizeHdr, &c.m_Data.front(), c.m_Data.size());

		// the methods themselves are not assigned yet

		c.Build();

		pHdr = reinterpret_cast<Header*>(&c.m_Result.front());
		pHdr->m_hdrTable0 = ByteOrder::to_le(c.m_cmplTable0);

		for (auto it = hdrMap.begin(); hdrMap.end() != it; it++)
		{
			uint32_t iMethod = it->first;
			uint32_t iFunc = it->second;
			pHdr->m_pMethod[iMethod] = ByteOrder::to_le(c.m_Labels.m_Items[iFunc]);
		}

		res = std::move(c.m_Result);
	}


	/////////////////////////////////////////////
	// Redirection of calls

	template <typename T>
	struct ParamWrap {
		typedef T Type;
		Type V;
		Wasm::Processor& operator = (Wasm::Processor& p) {
			V = p.m_Stack.Pop<Type>();
			return p;
		}

		void Set(Type v) {
			V = v;
		}

		static_assert(std::numeric_limits<T>::is_integer);
		static_assert(sizeof(T) <= sizeof(Wasm::Word) * 2);

		static const uint8_t s_Code = sizeof(T) <= sizeof(Wasm::Word) ?
			Wasm::TypeCode::i32 :
			Wasm::TypeCode::i64;

		static void TestRetType(const Wasm::Compiler::Vec<uint8_t>& v) {
			Wasm::Test((1 == v.n) && (s_Code == *v.p));
		}
	};

	template <typename T>
	struct ParamWrap<T*>
		:public ParamWrap<Wasm::Word>
	{
		Wasm::Processor& operator = (Wasm::Processor& p) {
			return ParamWrap<Wasm::Word>::operator = (p);
		}
	};

	template <typename T>
	struct ParamWrap<T&>
		:public ParamWrap<Wasm::Word>
	{
		Wasm::Processor& operator = (Wasm::Processor& p) {
			return ParamWrap<Wasm::Word>::operator = (p);
		}
	};

	template <>
	struct ParamWrap<void>
	{
		typedef void Type;

		static void TestRetType(const Wasm::Compiler::Vec<uint8_t>& v) {
			Wasm::Test(!v.n);
		}
	};

	template <typename TRes> struct Caller {
		template <typename TArgs, typename TProcessor>
		static void Call(TProcessor& me, const TArgs& args) {
			me.m_Stack.template Push<TRes>(args.Call(me));
		}
	};
	template <> struct Caller<void> {
		template <typename TArgs, typename TProcessor>
		static void Call(TProcessor& me, const TArgs& args) {
			args.Call(me);
		}
	};


#define MACRO_NOP
#define MACRO_COMMA ,
#define PAR_DECL(type, name) ParamWrap<type>::Type name
#define THE_MACRO(id, ret, name) \
		typedef ParamWrap<ret>::Type RetType_##name; \
		typedef ret RetTypeHost_##name; \
		RetType_##name OnMethod_##name(BVMOp_##name(PAR_DECL, MACRO_COMMA));

	typedef ECC::Point PubKey;
	typedef Asset::ID AssetID;

	struct ProcessorPlus
		:public ProcessorPlusEnv
	{
		typedef ProcessorPlus TProcessor;
		static TProcessor& From(Processor& p)
		{
			static_assert(sizeof(TProcessor) == sizeof(p));
			return Cast::Up<TProcessor>(p);
		}

		template <typename TOut, typename TIn>
		TOut ToHost(TIn x) { return x; }

		template <typename TOut, typename TIn>
		TOut ToMethod(TIn x) { return x; }

		void InvokeExtPlus(uint32_t nBinding);

		BVMOpsAll_Common(THE_MACRO)
	};

	template<>
	const char* ProcessorPlus::ToHost<const char*, Wasm::Word>(Wasm::Word sz)
	{
		return RealizeStr(sz);
	}

	template<>
	Wasm::Word ProcessorPlus::ToMethod<Wasm::Word, HashObj*>(HashObj* p)
	{
		return static_cast<Wasm::Word>(reinterpret_cast<size_t>(p));
	}

	struct ProcessorPlus_Contract
		:public ProcessorPlusEnv_Contract
	{
		typedef ProcessorPlus_Contract TProcessor;
		static TProcessor& From(ProcessorContract& p)
		{
			static_assert(sizeof(TProcessor) == sizeof(p));
			return Cast::Up<TProcessor>(p);
		}

		void InvokeExtPlus(uint32_t nBinding);
		BVMOpsAll_Contract(THE_MACRO)
	};

	struct ProcessorPlus_Manager
		:public ProcessorPlusEnv_Manager
	{
		typedef ProcessorPlus_Manager TProcessor;
		static TProcessor& From(ProcessorManager& p)
		{
			static_assert(sizeof(TProcessor) == sizeof(p));
			return Cast::Up<TProcessor>(p);
		}

		void InvokeExtPlus(uint32_t nBinding);
		BVMOpsAll_Manager(THE_MACRO)
	};

#undef THE_MACRO
#undef PAR_DECL

	template <typename T>
	typename uintBigFor<T>::Type LogFrom(T x) {
		return typename uintBigFor<T>::Type(x);
	}

	uint32_t LogFrom(uint8_t x) {
		return x;
	}

#define PAR_PASS(type, name) m_##name.V
#define PAR_DECL(type, name) ParamWrap<type> m_##name;
#define PAR_ASSIGN(type, name) args.m_##name =
#define PAR_DUMP(type, name) << "," #name "=" << LogFrom(m_##name.V)

#define THE_MACRO(id, ret, name) \
		case id: { \
			if (m_Dbg.m_ExtCall) \
				*m_Dbg.m_pOut << "  " #name << std::endl; \
			struct Args :public Wasm::Checkpoint { \
				BVMOp_##name(PAR_DECL, MACRO_NOP) \
				RetType_##name Call(TProcessor& me) const { return me.OnMethod_##name(BVMOp_##name(PAR_PASS, MACRO_COMMA)); } \
				virtual void Dump(std::ostream& os) override { \
					os << #name BVMOp_##name(PAR_DUMP, MACRO_NOP); \
				} \
			} args; \
			BVMOp_##name(PAR_ASSIGN, MACRO_NOP) *this; \
			Caller<RetType_##name>::Call(*this, args); \
		} break;

	void ProcessorPlus::InvokeExtPlus(uint32_t nBinding)
	{
		switch (nBinding)
		{
		BVMOpsAll_Common(THE_MACRO)
		default:
			Wasm::Processor::InvokeExt(nBinding);
		}
	}

	void ProcessorPlus_Contract::InvokeExtPlus(uint32_t nBinding)
	{
		switch (nBinding)
		{
		BVMOpsAll_Contract(THE_MACRO)
		default:
			ProcessorPlus::From(*this).InvokeExtPlus(nBinding);
		}
	}

	void ProcessorPlus_Manager::InvokeExtPlus(uint32_t nBinding)
	{
		switch (nBinding)
		{
		BVMOpsAll_Manager(THE_MACRO)
		default:
			ProcessorPlus::From(*this).InvokeExtPlus(nBinding);
		}
	}

#undef THE_MACRO
#undef PAR_PASS
#undef PAR_DECL
#undef PAR_ASSIGN

	void Processor::InvokeExt(uint32_t nBinding)
	{
		ProcessorPlus::From(*this).InvokeExtPlus(nBinding);
	}

	void ProcessorContract::InvokeExt(uint32_t nBinding)
	{
		ProcessorPlus_Contract::From(*this).InvokeExtPlus(nBinding);
	}

	void ProcessorManager::InvokeExt(uint32_t nBinding)
	{
		ProcessorPlus_Manager::From(*this).InvokeExtPlus(nBinding);
	}

	void TestStackPtr(const Wasm::Compiler::GlobalVar& x)
	{
		Wasm::Test(x.m_IsVariable && (Wasm::TypeCode::i32 == x.m_Type));
	}

	void Processor::ResolveBinding(Wasm::Compiler& c, uint32_t iFunction, Kind kind)
	{
		auto& x = c.m_ImportFuncs[iFunction];

		if (!STR_MATCH(x.m_sMod, "env"))
			Wasm::Fail(); // imports from other modules are not supported

		const auto& tp = c.m_Types[x.m_TypeIdx];

#define PAR_TYPECODE(type, name) ParamWrap<type>::s_Code,
#define THE_MACRO(id, ret, name) \
		if (STR_MATCH(x.m_sName, #name)) { \
			x.m_Binding = id; \
			/* verify signature */ \
			const uint8_t pSig[] = { BVMOp_##name(PAR_TYPECODE, MACRO_NOP) 0 }; \
			Wasm::Test(tp.m_Args.n == _countof(pSig) - 1); \
			Wasm::Test(!memcmp(tp.m_Args.p, pSig, sizeof(pSig) - 1)); \
			ParamWrap<ret>::TestRetType(tp.m_Rets); \
			return; \
		}

		BVMOpsAll_Common(THE_MACRO)

		if (Kind::Contract == kind)
		{
			BVMOpsAll_Contract(THE_MACRO)
		}

		if (Kind::Manager == kind)
		{
			BVMOpsAll_Manager(THE_MACRO)
		}


#undef THE_MACRO
#undef PAR_TYPECODE

		Wasm::Fail(); // not found
	}

	void Processor::ResolveBindings(Wasm::Compiler& c, Kind kind)
	{
		for (uint32_t i = 0; i < c.m_ImportFuncs.size(); i++)
			ResolveBinding(c, i, kind);

		bool bStackPtrImported = false;
		for (uint32_t i = 0; i < c.m_ImportGlobals.size(); i++)
		{
			auto& x = c.m_ImportGlobals[i];

			if (STR_MATCH(x.m_sMod, "env"))
			{
				if (STR_MATCH(x.m_sName, "__stack_pointer"))
				{
					TestStackPtr(x);
					x.m_Binding = static_cast<uint32_t>(Wasm::VariableType::StackPointer);
					bStackPtrImported = true;
				}
			}

			// ignore unrecognized variables, they may not be used
		}

		if (!c.m_Globals.empty())
		{
			// we don't support globals, but it could be the stack pointer if the module wasn't built as a "shared" lib.
			Wasm::Test(!bStackPtrImported && (1 == c.m_Globals.size()));

			auto& g0 = c.m_Globals.front();
			TestStackPtr(g0);

			// work-around
			auto& x = c.m_ImportGlobals.emplace_back();
			ZeroObject(x);
			x.m_Binding = static_cast<uint32_t>(Wasm::VariableType::StackPointer);

			Cast::Down<Wasm::Compiler::GlobalVar>(x) = g0;
			c.m_Globals.clear();
		}

	}



	/////////////////////////////////////////////
	// Methods
	namespace ProcessorFromMethod {
#define THE_MACRO(id, ret, name) typedef ProcessorPlus name##_Type; typedef ProcessorPlusEnv name##_TypeEnv;
		BVMOpsAll_Common(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(id, ret, name) typedef ProcessorPlus_Contract name##_Type; typedef ProcessorPlusEnv_Contract name##_TypeEnv;
		BVMOpsAll_Contract(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(id, ret, name) typedef ProcessorPlus_Manager name##_Type; typedef ProcessorPlusEnv_Manager name##_TypeEnv;
		BVMOpsAll_Manager(THE_MACRO)
#undef THE_MACRO
	}

#define BVM_METHOD_PAR_DECL(type, name) ParamWrap<type>::Type name
#define BVM_METHOD_PAR_DECL_HOST(type, name) type name
#define BVM_METHOD_PAR_PASS_TO_METHOD(type, name) ProcessorPlus::From(*this).ToMethod<ParamWrap<type>::Type, type>(name)
#define BVM_METHOD_PAR_PASS_TO_HOST(type, name) ProcessorPlus::From(*this).ToHost<type>(name)
#define BVM_METHOD(name) ProcessorFromMethod::name##_Type::RetType_##name ProcessorFromMethod::name##_Type::OnMethod_##name(BVMOp_##name(BVM_METHOD_PAR_DECL, MACRO_COMMA))
#define BVM_METHOD_HOST(name) ProcessorFromMethod::name##_Type::RetTypeHost_##name ProcessorFromMethod::name##_TypeEnv::OnHost_##name(BVMOp_##name(BVM_METHOD_PAR_DECL_HOST, MACRO_COMMA))

#define BVM_METHOD_AUTO_INVOKE(name) ProcessorFromMethod::name##_Type::From(*this).OnMethod_##name(BVMOp_##name(BVM_METHOD_PAR_PASS_TO_METHOD, MACRO_COMMA));
#define BVM_METHOD_HOST_AUTO(name) BVM_METHOD_HOST(name)  { return BVM_METHOD_AUTO_INVOKE(name); }

#define BVM_METHOD_VIA_HOST(name) BVM_METHOD(name) { return OnHost_##name(BVMOp_##name(BVM_METHOD_PAR_PASS_TO_HOST, MACRO_COMMA)); }

	BVM_METHOD(Memcpy)
	{
		OnHost_Memcpy(get_AddrW(pDst, size), get_AddrR(pSrc, size), size);
		return pDst;
	}
	BVM_METHOD_HOST(Memcpy)
	{
		DischargeMemOp(size);
		// prefer to use memmove
		return memmove(pDst, pSrc, size);
	}

	BVM_METHOD(Memset)
	{
		OnHost_Memset(get_AddrW(pDst, size), val, size);
		return pDst;
	}
	BVM_METHOD_HOST(Memset)
	{
		DischargeMemOp(size);
		return memset(pDst, val, size);
	}

	BVM_METHOD(Memcmp)
	{
		return OnHost_Memcmp(get_AddrR(p1, size), get_AddrR(p2, size), size);
	}
	BVM_METHOD_HOST(Memcmp)
	{
		DischargeMemOp(size);
		return static_cast<int32_t>(memcmp(p1, p2, size));
	}

	BVM_METHOD(Memis0)
	{
		return OnHost_Memis0(get_AddrR(p, size), size);
	}
	BVM_METHOD_HOST(Memis0)
	{
		DischargeMemOp(size);
		bool bRes = memis0(p, size);
		return !!bRes;
	}

	const char* Processor::RealizeStr(Wasm::Word sz, uint32_t& nLenOut)
	{
		Wasm::CheckpointTxt cp("string/realize");

		uint32_t n;
		auto sz_ = reinterpret_cast<const char*>(get_AddrExVar(sz, n, false));

		auto* p = reinterpret_cast<const char*>(memchr(sz_, 0, n));
		Wasm::Test(p);
		nLenOut = static_cast<uint32_t>(p - sz_);

		DischargeMemOp(nLenOut + 1);

		return sz_;
	}

	const char* Processor::RealizeStr(Wasm::Word sz)
	{
		uint32_t nLenOut;
		return RealizeStr(sz, nLenOut);
	}

	BVM_METHOD(Strlen)
	{
		uint32_t n;
		RealizeStr(sz, n);
		return n;
	}
	BVM_METHOD_HOST(Strlen)
	{
		return static_cast<uint32_t>(strlen(sz));
	}

	BVM_METHOD_VIA_HOST(Strcmp)
	BVM_METHOD_HOST(Strcmp)
	{
		return static_cast<int32_t>(strcmp(sz1, sz2));
	}

	BVM_METHOD(StackAlloc)
	{
		m_Stack.AliasAlloc(size);
		return m_Stack.get_AlasSp();
	}
	BVM_METHOD_HOST(StackAlloc)
	{
		m_Stack.AliasAlloc(size);
		return m_Stack.get_AliasPtr();
	}

	BVM_METHOD(StackFree)
	{
		m_Stack.AliasFree(size);
	}
	BVM_METHOD_HOST_AUTO(StackFree)

	bool Processor::HeapAllocEx(uint32_t& res, uint32_t size)
	{
		DischargeUnits(Limits::Cost::HeapOp);
		DischargeMemOp(size);

		if (m_Heap.Alloc(res, size))
			return true;

		uint32_t nSizeOld = m_LinearMem.n;
		uint32_t nReserve = m_Heap.get_UnusedAtEnd(nSizeOld);
		assert(nReserve < size);
		uint32_t nSizeNew = nSizeOld + size - nReserve;

		uint32_t nHeapMax = get_HeapLimit();
		if (nHeapMax < nSizeNew)
			return false;

		// grow
		std::setmax(nSizeNew, 0x1000U); // 4K 
		std::setmax(nSizeNew, static_cast<uint32_t>(m_vHeap.size()) * 2);
		std::setmin(nSizeNew, nHeapMax);

		HeapReserveStrict(nSizeNew);

		return m_Heap.Alloc(res, size);
	}

	void Processor::HeapReserveStrict(uint32_t n)
	{
		uint32_t nOld = m_LinearMem.n;
		assert(nOld < n);

		m_vHeap.resize(n, 0); // zero-init new mem

		m_LinearMem.p = &m_vHeap.front();
		m_LinearMem.n = n;
		m_Heap.OnGrow(nOld, n);
	}

	void Processor::HeapFreeEx(uint32_t res)
	{
		DischargeUnits(Limits::Cost::HeapOp);
		m_Heap.Free(res);
	}

	BVM_METHOD(Heap_Alloc)
	{
		size = Stack::AlignUp(size); // use the same alignment for heap too

		uint32_t val;
		if (!HeapAllocEx(val, size))
			return 0;

		return Wasm::MemoryType::Global | val;
	}
	BVM_METHOD_HOST(Heap_Alloc)
	{
		uint32_t val;
		if (!HeapAllocEx(val, size))
			return nullptr;

		return reinterpret_cast<uint8_t*>(Cast::NotConst(m_LinearMem.p)) + val;
	}

	BVM_METHOD(Heap_Free)
	{
		HeapFreeEx(pPtr ^ Wasm::MemoryType::Global);
	}
	BVM_METHOD_HOST(Heap_Free)
	{
		auto val =
			reinterpret_cast<const uint8_t*>(pPtr) -
			reinterpret_cast<const uint8_t*>(m_LinearMem.p);

		HeapFreeEx(static_cast<uint32_t>(val));
	}

	bool Processor::Heap::Alloc(uint32_t& retVal, uint32_t size)
	{
		auto it = m_mapSize.lower_bound(size, Entry::Size::Comparator());
		if (m_mapSize.end() == it)
			return false;

		auto& e = it->get_ParentObj();
		assert(e.m_Size.m_Key >= size);
		retVal = e.m_Pos.m_Key;

		if (e.m_Size.m_Key == size)
		{
			Remove(e, true);
			Insert(e, false);
		}
		else
		{
			// partition
			Create(e.m_Pos.m_Key, size, false);

			e.m_Pos.m_Key += size; // no need to remove and re-insert, order should not change
			UpdateSizeFree(e, e.m_Size.m_Key - size);
		}

		return true;
	}

	Processor::Heap::Entry* Processor::Heap::Create(uint32_t nPos, uint32_t nSize, bool bFree)
	{
		auto* p = new Heap::Entry;
		p->m_Pos.m_Key = nPos;
		p->m_Size.m_Key = nSize;
		Insert(*p, bFree);
		return p;
	}

	void Processor::Heap::Free(uint32_t ptr)
	{
		auto it = m_mapAllocated.find(ptr, Entry::Pos::Comparator());
		Wasm::Test(m_mapAllocated.end() != it);

		auto& e = it->get_ParentObj();
		Remove(e, false);
		Insert(e, true);

		TryMerge(e);

		it = MapPos::s_iterator_to(e.m_Pos);
		if (m_mapFree.begin() != it)
		{
			--it;
			TryMerge(it->get_ParentObj());
		}
	}

	void Processor::Heap::Test(uint32_t ptr, uint32_t size)
	{
		auto it = m_mapAllocated.upper_bound(ptr, Entry::Pos::Comparator());
		Wasm::Test(m_mapAllocated.begin() != it);
		--it;

		auto& e = it->get_ParentObj();
		assert(ptr >= e.m_Pos.m_Key);
		
		ptr += size;
		Wasm::Test((ptr >= size) && (ptr <= e.m_Pos.m_Key + e.m_Size.m_Key));
	}

	void Processor::Heap::UpdateSizeFree(Entry& e, uint32_t newVal)
	{
		m_mapSize.erase(MapSize::s_iterator_to(e.m_Size));
		e.m_Size.m_Key = newVal;
		m_mapSize.insert(e.m_Size);
	}

	void Processor::Heap::TryMerge(Entry& e)
	{
		auto it = m_mapFree.find(e.m_Pos.m_Key + e.m_Size.m_Key, Entry::Pos::Comparator());
		if (m_mapFree.end() != it)
		{
			auto& e2 = it->get_ParentObj();
			UpdateSizeFree(e, e.m_Size.m_Key + e2.m_Size.m_Key);
			Delete(e2, true);
		}
	}

	void Processor::Heap::Insert(Entry& e, bool bFree)
	{
		if (bFree)
		{
			m_mapFree.insert(e.m_Pos);
			m_mapSize.insert(e.m_Size);
		}
		else
			m_mapAllocated.insert(e.m_Pos);
	}

	void Processor::Heap::Remove(Entry& e, bool bFree)
	{
		if (bFree)
		{
			m_mapFree.erase(MapPos::s_iterator_to(e.m_Pos));
			m_mapSize.erase(MapSize::s_iterator_to(e.m_Size));
		}
		else
			m_mapAllocated.erase(MapPos::s_iterator_to(e.m_Pos));
	}

	void Processor::Heap::Delete(Entry& e, bool bFree)
	{
		Remove(e, bFree);
		delete &e;
	}

	void Processor::Heap::Clear()
	{
		while (!m_mapFree.empty())
			Delete(m_mapFree.begin()->get_ParentObj(), true);

		while (!m_mapAllocated.empty())
			Delete(m_mapAllocated.begin()->get_ParentObj(), false);
	}

	uint32_t Processor::Heap::get_UnusedAtEnd(uint32_t nEnd) const
	{
		if (!m_mapFree.empty())
		{
			auto& e = m_mapFree.rbegin()->get_ParentObj();
			if (e.m_Pos.m_Key + e.m_Size.m_Key == nEnd)
				return e.m_Size.m_Key;
		}
		return 0;
	}

	void Processor::Heap::OnGrow(uint32_t nOld, uint32_t nNew)
	{
		assert(nOld < nNew);

		if (!m_mapFree.empty())
		{
			auto& e = m_mapFree.rbegin()->get_ParentObj();
			assert(e.m_Pos.m_Key + e.m_Size.m_Key <= nOld);

			if (e.m_Pos.m_Key + e.m_Size.m_Key == nOld)
			{
				UpdateSizeFree(e, nNew - e.m_Pos.m_Key);
				return;
			}
		}

		Create(nOld, nNew - nOld, true);
	}

	void ProcessorContract::SetVarKeyFromShader(VarKey& vk, uint8_t nTag, const Blob& key, bool bW)
	{
		Wasm::Test(key.n <= Limits::VarKeySize);
		SetVarKey(vk, nTag, key);

		if (bW)
		{
			switch (nTag)
			{
			case VarKey::Tag::Internal:
			case VarKey::Tag::InternalStealth:
				break; // ok

			default:
				Wasm::Fail(); // not allowed to modify/emit
			}
		}
	}

	BVM_METHOD(LoadVar)
	{
		uint32_t ret = OnHost_LoadVar(get_AddrR(pKey, nKey), nKey, get_AddrW(pVal, nVal), nVal, nType);

		DischargeUnits(Limits::Cost::LoadVar + Limits::Cost::LoadVarPerByte * std::min(nVal, ret));

		return ret;
	}
	BVM_METHOD_HOST(LoadVar)
	{
		VarKey vk;
		SetVarKeyFromShader(vk, nType, Blob(pKey, nKey), false);

		LoadVar(vk, static_cast<uint8_t*>(pVal), nVal);
		return nVal;
	}

	BVM_METHOD(SaveVar)
	{
		DischargeUnits(Limits::Cost::SaveVar + Limits::Cost::SaveVarPerByte * nVal);
		return OnHost_SaveVar(get_AddrR(pKey, nKey), nKey, get_AddrR(pVal, nVal), nVal, nType);
	}
	BVM_METHOD_HOST(SaveVar)
	{
		Wasm::Test(nVal <= Limits::VarSize);

		VarKey vk;
		SetVarKeyFromShader(vk, nType, Blob(pKey, nKey), true);

		return SaveVar(vk, static_cast<const uint8_t*>(pVal), nVal);
	}

	BVM_METHOD(EmitLog)
	{
		DischargeUnits(Limits::Cost::Log + Limits::Cost::LogPerByte * nVal);
		return OnHost_EmitLog(get_AddrR(pKey, nKey), nKey, get_AddrR(pVal, nVal), nVal, nType);
	}
	BVM_METHOD_HOST(EmitLog)
	{
		Wasm::Test(nVal <= Limits::VarSize);

		VarKey vk;
		SetVarKeyFromShader(vk, nType, Blob(pKey, nKey), true);

		return OnLog(vk, Blob(pVal, nVal));
	}

	BVM_METHOD(CallFar)
	{
		// make sure the pArgs is not malicious.
		// Attacker may try to cause the target shader to overwrite its future stack or heap operands, or const data in its data section.
		//
		// Note: using 'Global' (heap) is not allowed either, since there's no reliable and simple way to verify it.
		// The attacker may pass a global pointer which is assumed to point to arguments, but partially belongs to unallocated heap, which may be allocated later by the callee.

		auto nCalleeStackMax = m_Stack.m_BytesCurrent;

		if (!bInheritContext)
		{
			Wasm::Test(iMethod >= 2); // c'tor and d'tor calls are not allowed

			if (nArgs)
			{
				// Only stack pointer is allowed
				Wasm::Test((Wasm::MemoryType::Mask & pArgs) == Wasm::MemoryType::Stack);

				// Make sure that the pointer is valid (i.e. between current and max stack pointers)
				// The caller is allowed to specify greater size (though it's not recommended), we will truncate it
				uint32_t nSize;
				get_AddrExVar(pArgs, nSize, false); // ensures it's a valid alias stack pointer.

				// Note: the above does NOT ensure it's not below current stack pointer, it has less strict validation criteria (see comments in its implementation for more info).
				// Hence - the following is necessary.
				Wasm::Test(pArgs >= m_Stack.get_AlasSp());

				nCalleeStackMax = (pArgs & ~Wasm::MemoryType::Stack) + std::min(nArgs, nSize); // restrict callee from accessing anything beyond
			}
		}

		CallFar(get_AddrAsR<ContractID>(cid), iMethod, pArgs);

		if (bInheritContext)
		{
			auto it = m_FarCalls.m_Stack.rbegin();
			auto& fr0 = *it;
			auto& fr1 = *(++it);
			fr0.m_Cid = fr1.m_Cid;
		}
		else
			m_Stack.m_BytesMax = nCalleeStackMax;
	}
	BVM_METHOD_HOST(CallFar)
	{
		Wasm::Fail(); // not supported in this form. Proper host code would invoke CallFar_T
	}

	BVM_METHOD(get_CallDepth)
	{
		return OnHost_get_CallDepth();
	}

	BVM_METHOD_HOST(get_CallDepth)
	{
		return static_cast<uint32_t>(m_FarCalls.m_Stack.size());
	}

	BVM_METHOD(get_CallerCid)
	{
		return OnHost_get_CallerCid(iCaller, get_AddrAsW<HashValue>(cid));
	}

	BVM_METHOD_HOST(get_CallerCid)
	{
		for (auto it = m_FarCalls.m_Stack.rbegin(); ; it++)
		{
			Wasm::Test(m_FarCalls.m_Stack.rend() != it);
			if (!iCaller--)
			{
				cid = it->m_Cid;
				break;
			}
		}
	}

	BVM_METHOD(Halt)
	{
		struct MyCheckpoint :public Wasm::Checkpoint
		{
			void Dump(std::ostream& os) override {
				os << "Explicit Halt";
			}

			uint32_t get_Type() override {
				return ErrorSubType::Internal;
			}

		} cp;

		Wasm::Fail();
	}
	BVM_METHOD_HOST_AUTO(Halt)

	BVM_METHOD(AddSig)
	{
		DischargeUnits(Limits::Cost::AddSig);
		return OnHost_AddSig(get_AddrAsR<ECC::Point>(pubKey));
	}
	BVM_METHOD_HOST(AddSig)
	{
		if (m_pSigValidate)
			AddSigInternal(pubKey);
	}

	BVM_METHOD(FundsLock)
	{
		HandleAmount(amount, aid, true);
	}
	BVM_METHOD_HOST_AUTO(FundsLock)

	BVM_METHOD(FundsUnlock)
	{
		HandleAmount(amount, aid, false);
	}
	BVM_METHOD_HOST_AUTO(FundsUnlock)

	BVM_METHOD(RefAdd)
	{
		DischargeUnits(Limits::Cost::SaveVar + Limits::Cost::LoadVar);
		return OnHost_RefAdd(get_AddrAsR<ContractID>(cid));
	}
	BVM_METHOD_HOST(RefAdd)
	{
		return ProcessorPlus_Contract::From(*this).HandleRef(cid, true);
	}

	BVM_METHOD(RefRelease)
	{
		DischargeUnits(Limits::Cost::SaveVar + Limits::Cost::LoadVar);
		return OnHost_RefRelease(get_AddrAsR<ContractID>(cid));
	}
	BVM_METHOD_HOST(RefRelease)
	{
		return ProcessorPlus_Contract::From(*this).HandleRef(cid, false);
	}

	BVM_METHOD(AssetCreate)
	{
		return OnHost_AssetCreate(get_AddrR(pMeta, nMeta), nMeta);
	}
	BVM_METHOD_HOST(AssetCreate)
	{
		Wasm::Test(nMeta && (nMeta <= Asset::Info::s_MetadataMaxSize));
		DischargeUnits(Limits::Cost::AssetManage);

		Asset::Metadata md;
		Blob(pMeta, nMeta).Export(md.m_Value);
		md.UpdateHash();

		AssetVar av;
		get_AssetOwner(av.m_Owner, m_FarCalls.m_Stack.back().m_Cid, md);

		Asset::ID ret = AssetCreate(md, av.m_Owner);
		if (ret)
		{
			ProcessorPlus_Contract::From(*this).HandleAmountOuter(Rules::get().CA.DepositForList, Zero, true);

			SetAssetKey(av, ret);
			SaveVar(av.m_vk, av.m_Owner.m_pData, av.m_Owner.nBytes);
		}

		return ret;
	}

	void ProcessorContract::SetAssetKey(AssetVar& av, Asset::ID aid)
	{
		SetVarKey(av.m_vk);
		av.m_vk.Append(VarKey::Tag::OwnedAsset, uintBigFrom(aid));
	}

	void ProcessorContract::get_AssetStrict(AssetVar& av, Asset::ID aid)
	{
		SetAssetKey(av, aid);

		uint32_t n = av.m_Owner.nBytes;
		LoadVar(av.m_vk, av.m_Owner.m_pData, n);
		Wasm::Test(av.m_Owner.nBytes == n);
	}

	BVM_METHOD(AssetEmit)
	{
		DischargeUnits(Limits::Cost::AssetEmit);

		AssetVar av;
		get_AssetStrict(av, aid);

		AmountSigned valS(amount);
		Wasm::Test(valS >= 0);

		if (!bEmit)
		{
			valS = -valS;
			Wasm::Test(valS <= 0);
		}

		bool b = AssetEmit(aid, av.m_Owner, valS);
		if (b)
			HandleAmountInner(amount, aid, bEmit);

		return !!b;
	}
	BVM_METHOD_HOST_AUTO(AssetEmit)

	BVM_METHOD(AssetDestroy)
	{
		DischargeUnits(Limits::Cost::AssetManage);

		AssetVar av;
		get_AssetStrict(av, aid);

		bool b = AssetDestroy(aid, av.m_Owner);
		if (b)
		{
			HandleAmountOuter(Rules::get().CA.DepositForList, Zero, false);
			SaveVar(av.m_vk, nullptr, 0);
		}

		return !!b;
	}
	BVM_METHOD_HOST_AUTO(AssetDestroy)

	BVM_METHOD(get_Height)
	{
		return get_Height();
	}
	BVM_METHOD_HOST_AUTO(get_Height)

	BVM_METHOD(get_HdrInfo)
	{
		DischargeUnits(Limits::Cost::LoadVar);
		auto& hdr_ = get_AddrAsW<BlockHeader::Info>(hdr); // currently ignore alignment
		hdr_.m_Height = Wasm::from_wasm(hdr_.m_Height);
		OnHost_get_HdrInfo(hdr_);
		hdr_.Convert<true>();
	}
	BVM_METHOD_HOST(get_HdrInfo)
	{
		Block::SystemState::Full s;
		s.m_Height = hdr.m_Height;
		Wasm::Test(get_HdrAt(s));

		CvtHdr(hdr, s);
		s.get_Hash(hdr.m_Hash);
	}

	BVM_METHOD(get_HdrFull)
	{
		DischargeUnits(Limits::Cost::LoadVar);
		auto& hdr_ = get_AddrAsW<BlockHeader::Full>(hdr); // currently ignore alignment
		hdr_.m_Height = Wasm::from_wasm(hdr_.m_Height);
		OnHost_get_HdrFull(hdr_);
		hdr_.Convert<true>();
	}
	BVM_METHOD_HOST(get_HdrFull)
	{
		Block::SystemState::Full s;
		s.m_Height = hdr.m_Height;
		Wasm::Test(get_HdrAt(s));

		CvtHdr(hdr, s);
		hdr.m_Prev = s.m_Prev;
		hdr.m_ChainWork = s.m_ChainWork;

		Cast::Reinterpret<Block::PoW>(hdr.m_PoW) = s.m_PoW;
	}

	void Processor::CvtHdr(Shaders::BlockHeader::InfoBase& hdr, const Block::SystemState::Full& s)
	{
		hdr.m_Timestamp = s.m_TimeStamp;
		hdr.m_Kernels = s.m_Kernels;
		hdr.m_Definition = s.m_Definition;
	}

	BVM_METHOD(get_RulesCfg)
	{
		auto& res_ = get_AddrAsW<Shaders::HashValue>(res); // currently ignore alignment
		return OnHost_get_RulesCfg(h, res_);
	}
	BVM_METHOD_HOST(get_RulesCfg)
	{
		const auto& r = Rules::get();
		auto iFork = r.FindFork(h);
		res = r.pForks[iFork].m_Hash;
		return r.pForks[iFork].m_Height;
	}

	struct Processor::DataProcessor::Instance
	{
		template <uint32_t nBytes>
		struct FixedResSize
			:public Base
		{
			typedef uintBig_t<nBytes> uintBig;

			virtual void Read(uintBig&) = 0;

			virtual uint32_t Read(uint8_t* p, uint32_t n) override
			{
				if (n >= nBytes)
				{
					Read(*reinterpret_cast<uintBig*>(p));
					return nBytes;
				}

				uintBig hv;
				Read(hv);

				memcpy(p, hv.m_pData, n);
				return n;
			}
		};

		struct Sha256
			:public FixedResSize<ECC::Hash::Value::nBytes>
		{
			ECC::Hash::Processor m_Hp;

			virtual ~Sha256() {}
			virtual void Write(const uint8_t* p, uint32_t n) override
			{
				m_Hp << Blob(p, n);
			}
			virtual void Read(uintBig& hv) override
			{
				ECC::Hash::Processor(m_Hp) >> hv;
			}
		};

		struct Blake2b
			:public Base
		{
			bvm2::Impl::HashProcessor::Blake2b_Base m_B2b;

			virtual ~Blake2b() {}
			virtual void Write(const uint8_t* p, uint32_t n) override
			{
				m_B2b.Write(p, n);
			}
			virtual uint32_t Read(uint8_t* p, uint32_t n) override
			{
				auto s = m_B2b; // copy
				return s.Read(p, n);
			}
		};

		template <uint32_t nBits_>
		struct KeccakProcessor
		{
			static const uint32_t nBits = nBits_;
			static const uint32_t nBytes = nBits / 8;

			static const uint32_t nSizeWord = sizeof(uint64_t);
			static const uint32_t nSizeBlock = (1600 - nBits * 2) / 8;
			static const uint32_t nWordsBlock = nSizeBlock / nSizeWord;

			KeccakProcessor()
			{
				ZeroObject(*this);
			}

			void Write(const uint8_t* pSrc, uint32_t nSrc)
			{
				if (m_LastWordBytes)
				{
					auto* pDst = m_pLastWordAsBytes + m_LastWordBytes;

					if (m_LastWordBytes + nSrc < nSizeWord)
					{
						memcpy(pDst, pSrc, nSrc);
						m_LastWordBytes += nSrc;
						return;
					}

					uint32_t nPortion = nSizeWord - m_LastWordBytes;

					memcpy(pDst, pSrc, nPortion);
					pSrc += nPortion;
					nSrc -= nPortion;

					m_LastWordBytes = 0;
					AddLastWord();
				}

				while (true)
				{
					if (nSrc < nSizeWord)
					{
						memcpy(m_pLastWordAsBytes, pSrc, nSrc);
						m_LastWordBytes = nSrc;
						return;
					}

					memcpy(m_pLastWordAsBytes, pSrc, nSizeWord);
					pSrc += nSizeWord;
					nSrc -= nSizeWord;

					AddLastWord();
				}
			}

			void Read(uint8_t* pRes)
			{
				// pad and transform
				m_pLastWordAsBytes[m_LastWordBytes++] = 0x01;
				memset0(m_pLastWordAsBytes + m_LastWordBytes, nSizeWord - m_LastWordBytes);

				AddLastWordRaw();

				m_pState[nWordsBlock - 1] ^= 0x8000000000000000;

				ethash_keccakf1600(m_pState);

				for (uint32_t i = 0; i < (nBytes / nSizeWord); ++i)
					reinterpret_cast<uint64_t*>(pRes)[i] = ByteOrder::from_le(m_pState[i]);
			}

		private:

			uint64_t m_pState[25];

			union {
				uint64_t m_LastWord;
				uint8_t m_pLastWordAsBytes[nSizeWord];
			};

			uint32_t m_iWord;
			uint32_t m_LastWordBytes;


			void AddLastWordRaw()
			{
				assert(m_iWord < nWordsBlock);
				m_pState[m_iWord] ^= ByteOrder::to_le(m_LastWord);
			}

			void AddLastWord()
			{
				AddLastWordRaw();

				if (++m_iWord == nWordsBlock)
				{
					ethash_keccakf1600(m_pState);
					m_iWord = 0;
				}
			}
		};


		template <uint32_t nBits>
		struct Keccak
			:public FixedResSize<nBits / 8>
		{
			KeccakProcessor<nBits> m_State;

			virtual void Write(const uint8_t* pSrc, uint32_t nSrc) override
			{
				m_State.Write(pSrc, nSrc);
			}

			virtual void Read(uintBig_t<nBits/8>& hv) override
			{
				m_State.Read(hv.m_pData);
			}
		};

	};

	Processor::DataProcessor::Base& Processor::DataProcessor::FindStrict(uint32_t key)
	{
		auto it = m_Map.find(key, Base::Comparator());
		Wasm::Test(m_Map.end() != it);
		return *it;
	}

	Processor::DataProcessor::Base& Processor::DataProcessor::FindStrict(HashObj* pObj)
	{
		return FindStrict(static_cast<uint32_t>(reinterpret_cast<size_t>(pObj)));
	}

	uint32_t Processor::AddHash(std::unique_ptr<DataProcessor::Base>&& p)
	{
		if ((Kind::Contract == get_Kind()) && (m_DataProcessor.m_Map.size() >= Limits::HashObjects))
			return 0;

		DischargeUnits(Limits::Cost::HashOp);

		uint32_t ret = m_DataProcessor.m_Map.empty() ? 1 : (m_DataProcessor.m_Map.rbegin()->m_Key + 1);
		p->m_Key = ret;
		m_DataProcessor.m_Map.insert(*p.release());

		return ret;
	}

	BVM_METHOD(HashCreateSha256)
	{
		auto pRet = std::make_unique<DataProcessor::Instance::Sha256>();
		return AddHash(std::move(pRet));
	}

	BVM_METHOD_HOST(HashCreateSha256)
	{
		auto val = ProcessorPlus::From(*this).OnMethod_HashCreateSha256();
		return val ? reinterpret_cast<HashObj*>(static_cast<size_t>(val)) : nullptr;
	}

	BVM_METHOD(HashCreateBlake2b)
	{
		auto val = ProcessorPlus::From(*this).OnHost_HashCreateBlake2b(get_AddrR(pPersonal, nPersonal), nPersonal, nResultSize);
		return val ? static_cast<uint32_t>(reinterpret_cast<size_t>(val)) : 0;
	}

	BVM_METHOD_HOST(HashCreateBlake2b)
	{
		auto pRet = std::make_unique<DataProcessor::Instance::Blake2b>();
		if (!pRet->m_B2b.Init(pPersonal, nPersonal, nResultSize))
			return nullptr;

		auto val = AddHash(std::move(pRet));
		return val ? reinterpret_cast<HashObj*>(static_cast<size_t>(val)) : nullptr;
	}

	BVM_METHOD(HashCreateKeccak)
	{
		switch (nBits)
		{
		case 256:
			return AddHash(std::make_unique<DataProcessor::Instance::Keccak<256> >());
		case 384:
			return AddHash(std::make_unique<DataProcessor::Instance::Keccak<384> >());
		case 512:
			return AddHash(std::make_unique<DataProcessor::Instance::Keccak<512> >());
		}

		return 0;
	}

	BVM_METHOD_HOST(HashCreateKeccak)
	{
		auto val = ProcessorPlus::From(*this).OnMethod_HashCreateKeccak(nBits);
		return val ? reinterpret_cast<HashObj*>(static_cast<size_t>(val)) : nullptr;
	}

	BVM_METHOD(HashWrite)
	{
		DischargeUnits(Limits::Cost::HashOpPerByte * size);

		m_DataProcessor.FindStrict(pHash).Write(get_AddrR(p, size), size);
	}

	BVM_METHOD_HOST(HashWrite)
	{
		m_DataProcessor.FindStrict(pHash).Write(reinterpret_cast<const uint8_t*>(p), size);
	}

	BVM_METHOD(HashGetValue)
	{
		DischargeUnits(Limits::Cost::HashOp + Limits::Cost::HashOpPerByte * size);

		uint8_t* pDst_ = get_AddrW(pDst, size);
		uint32_t n = m_DataProcessor.FindStrict(pHash).Read(pDst_, size);
		memset0(pDst_ + n, size - n);
	}

	BVM_METHOD_HOST(HashGetValue)
	{
		uint8_t* pDst_ = reinterpret_cast<uint8_t*>(pDst);
		uint32_t n = m_DataProcessor.FindStrict(pHash).Read(pDst_, size);
		memset0(pDst_ + n, size - n);
	}

	BVM_METHOD(HashFree)
	{
		m_DataProcessor.m_Map.Delete(m_DataProcessor.FindStrict(pHash));
	}

	BVM_METHOD_HOST(HashFree)
	{
		m_DataProcessor.m_Map.Delete(m_DataProcessor.FindStrict(pHash));
	}

	/////////////////////////////////////////////
	// Secp
	uint32_t Processor::Secp::Scalar::From(const Secp_scalar& s)
	{
		return static_cast<uint32_t>(reinterpret_cast<size_t>(&s));
	}

	template<> Wasm::Word ProcessorPlus::ToMethod<Wasm::Word, const Secp_scalar&>(const Secp_scalar& s) {
		return Secp::Scalar::From(s);
	}
	template<> Wasm::Word ProcessorPlus::ToMethod<Wasm::Word, Secp_scalar&>(Secp_scalar& s) {
		return Secp::Scalar::From(s);
	}

	Processor::Secp::Scalar::Item& Processor::Secp::Scalar::FindStrict(uint32_t key)
	{
		auto it = m_Map.find(key, Item::Comparator());
		Wasm::Test(m_Map.end() != it);
		return *it;
	}

	BVM_METHOD(Secp_Scalar_alloc)
	{
		auto& x = m_Secp.m_Scalar.m_Map;

		if ((Kind::Contract == get_Kind()) && (x.size() >= Limits::SecScalars))
			return 0;

		uint32_t ret = x.empty() ? 1 : (x.rbegin()->m_Key + 1);
		x.Create(ret);
		return ret;
	}

	BVM_METHOD_HOST(Secp_Scalar_alloc)
	{
		uint32_t val = ProcessorPlus::From(*this).OnMethod_Secp_Scalar_alloc();
		return reinterpret_cast<Secp_scalar*>((size_t) val);
	}

	BVM_METHOD(Secp_Scalar_free)
	{
		m_Secp.m_Scalar.m_Map.Delete(m_Secp.m_Scalar.FindStrict(s));
	}
	BVM_METHOD_HOST_AUTO(Secp_Scalar_free)

	BVM_METHOD(Secp_Scalar_import)
	{
		return !m_Secp.m_Scalar.FindStrict(s).m_Val.Import(get_AddrAsR<ECC::Scalar>(data));
	}

	BVM_METHOD_HOST(Secp_Scalar_import)
	{
		return !m_Secp.m_Scalar.FindStrict(Secp::Scalar::From(s)).m_Val.Import(data);
	}

	BVM_METHOD(Secp_Scalar_export)
	{
		return m_Secp.m_Scalar.FindStrict(s).m_Val.Export(get_AddrAsW<ECC::Scalar>(data));
	}

	BVM_METHOD_HOST(Secp_Scalar_export)
	{
		return m_Secp.m_Scalar.FindStrict(Secp::Scalar::From(s)).m_Val.Export(data);
	}

	BVM_METHOD(Secp_Scalar_neg)
	{
		m_Secp.m_Scalar.FindStrict(dst).m_Val = -m_Secp.m_Scalar.FindStrict(src).m_Val;
	}
	BVM_METHOD_HOST_AUTO(Secp_Scalar_neg)

	BVM_METHOD(Secp_Scalar_add)
	{
		m_Secp.m_Scalar.FindStrict(dst).m_Val =
			m_Secp.m_Scalar.FindStrict(a).m_Val +
			m_Secp.m_Scalar.FindStrict(b).m_Val;
	}
	BVM_METHOD_HOST_AUTO(Secp_Scalar_add)

	BVM_METHOD(Secp_Scalar_mul)
	{
		m_Secp.m_Scalar.FindStrict(dst).m_Val =
			m_Secp.m_Scalar.FindStrict(a).m_Val *
			m_Secp.m_Scalar.FindStrict(b).m_Val;
	}
	BVM_METHOD_HOST_AUTO(Secp_Scalar_mul)

	BVM_METHOD(Secp_Scalar_inv)
	{
		DischargeUnits(Limits::Cost::Secp_ScalarInv);
		m_Secp.m_Scalar.FindStrict(dst).m_Val.SetInv(m_Secp.m_Scalar.FindStrict(src).m_Val);
	}
	BVM_METHOD_HOST_AUTO(Secp_Scalar_inv)

	BVM_METHOD(Secp_Scalar_set)
	{
		m_Secp.m_Scalar.FindStrict(dst).m_Val = val;
	}
	BVM_METHOD_HOST_AUTO(Secp_Scalar_set)


	uint32_t Processor::Secp::Point::From(const Secp_point& p)
	{
		return static_cast<uint32_t>(reinterpret_cast<size_t>(&p));
	}

	template<> Wasm::Word ProcessorPlus::ToMethod<Wasm::Word, const Secp_point&>(const Secp_point& p) {
		return Secp::Point::From(p);
	}
	template<> Wasm::Word ProcessorPlus::ToMethod<Wasm::Word, Secp_point&>(Secp_point& p) {
		return Secp::Point::From(p);
	}

	Processor::Secp::Point::Item& Processor::Secp::Point::FindStrict(uint32_t key)
	{
		auto it = m_Map.find(key, Item::Comparator());
		Wasm::Test(m_Map.end() != it);
		return *it;
	}

	BVM_METHOD(Secp_Point_alloc)
	{
		auto& x = m_Secp.m_Point.m_Map;

		if ((Kind::Contract == get_Kind()) && (x.size() >= Limits::SecPoints))
			return 0;

		uint32_t ret = x.empty() ? 1 : (x.rbegin()->m_Key + 1);
		x.Create(ret);
		return ret;
	}

	BVM_METHOD_HOST(Secp_Point_alloc)
	{
		uint32_t val = ProcessorPlus::From(*this).OnMethod_Secp_Point_alloc();
		return reinterpret_cast<Secp_point*>((size_t) val);
	}

	BVM_METHOD(Secp_Point_free)
	{
		m_Secp.m_Point.m_Map.Delete(m_Secp.m_Point.FindStrict(p));
	}
	BVM_METHOD_HOST_AUTO(Secp_Point_free)

	BVM_METHOD(Secp_Point_Import)
	{
		DischargeUnits(Limits::Cost::Secp_Point_Import);
		return !!m_Secp.m_Point.FindStrict(p).m_Val.Import(get_AddrAsR<ECC::Point>(pk));
	}

	BVM_METHOD_HOST(Secp_Point_Import)
	{
		return !!m_Secp.m_Point.FindStrict(Secp::Point::From(p)).m_Val.Import(pk);
	}

	BVM_METHOD(Secp_Point_Export)
	{
		DischargeUnits(Limits::Cost::Secp_Point_Export);
		m_Secp.m_Point.FindStrict(p).m_Val.Export(get_AddrAsW<ECC::Point>(pk));
	}

	BVM_METHOD_HOST(Secp_Point_Export)
	{
		m_Secp.m_Point.FindStrict(Secp::Point::From(p)).m_Val.Export(pk);
	}

	BVM_METHOD(Secp_Point_neg)
	{
		m_Secp.m_Point.FindStrict(dst).m_Val = -m_Secp.m_Point.FindStrict(src).m_Val;
	}
	BVM_METHOD_HOST_AUTO(Secp_Point_neg)

	BVM_METHOD(Secp_Point_add)
	{
		m_Secp.m_Point.FindStrict(dst).m_Val =
			m_Secp.m_Point.FindStrict(a).m_Val +
			m_Secp.m_Point.FindStrict(b).m_Val;
	}
	BVM_METHOD_HOST_AUTO(Secp_Point_add)

	BVM_METHOD(Secp_Point_mul)
	{
		DischargeUnits(Limits::Cost::Secp_Point_Multiply);

		ECC::Mode::Scope mode(ECC::Mode::Fast);

		m_Secp.m_Point.FindStrict(dst).m_Val =
			m_Secp.m_Point.FindStrict(p).m_Val *
			m_Secp.m_Scalar.FindStrict(s).m_Val;
	}
	BVM_METHOD_HOST_AUTO(Secp_Point_mul)

	BVM_METHOD(Secp_Point_IsZero)
	{
		bool bRet = m_Secp.m_Point.FindStrict(p).m_Val == Zero;
		return !!bRet;
	}
	BVM_METHOD_HOST_AUTO(Secp_Point_IsZero)

	BVM_METHOD(Secp_Point_mul_G)
	{
		DischargeUnits(Limits::Cost::Secp_Point_Multiply);

		ECC::Mode::Scope mode(ECC::Mode::Fast);

		m_Secp.m_Point.FindStrict(dst).m_Val =
			ECC::Context::get().G *
			m_Secp.m_Scalar.FindStrict(s).m_Val;
	}
	BVM_METHOD_HOST_AUTO(Secp_Point_mul_G)

	BVM_METHOD(Secp_Point_mul_J)
	{
		DischargeUnits(Limits::Cost::Secp_Point_Multiply);

		ECC::Mode::Scope mode(ECC::Mode::Fast);

		m_Secp.m_Point.FindStrict(dst).m_Val =
			ECC::Context::get().J *
			m_Secp.m_Scalar.FindStrict(s).m_Val;
	}
	BVM_METHOD_HOST_AUTO(Secp_Point_mul_J)

	BVM_METHOD(Secp_Point_mul_H)
	{
		DischargeUnits(Limits::Cost::Secp_Point_Multiply);

		ECC::Mode::Scope mode(ECC::Mode::Fast);

		auto& dst_ = m_Secp.m_Point.FindStrict(dst).m_Val;
		auto& s_ = m_Secp.m_Scalar.FindStrict(s).m_Val;

		CoinID::Generator gen(aid);
		if (gen.m_hGen == Zero)
			dst_ = ECC::Context::get().H_Big * s_;
		else
			dst_ = gen.m_hGen * s_;
	}
	BVM_METHOD_HOST_AUTO(Secp_Point_mul_H)


	/////////////////////////////////////////////
	// other

	BVM_METHOD(VerifyBeamHashIII)
	{
		DischargeUnits(Limits::Cost::BeamHashIII);

		return OnHost_VerifyBeamHashIII(
			get_AddrR(pInp, nInp), nInp,
			get_AddrR(pNonce, nNonce), nNonce,
			get_AddrR(pSol, nSol), nSol);
	}
	BVM_METHOD_HOST(VerifyBeamHashIII)
	{
		return !!Impl::BeamHashIII::Verify(pInp, nInp, pNonce, nNonce, (const uint8_t*) pSol, nSol);
	}

	BVM_METHOD(get_EthMixHash)
	{
		DischargeUnits(Limits::Cost::BeamHashIII);
		return OnHost_get_EthMixHash(get_AddrAsW<HashValue>(hv), iEpoch, get_AddrAsR<HashValue512>(hvSeed));
	}
	BVM_METHOD_HOST(get_EthMixHash)
	{
		ByteBuffer buf;
		if (!LoadEthContext(buf, iEpoch))
			return 0;

#pragma pack (push, 1)

		struct Hdr
		{
			uint32_t m_ItemsLight_LE;
			uint32_t m_ItemsFull_LE;
		};

#pragma pack (pop)

		struct Guard {
			ethash_epoch_context* m_pVal = nullptr;
			~Guard()
			{
				if (m_pVal)
					ethash_destroy_epoch_context(m_pVal);
			}
		} g;

		ethash_epoch_context ctx = { 0 };

		if (buf.empty())
		{
			g.m_pVal = ethash_create_epoch_context(iEpoch);
			if (!g.m_pVal)
				Wasm::Fail("no mem");

			memcpy(reinterpret_cast<void*>(&ctx), g.m_pVal, sizeof(ctx));
		}
		else
		{
			Wasm::Test(buf.size() >= sizeof(Hdr));
			auto pHdr = reinterpret_cast<Hdr*>(&buf.front());

			ethash_epoch_context ctxInst2 = ethash_epoch_context{
				static_cast<int>(iEpoch),
				static_cast<int>(ByteOrder::from_le(pHdr->m_ItemsLight_LE)),
				reinterpret_cast<ethash_hash512*>(&buf.front() + sizeof(Hdr)),
				nullptr,
				static_cast<int>(ByteOrder::from_le(pHdr->m_ItemsFull_LE)) };

			memcpy(reinterpret_cast<void*>(&ctx), &ctxInst2, sizeof(ctx));
		}

		uint32_t nSizeBuf = sizeof(Hdr) + ctx.light_cache_num_items * sizeof(HashValue512);
		Wasm::Test(buf.empty() || (buf.size() == nSizeBuf));

		ethash_get_MixHash((ethash_hash256*) hv.m_pData, &ctx, (ethash_hash512*) hvSeed.m_pData);

		if (buf.empty())
		{
			static_assert(sizeof(ctx) >= sizeof(Hdr)); // the context is allocated as ctx + cache at once. It's safe to overwrite the preceeding portion by our header
			//assert(reinterpret_cast<const uint8_t*>(ctx.light_cache) == reinterpret_cast<const uint8_t*>(g.m_pVal + 1));

			Hdr hdr1;
			hdr1.m_ItemsFull_LE = ByteOrder::to_le(static_cast<uint32_t>(ctx.full_dataset_num_items));
			hdr1.m_ItemsLight_LE = ByteOrder::to_le(static_cast<uint32_t>(ctx.light_cache_num_items));

			Hdr& hdrRef = reinterpret_cast<Hdr*>(Cast::NotConst(ctx.light_cache))[-1];
			TemporarySwap<Hdr> tmpSwap(hdr1, hdrRef);

			Blob blob;
			blob.p = &hdrRef;
			blob.n = nSizeBuf;

			SaveEthContext(blob, iEpoch);
		}

		return 1;
	}


	//BVM_METHOD(LoadVarEx)
	//{
	//	return OnHost_LoadVarEx(nTag, get_AddrR(pKey, nKey), nKey, get_AddrW(pVal, nVal), nVal);
	//}
	//BVM_METHOD_HOST(LoadVarEx)
	//{
	//	Wasm::Test(m_pCid);
	//	Wasm::Test(nKey <= Limits::VarKeySize);

	//	VarKey vk;
	//	vk.Set(*m_pCid);
	//	vk.Append(nTag, Blob(pKey, nKey));

	//	LoadVar(vk, static_cast<uint8_t*>(pVal), nVal);
	//	return nVal;
	//}

	//BVM_METHOD(LoadAllVars)
	//{
	//	struct Marshaller
	//		:public ILoadVarCallback
	//	{
	//		ProcessorManager& m_This;
	//		Wasm::Word m_Object;
	//		Wasm::Word m_Method;
	//
	//		Marshaller(ProcessorManager& x) :m_This(x) {}
	//
	//		uint8_t OnVar(uint8_t nTag, const uint8_t* pKey, uint32_t nKey, const uint8_t* pVal, uint32_t nVal) override
	//		{
	//			Wasm::Word nAliasSp0 = m_This.m_Stack.m_BytesCurrent;
	//
	//			m_This.m_Stack.PushAlias(Blob(pKey, nKey));
	//			Wasm::Word pKey_ = m_This.m_Stack.get_AlasSp();
	//
	//			m_This.m_Stack.PushAlias(Blob(pVal, nVal));
	//			Wasm::Word pVal_ = m_This.m_Stack.get_AlasSp();
	//
	//			Wasm::Word nAliasSp1 = m_This.m_Stack.m_BytesCurrent;
	//			Wasm::Word nOperandSp = m_This.m_Stack.m_Pos;
	//
	//			m_This.m_Stack.Push(m_Object); // 'this' pointer
	//			m_This.m_Stack.Push(nTag);
	//			m_This.m_Stack.Push(pKey_);
	//			m_This.m_Stack.Push(nKey);
	//			m_This.m_Stack.Push(pVal_);
	//			m_This.m_Stack.Push(nVal);
	//
	//			m_This.Run(m_Method);
	//
	//			auto ret = m_This.m_Stack.Pop<Wasm::Word>();
	//
	//			Wasm::Test(nAliasSp1 == m_This.m_Stack.m_BytesCurrent); // alias stack must be restored
	//			Wasm::Test(nOperandSp == m_This.m_Stack.m_Pos); // operand stack must be restored
	//
	//			m_This.m_Stack.m_BytesCurrent = nAliasSp0;
	//
	//
	//			return !!ret;
	//		}
	//
	//	} m(*this);
	//
	//	m.m_Object = pCallback;
	//	m.m_Method = ReadVFunc(pCallback, 0);
	//
	//	return LoadAllVars(m);
	//}
	//
	//BVM_METHOD_HOST(LoadAllVars)
	//{
	//	Wasm::Test(pCallback);
	//	return LoadAllVars(*pCallback);
	//}

	BVM_METHOD(VarsEnum)
	{
		OnHost_VarsEnum(get_AddrR(pKey0, nKey0), nKey0, get_AddrR(pKey1, nKey1), nKey1);
	}
	BVM_METHOD_HOST(VarsEnum)
	{
		FreeAuxAllocGuarded();
		VarsEnum(Blob(pKey0, nKey0), Blob(pKey1, nKey1));
		m_EnumType = EnumType::Vars;
	}

	BVM_METHOD(VarsMoveNext)
	{
		auto ppKey_ = get_AddrW(ppKey, sizeof(Wasm::Word));
		auto pnKey_ = get_AddrW(pnKey, sizeof(Wasm::Word));
		auto ppVal_ = get_AddrW(ppVal, sizeof(Wasm::Word));
		auto pnVal_ = get_AddrW(pnVal, sizeof(Wasm::Word));

		const void *pKey, *pVal;
		uint32_t nKey, nVal;

		if (!OnHost_VarsMoveNext(&pKey, &nKey, &pVal, &nVal))
			return 0;

		uint8_t* pDst = ResizeAux(nKey + nVal);

		Wasm::to_wasm(pnKey_, nKey);
		Wasm::to_wasm(ppKey_, m_AuxAlloc.m_pPtr);
		memcpy(pDst, pKey, nKey);

		Wasm::to_wasm(pnVal_, nVal);
		Wasm::to_wasm(ppVal_, m_AuxAlloc.m_pPtr + nKey);
		memcpy(pDst + nKey, pVal, nVal);

		return 1;
	}
	BVM_METHOD_HOST(VarsMoveNext)
	{
		Wasm::Test(EnumType::Vars == m_EnumType); // illegal to call this method before VarsEnum

		Blob key, data;
		if (!VarsMoveNext(key, data))
		{
			FreeAuxAllocGuarded();
			m_EnumType = EnumType::None;
			return 0;
		}

		*ppKey = key.p;
		*pnKey = key.n;
		*ppVal = data.p;
		*pnVal = data.n;

		return 1;
	}

	const HeightPos* Processor::FromWasmOpt(Wasm::Word pPos, HeightPos& buf)
	{
		if (!pPos)
			return nullptr;

		auto& pos_ = get_AddrAsR<HeightPos>(pPos);
		buf.m_Height = Wasm::from_wasm(pos_.m_Height);
		buf.m_Pos = Wasm::from_wasm(pos_.m_Pos);

		return &buf;
	}

	BVM_METHOD(LogsEnum)
	{
		HeightPos posMin, posMax;
		OnHost_LogsEnum(get_AddrR(pKey0, nKey0), nKey0, get_AddrR(pKey1, nKey1), nKey1, FromWasmOpt(pPosMin, posMin), FromWasmOpt(pPosMax, posMax));
	}
	BVM_METHOD_HOST(LogsEnum)
	{
		FreeAuxAllocGuarded();
		LogsEnum(Blob(pKey0, nKey0), Blob(pKey1, nKey1), pPosMin, pPosMax);
		m_EnumType = EnumType::Logs;
	}

	BVM_METHOD(LogsMoveNext)
	{
		auto ppKey_ = get_AddrW(ppKey, sizeof(Wasm::Word));
		auto pnKey_ = get_AddrW(pnKey, sizeof(Wasm::Word));
		auto ppVal_ = get_AddrW(ppVal, sizeof(Wasm::Word));
		auto pnVal_ = get_AddrW(pnVal, sizeof(Wasm::Word));
		auto& pos_ = get_AddrAsW<HeightPos>(pPos);

		const void *pKey, *pVal;
		uint32_t nKey, nVal;

		if (!OnHost_LogsMoveNext(&pKey, &nKey, &pVal, &nVal, &pos_))
			return 0;

		uint8_t* pDst = ResizeAux(nKey + nVal);

		Wasm::to_wasm(pnKey_, nKey);
		Wasm::to_wasm(ppKey_, m_AuxAlloc.m_pPtr);
		memcpy(pDst, pKey, nKey);

		Wasm::to_wasm(pnVal_, nVal);
		Wasm::to_wasm(ppVal_, m_AuxAlloc.m_pPtr + nKey);
		memcpy(pDst + nKey, pVal, nVal);

		pos_.m_Height = Wasm::to_wasm(pos_.m_Height);
		pos_.m_Pos = Wasm::to_wasm(pos_.m_Pos);

		return 1;
	}
	BVM_METHOD_HOST(LogsMoveNext)
	{
		Wasm::Test(EnumType::Logs == m_EnumType); // illegal to call this method before LogsEnum

		Blob key, val;
		if (!LogsMoveNext(key, val, *pPos))
		{
			FreeAuxAllocGuarded();
			m_EnumType = EnumType::None;
			return 0;
		}

		*ppKey = key.p;
		*pnKey = key.n;
		*ppVal = val.p;
		*pnVal = val.n;

		return 1;
	}

	uint32_t ProcessorManager::VarGetProofInternal(const void* pKey, uint32_t nKey, Wasm::Word& pVal, Wasm::Word& nVal, Wasm::Word& pProof)
	{
		ByteBuffer val;
		beam::Merkle::Proof proof;

		Blob key(pKey, nKey);
		if (!VarGetProof(key, val, proof))
		{
			FreeAuxAllocGuarded();
			return 0;
		}

		uint32_t nSizeVal = static_cast<uint32_t>(val.size());
		uint32_t nProof = static_cast<uint32_t>(proof.size());
		uint32_t nSizeProof = nProof * static_cast<uint32_t>(sizeof(proof[0]));

		uint8_t* pDst = ResizeAux(nSizeVal + nSizeProof);

		nVal = nSizeVal;
		pVal = m_AuxAlloc.m_pPtr;
		if (nSizeVal)
			memcpy(pDst, &val.front(), nSizeVal);

		auto* pProofDst = reinterpret_cast<Merkle::Node*>(pDst + nSizeVal);
		pProof = m_AuxAlloc.m_pPtr + nSizeVal;
		if (nProof)
			memcpy((void*)pProofDst, &proof.front(), nSizeProof);

		return nProof;
	}

	BVM_METHOD(VarGetProof)
	{
		Wasm::Word pVal, nVal, pProof;
		uint32_t nRet = VarGetProofInternal(get_AddrR(pKey, nKey), nKey, pVal, nVal, pProof);

		if (nRet)
		{
			Wasm::to_wasm(get_AddrW(pnVal, sizeof(Wasm::Word)), nVal);
			Wasm::to_wasm(get_AddrW(ppVal, sizeof(Wasm::Word)), pVal);
			Wasm::to_wasm(get_AddrW(ppProof, sizeof(Wasm::Word)), pProof);
		}

		return nRet;
	}

	BVM_METHOD_HOST(VarGetProof)
	{
		Wasm::Word pVal, pProof;
		uint32_t nRet = VarGetProofInternal(pKey, nKey, pVal, *pnVal, pProof);

		if (nRet)
		{
			const uint8_t* p = get_AddrR(m_AuxAlloc.m_pPtr, 1);
			*ppVal = p + pVal;
			*ppProof = reinterpret_cast<const Merkle::Node*>(p + pProof);
		}

		return nRet;
	}

	uint32_t ProcessorManager::LogGetProofInternal(const HeightPos& hp, Wasm::Word& pProof)
	{
		ByteBuffer val;
		beam::Merkle::Proof proof;

		if (!LogGetProof(hp, proof))
		{
			FreeAuxAllocGuarded();
			return 0;
		}

		uint32_t nProof = static_cast<uint32_t>(proof.size());
		uint32_t nSizeProof = nProof * static_cast<uint32_t>(sizeof(proof[0]));

		uint8_t* pDst = ResizeAux(nSizeProof);

		auto* pProofDst = reinterpret_cast<Merkle::Node*>(pDst);
		pProof = m_AuxAlloc.m_pPtr;
		if (nProof)
			memcpy((void*) pProofDst, &proof.front(), nSizeProof);

		return nProof;
	}

	BVM_METHOD(LogGetProof)
	{
		const auto& pos_ = get_AddrAsR<HeightPos>(pos);

		HeightPos hp;
		hp.m_Height = Wasm::from_wasm(pos_.m_Height);
		hp.m_Pos = Wasm::from_wasm(pos_.m_Pos);

		Wasm::Word pProof;
		uint32_t nRet = LogGetProofInternal(hp, pProof);

		if (nRet)
			Wasm::to_wasm(get_AddrW(ppProof, sizeof(Wasm::Word)), pProof);

		return nRet;
	}

	BVM_METHOD_HOST(LogGetProof)
	{
		Wasm::Word pProof;
		uint32_t nRet = LogGetProofInternal(pos, pProof);

		if (nRet)
		{
			const uint8_t* p = get_AddrR(m_AuxAlloc.m_pPtr, 1);
			*ppProof = reinterpret_cast<const Merkle::Node*>(p + pProof);
		}

		return nRet;
	}

	BVM_METHOD(DerivePk)
	{
		return OnHost_DerivePk(get_AddrAsW<PubKey>(pubKey), get_AddrR(pID, nID), nID);
	}
	BVM_METHOD_HOST(DerivePk)
	{
		ECC::Hash::Value hv;
		DeriveKeyPreimage(hv, Blob(pID, nID));
		DerivePk(pubKey, hv);
	}

	void ProcessorManager::DeriveKeyPreimage(ECC::Hash::Value& hv, const Blob& b)
	{
		ECC::Hash::Processor()
			<< "bvm.m.key"
			<< b
			>> hv;
	}

	uint8_t* ProcessorManager::ResizeAux(uint32_t nSize)
	{
		if (m_AuxAlloc.m_Size < nSize)
		{
			FreeAuxAllocGuarded();
			m_AuxAlloc.m_pPtr = ProcessorPlus::From(*this).OnMethod_Heap_Alloc(nSize);
			m_AuxAlloc.m_Size = nSize;
		}

		return get_AddrW(m_AuxAlloc.m_pPtr, nSize);
	}

	void ProcessorManager::FreeAuxAllocGuarded()
	{
		// may raise exc, call this only from within interpretator methods
		if (m_AuxAlloc.m_pPtr)
		{
			ProcessorPlus::From(*this).OnMethod_Heap_Free(m_AuxAlloc.m_pPtr);
			ZeroObject(m_AuxAlloc);
		}
	}

	BVM_METHOD_VIA_HOST(DocAddGroup)
	BVM_METHOD_HOST(DocAddGroup)
	{
		DocID(szID);
		*m_pOut << '{';
		m_NeedComma = false;
	}

	BVM_METHOD_VIA_HOST(DocCloseGroup)
	BVM_METHOD_HOST(DocCloseGroup)
	{
		*m_pOut << '}';
		m_NeedComma = true;
	}

	BVM_METHOD_VIA_HOST(DocAddText)
	BVM_METHOD_HOST(DocAddText)
	{
		DocID(szID);
		DocQuotedText(val);
	}

	BVM_METHOD_VIA_HOST(DocAddNum32)
	BVM_METHOD_HOST(DocAddNum32)
	{
		DocID(szID);
		*m_pOut << val;
	}

	BVM_METHOD_VIA_HOST(DocAddNum64)
	BVM_METHOD_HOST(DocAddNum64)
	{
		DocID(szID);
		*m_pOut << val;
	}

	BVM_METHOD(DocAddBlob)
	{
		return OnHost_DocAddBlob(RealizeStr(szID), get_AddrR(pBlob, nBlob), nBlob);
	}
	BVM_METHOD_HOST(DocAddBlob)
	{
		DocID(szID);
		*m_pOut << '"';
		uintBigImpl::_PrintFull(reinterpret_cast<const uint8_t*>(pBlob), nBlob, *m_pOut);
		*m_pOut << '"';
	}

	BVM_METHOD_VIA_HOST(DocAddArray)
	BVM_METHOD_HOST(DocAddArray)
	{
		DocID(szID);
		*m_pOut << '[';
		m_NeedComma = false;
	}

	BVM_METHOD_VIA_HOST(DocCloseArray)
	BVM_METHOD_HOST(DocCloseArray)
	{
		*m_pOut << ']';
		m_NeedComma = true;
	}

	BVM_METHOD(DocGetText)
	{
		return OnHost_DocGetText(RealizeStr(szID), reinterpret_cast<char*>(get_AddrW(szRes, nLen)), nLen);
	}
	BVM_METHOD_HOST(DocGetText)
	{
		auto pVal = FindArg(szID);
		if (!pVal)
		{
			if (nLen)
				szRes[0] = 0;
			return 0;
		}

		uint32_t n = static_cast<uint32_t>(pVal->size()) + 1;
		if (nLen)
		{
			std::setmin(nLen, n);

			memcpy(szRes, pVal->c_str(), nLen - 1);
			szRes[nLen - 1] = 0;
		}
		return n;
	}

	BVM_METHOD(DocGetBlob)
	{
		return OnHost_DocGetBlob(RealizeStr(szID), reinterpret_cast<char*>(get_AddrW(pOut, nLen)), nLen);
	}
	BVM_METHOD_HOST(DocGetBlob)
	{
		//if (!nLen)
		//	return 0;

		auto pVal = FindArg(szID);
		if (!pVal)
			return 0;

		uint32_t nSrcLen = static_cast<uint32_t>(pVal->size());
		uint32_t nMax = nLen * 2;
		uint32_t nRes = uintBigImpl::_Scan(reinterpret_cast<uint8_t*>(pOut), pVal->c_str(), std::min(nSrcLen, nMax));

		// if src len is bigger than the requested size - return the src size
		// if parsed less than requested - return size parsed.
		// The requested size is returned only if it equals to the src size, and was successfully parsed
		return ((nRes == nMax) ? nSrcLen : nRes) / 2;
	}

	BVM_METHOD(DocGetNum32)
	{
		uint32_t val = 0;
		auto nRet = OnHost_DocGetNum32(RealizeStr(szID), &val);
		Wasm::to_wasm(get_AddrW(pOut, sizeof(val)), val);
		return nRet;
	}
	BVM_METHOD_HOST(DocGetNum32)
	{
		auto pVal = FindArg(szID);
		if (!pVal)
			return 0;

		*pOut = atoi(pVal->c_str());
		return sizeof(*pOut); // ignore errors
	}

	BVM_METHOD(DocGetNum64)
	{
		uint64_t val = 0;
		auto nRet = OnHost_DocGetNum64(RealizeStr(szID), &val);
		Wasm::to_wasm(get_AddrW(pOut, sizeof(val)), val);
		return nRet;
	}
	BVM_METHOD_HOST(DocGetNum64)
	{
		auto pVal = FindArg(szID);
		if (!pVal)
			return 0;

		*pOut = atoll(pVal->c_str());
		return sizeof(*pOut); // ignore errors
	}

	const std::string* ProcessorManager::FindArg(const char* szID)
	{
		auto it = m_Args.find(szID);
		return (m_Args.end() == it) ? nullptr : &it->second;
	}

	void ProcessorManager::DocID(const char* sz)
	{
		DocOnNext();
		if (*sz)
		{
			DocQuotedText(sz);
			*m_pOut << ": ";
		}
	}

	void ProcessorManager::DocQuotedText(const char* sz)
	{
		*m_pOut << '"';
		DocEncodedText(sz);
		*m_pOut << '"';
	}

	void ProcessorManager::DocEncodedText(const char* sz)
	{
		while (true)
		{
			char ch = *sz++;
			switch (ch)
			{
			case 0:
				return;

			case '\b': *m_pOut << "\\b"; break;
			case '\f': *m_pOut << "\\f"; break;
			case '\n': *m_pOut << "\\n"; break;
			case '\r': *m_pOut << "\\r"; break;
			case '\t': *m_pOut << "\\t"; break;
			case '"': *m_pOut << "\\\""; break;
			case '\\': *m_pOut << "\\\\"; break;

			default:
				*m_pOut << ch;
			}
		}
	}

	void ProcessorManager::DocOnNext()
	{
		if (m_NeedComma)
			*m_pOut << ',';
		else
			m_NeedComma = true;
	}

	void ProcessorManager::Call(Wasm::Word addr)
	{
		Call(addr, get_Ip());
	}

	void ProcessorManager::Call(Wasm::Word addr, Wasm::Word retAddr)
	{
		m_Stack.Push(retAddr);
		OnCall(addr);
	}

	BVM_METHOD(GenerateKernel)
	{
#pragma pack (push, 1)
		struct SigRequest {
			Wasm::Word m_pID;
			Wasm::Word m_nID;
		};
#pragma pack (pop)

		auto* pSig_ = get_ArrayAddrAsR<SigRequest>(pSig, nSig);

		std::vector<ECC::Hash::Value> vPreimages;
		vPreimages.reserve(nSig);

		for (uint32_t i = 0; i < nSig; i++)
		{
			const auto& x_ = pSig_[i];
			SigRequest x;
			x.m_pID = Wasm::from_wasm(x_.m_pID);
			x.m_nID = Wasm::from_wasm(x_.m_nID);
			DeriveKeyPreimage(vPreimages.emplace_back(), Blob(get_AddrR(x.m_pID, x.m_nID), x.m_nID));
		}

		FundsChange* pFunds_ = Cast::NotConst(get_ArrayAddrAsR<FundsChange>(pFunds, nFunds));
		for (uint32_t i = 0; i < nFunds; i++)
			pFunds_[i].Convert<true>();

		Wasm::Test(pCid || !iMethod); // only c'tor can be invoked without cid

		GenerateKernel(pCid ? &get_AddrAsR<ContractID>(pCid) : nullptr, iMethod, Blob(get_AddrR(pArg, nArg), nArg), pFunds_, nFunds, vPreimages.empty() ? nullptr : &vPreimages.front(), nSig, RealizeStr(szComment), nCharge);

		for (uint32_t i = 0; i < nFunds; i++)
			pFunds_[i].Convert<false>();
	}
	BVM_METHOD_HOST(GenerateKernel)
	{
		std::vector<ECC::Hash::Value> vPreimages;
		vPreimages.reserve(nSig);

		for (uint32_t i = 0; i < nSig; i++)
		{
			const auto& x = pSig[i];
			DeriveKeyPreimage(vPreimages.emplace_back(), Blob(x.m_pID, x.m_nID));
		}

		GenerateKernel(pCid, iMethod, Blob(pArg, nArg), pFunds, nFunds, vPreimages.empty() ? nullptr : &vPreimages.front(), nSig, szComment, nCharge);
	}

#undef BVM_METHOD_BinaryVar
#undef BVM_METHOD
#undef THE_MACRO_ParamDecl
#undef THE_MACRO_IsConst_w
#undef THE_MACRO_IsConst_r

	/////////////////////////////////////////////
	// FundsChangeMap
	void FundsChangeMap::Set(ECC::Scalar::Native& k, Amount val, bool bLock)
	{
		k = val;
		if (bLock)
			k = -k;
	}

	void FundsChangeMap::Process(Amount val, Asset::ID aid, bool bLock)
	{
		if (!val)
			return;

		auto it = m_Map.find(aid);
		if (m_Map.end() == it)
			Set(m_Map[aid], val, bLock);
		else
		{
			auto& k = it->second;
			ECC::Scalar::Native dk;
			Set(dk, val, bLock);

			k += dk;
			if (k == Zero)
				m_Map.erase(it);
		}
	}

	void FundsChangeMap::ToCommitment(ECC::Point::Native& res) const
	{
		if (m_Map.empty())
			res = Zero;
		else
		{
			// TODO: optimize for small values, and specifically for Aid=0
			ECC::MultiMac_Dyn mm;
			mm.Prepare(static_cast<uint32_t>(m_Map.size()), 1);

			for (auto it = m_Map.begin(); m_Map.end() != it; it++)
			{
				if (it->first)
				{
					CoinID::Generator gen(it->first);

					mm.m_pCasual[mm.m_Casual].Init(gen.m_hGen);
					mm.m_pKCasual[mm.m_Casual] = it->second;
					mm.m_Casual++;
				}
				else
				{
					mm.m_ppPrepared[mm.m_Prepared] = &ECC::Context::get().m_Ipp.H_;
					mm.m_pKPrep[mm.m_Prepared] = it->second;
					mm.m_Prepared++;
				}
			}

			mm.Calculate(res);
		}
	}

	/////////////////////////////////////////////
	// Other funcs

	bool ProcessorContract::LoadFixedOrZero(const VarKey& vk, uint8_t* pVal, uint32_t n)
	{
		uint32_t n0 = n;
		LoadVar(vk, pVal, n);

		if (n == n0)
			return true;

		memset0(pVal, n0);
		return false;
	}

	uint32_t ProcessorContract::SaveNnz(const VarKey& vk, const uint8_t* pVal, uint32_t n)
	{
		return SaveVar(vk, pVal, memis0(pVal, n) ? 0 : n);
	}

	void ProcessorContract::HandleAmount(Amount amount, Asset::ID aid, bool bLock)
	{
		DischargeUnits(Limits::Cost::FundsLock);

		HandleAmountInner(amount, aid, bLock);
		HandleAmountOuter(amount, aid, bLock);
	}

	void ProcessorContract::HandleAmountInner(Amount amount, Asset::ID aid, bool bLock)
	{
		struct MyCheckpoint :public Wasm::Checkpoint
		{
			void Dump(std::ostream& os) override {
				os << "Funds I/O";
			}

			uint32_t get_Type() override {
				return ErrorSubType::FundsIO;
			}

		} cp;

		VarKey vk;
		SetVarKey(vk, VarKey::Tag::LockedAmount, uintBigFrom(aid));

		AmountBig::Type val0;
		Load_T(vk, val0);

		auto val = uintBigFrom(amount);

		if (bLock)
		{
			val0 += val;
			Wasm::Test(val0 >= val); // overflow test
		}
		else
		{
			Wasm::Test(val0 >= val); // overflow test

			val0.Negate();
			val0 += val;
			val0.Negate();
		}

		Save_T(vk, val0);
	}

	void ProcessorContract::HandleAmountOuter(Amount amount, Asset::ID aid, bool bLock)
	{
		if (m_pSigValidate)
			m_FundsIO.Process(amount, aid, bLock);
	}

	bool ProcessorContract::HandleRefRaw(const VarKey& vk, bool bAdd)
	{
		uintBig_t<4> refs; // more than enough
		Load_T(vk, refs);

		bool ret = false;

		if (bAdd)
		{
			ret = (refs == Zero);
			refs.Inc();
			Wasm::Test(refs != Zero);
		}
		else
		{
			Wasm::Test(refs != Zero);
			refs.Negate();
			refs.Inv();
			ret = (refs == Zero);
		}

		Save_T(vk, refs);
		return ret;
	}

	uint8_t ProcessorContract::HandleRef(const ContractID& cid, bool bAdd)
	{
		VarKey vk;
		SetVarKey(vk, VarKey::Tag::Refs, cid);

		if (HandleRefRaw(vk, bAdd))
		{
			// z/nnz flag changed.
			VarKey vk2;
			vk2.Set(cid);

			if (bAdd)
			{
				// make sure the target contract exists
				uint32_t nData = 0;
				LoadVar(vk2, nullptr, nData);

				if (!nData)
				{
					HandleRefRaw(vk, false); // undo
					return 0;
				}
			}

			vk2.Append(VarKey::Tag::Refs, Blob(nullptr, 0));
			HandleRefRaw(vk2, bAdd);
		}

		return 1;
	}

	/////////////////////////////////////////////
	// Signature
	ECC::Point::Native& ProcessorContract::AddSigInternal(const ECC::Point& pk)
	{
		(*m_pSigValidate) << pk;

		auto& ret = m_vPks.emplace_back();
		Wasm::Test(ret.ImportNnz(pk));
		return ret;
	}

	void ProcessorContract::CheckSigs(const ECC::Point& pt, const ECC::Signature& sig)
	{
		if (!m_pSigValidate)
			return;

		struct MyCheckpoint :public Wasm::Checkpoint
		{
			void Dump(std::ostream& os) override {
				os << "CheckSigs";
			}

			uint32_t get_Type() override {
				return ErrorSubType::BadSignature;
			}

		} cp;

		auto& comm = AddSigInternal(pt);

		ECC::Point::Native ptFunds;
		m_FundsIO.ToCommitment(ptFunds);
		comm += ptFunds;

		ECC::Hash::Value hv;
		(*m_pSigValidate) >> hv;

		ECC::SignatureBase::Config cfg = ECC::Context::get().m_Sig.m_CfgG1; // copy
		cfg.m_nKeys = static_cast<uint32_t>(m_vPks.size());

		Wasm::Test(Cast::Down<ECC::SignatureBase>(sig).IsValid(cfg, hv, &sig.m_k, &m_vPks.front()));
	}

	/////////////////////////////////////////////
	// Manager
	void ProcessorManager::CallMethod(uint32_t iMethod)
	{
		const Header& hdr = ParseMod();
		Wasm::Test(iMethod < ByteOrder::from_le(hdr.m_NumMethods));
		uint32_t nAddr = ByteOrder::from_le(hdr.m_pMethod[iMethod]);
		Call(nAddr, 0);
	}

	void ProcessorManager::set_ArgBlob(const char* sz, const Blob& b)
	{
		auto& trg = m_Args[sz];
		trg.resize(b.n * 2);
		if (b.n)
			uintBigImpl::_Print(reinterpret_cast<const uint8_t*>(b.p), b.n, &trg.front());
	}

	uint32_t ProcessorManager::get_HeapLimit()
	{
		return 0x800000; // 8MB
	}

	uint32_t ProcessorManager::AddArgs(char* szCommaSeparatedPairs)
	{
		uint32_t ret = 0;
		for (size_t nLen = strlen(szCommaSeparatedPairs); nLen; )
		{
			char* szMid = (char*) memchr(szCommaSeparatedPairs, ',', nLen);

			size_t nLen1;
			if (szMid)
			{
				nLen1 = (szMid - szCommaSeparatedPairs) + 1;
				*szMid = 0;
			}
			else
				nLen1 = nLen;

			szMid = (char*) memchr(szCommaSeparatedPairs, '=', nLen1);
			if (szMid)
			{
				*szMid = 0;
				m_Args[szCommaSeparatedPairs] = szMid + 1;
				ret++;
			}

			szCommaSeparatedPairs += nLen1;
			nLen -= nLen1;
		}

		return ret;
	}

} // namespace bvm2
} // namespace beam
