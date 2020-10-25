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

namespace beam {
namespace bvm2 {

	void get_ShaderID(ECC::Hash::Value& sid, const Blob& data)
	{
		ECC::Hash::Processor()
			<< "bvm.shader.id"
			<< data.n
			<< data
			>> sid;
	}

	void get_Cid(ContractID& cid, ECC::Hash::Value& sid, const Blob& args)
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
		ECC::Hash::Value sid;
		get_ShaderID(sid, data);
		get_Cid(cid, sid, args);
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
		uint32_t m_Data0;
		uint32_t m_Table0;

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

		memset(pStack, nFill, nStackBytes);
	}

	void ProcessorContract::InitStack(uint8_t nFill /* = 0 */)
	{
		InitBase(m_pStack, sizeof(m_pStack), nFill);
	}

	void ProcessorManager::InitMem()
	{
		const uint32_t nStackBytes = 0x20000; // 128K
		const uint32_t nHeapBytes = 0x200000; // 2MB.

		m_vStack.resize(nStackBytes / sizeof(Wasm::Word));
		InitBase(&m_vStack.front(), nStackBytes, 0);

		m_vHeap.resize(nHeapBytes);
		m_LinearMem.p = &m_vHeap.front();
		m_LinearMem.n = nHeapBytes;
		m_Heap.Init(nHeapBytes);

		ZeroObject(m_AuxAlloc);
		m_EnumVars = false;
		m_LocalDepth = 0;
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
		m_Data0 = ByteOrder::from_le(hdr.m_Data0);

		m_Table0 = ByteOrder::from_le(hdr.m_Table0);
		Wasm::Test(m_Table0 <= m_Code.n);

		return hdr;
	}

	void ProcessorContract::CallFar(const ContractID& cid, uint32_t iMethod, Wasm::Word pArgs)
	{
		uint32_t nRetAddr = get_Ip();

		Wasm::Test(m_FarCalls.m_Stack.size() < Limits::FarCallDepth);
		auto& x = *m_FarCalls.m_Stack.Create_back();

		x.m_Cid = cid;
		x.m_LocalDepth = 0;

		VarKey vk;
		SetVarKey(vk);
		LoadVar(vk, x.m_Body);

		m_Code = x.m_Body;
		const Header& hdr = ParseMod();
		Wasm::Test(iMethod < ByteOrder::from_le(hdr.m_NumMethods));

		m_Stack.Push(pArgs);
		m_Stack.Push(nRetAddr);

		uint32_t nAddr = ByteOrder::from_le(hdr.m_pMethod[iMethod]);
		Jmp(nAddr);
	}

	void ProcessorContract::OnCall(Wasm::Word nAddr)
	{
		m_FarCalls.m_Stack.back().m_LocalDepth++;
		Processor::OnCall(nAddr);
	}

	void ProcessorContract::OnRet(Wasm::Word nRetAddr)
	{
		auto& nDepth = m_FarCalls.m_Stack.back().m_LocalDepth;
		if (nDepth)
			nDepth--;
		else
		{
			m_FarCalls.m_Stack.Delete(m_FarCalls.m_Stack.back());
			if (m_FarCalls.m_Stack.empty())
				return; // finished

			m_Code = m_FarCalls.m_Stack.back().m_Body;
			ParseMod(); // restore code/data sections
		}

		Processor::OnRet(nRetAddr);
	}

	void ProcessorManager::OnCall(Wasm::Word nAddr)
	{
		m_LocalDepth++;
		Processor::OnCall(nAddr);
	}

	void ProcessorManager::OnRet(Wasm::Word nRetAddr)
	{
		m_LocalDepth--;
		Processor::OnRet(nRetAddr);
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

	void ProcessorContract::SetVarKeyInternal(VarKey& vk, const void* pKey, Wasm::Word nKey)
	{
		Wasm::Test(nKey <= Limits::VarKeySize);
		SetVarKey(vk, VarKey::Tag::Internal, Blob(pKey, nKey));
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

	void Processor::Compile(ByteBuffer& res, const Blob& src, Kind kind)
	{
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
		pHdr->m_Data0 = ByteOrder::to_le(c.m_Data0);

		if (!c.m_Data.empty())
			memcpy(&c.m_Result.front() + nSizeHdr, &c.m_Data.front(), c.m_Data.size());

		// the methods themselves are not assigned yet

		c.Build();

		pHdr = reinterpret_cast<Header*>(&c.m_Result.front());
		pHdr->m_Table0 = ByteOrder::to_le(c.m_Table0);

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

		void InvokeExtPlus(uint32_t nBinding);

		BVMOpsAll_Common(THE_MACRO)
	};

	template<>
	const char* ProcessorPlus::ToHost<const char*, Wasm::Word>(Wasm::Word sz)
	{
		return RealizeStr(sz);
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

#define PAR_PASS(type, name) m_##name.V
#define PAR_DECL(type, name) ParamWrap<type> m_##name;
#define PAR_ASSIGN(type, name) args.m_##name =

#define THE_MACRO(id, ret, name) \
		case id: { \
			if (m_Dbg.m_ExtCall) \
				*m_Dbg.m_pOut << "  " #name << std::endl; \
			struct Args { \
				BVMOp_##name(PAR_DECL, MACRO_NOP) \
				RetType_##name Call(TProcessor& me) const { return me.OnMethod_##name(BVMOp_##name(PAR_PASS, MACRO_COMMA)); } \
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
#define BVM_METHOD_PAR_PASS(type, name) name
#define BVM_METHOD_PAR_PASS_TO_HOST(type, name) ProcessorPlus::From(*this).ToHost<type>(name)
#define BVM_METHOD(name) ProcessorFromMethod::name##_Type::RetType_##name ProcessorFromMethod::name##_Type::OnMethod_##name(BVMOp_##name(BVM_METHOD_PAR_DECL, MACRO_COMMA))
#define BVM_METHOD_HOST(name) ProcessorFromMethod::name##_Type::RetTypeHost_##name ProcessorFromMethod::name##_TypeEnv::OnHost_##name(BVMOp_##name(BVM_METHOD_PAR_DECL_HOST, MACRO_COMMA))

#define BVM_METHOD_AUTO_INVOKE(name) ProcessorFromMethod::name##_Type::From(*this).OnMethod_##name(BVMOp_##name(BVM_METHOD_PAR_PASS, MACRO_COMMA));
#define BVM_METHOD_HOST_AUTO(name) BVM_METHOD_HOST(name)  { return BVM_METHOD_AUTO_INVOKE(name); }

#define BVM_METHOD_VIA_HOST(name) BVM_METHOD(name) { return OnHost_##name(BVMOp_##name(BVM_METHOD_PAR_PASS_TO_HOST, MACRO_COMMA)); }

	BVM_METHOD(Memcpy)
	{
		OnHost_Memcpy(get_AddrW(pDst, size), get_AddrR(pSrc, size), size);
		return pDst;
	}
	BVM_METHOD_HOST(Memcpy)
	{
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
		return memset(pDst, val, size);
	}

	BVM_METHOD(Memcmp)
	{
		return OnHost_Memcmp(get_AddrR(p1, size), get_AddrR(p2, size), size);
	}
	BVM_METHOD_HOST(Memcmp)
	{
		return static_cast<int32_t>(memcmp(p1, p2, size));
	}

	BVM_METHOD(Memis0)
	{
		return OnHost_Memis0(get_AddrR(p, size), size);
	}
	BVM_METHOD_HOST(Memis0)
	{
		bool bRes = memis0(p, size);
		return !!bRes;
	}

	const char* Processor::RealizeStr(Wasm::Word sz, uint32_t& nLenOut) const
	{
		uint32_t n;
		auto sz_ = reinterpret_cast<const char*>(get_AddrExVar(sz, n, false));

		auto* p = reinterpret_cast<const char*>(memchr(sz_, 0, n));
		Wasm::Test(p);
		nLenOut = static_cast<uint32_t>(p - sz_);

		return sz_;
	}

	const char* Processor::RealizeStr(Wasm::Word sz) const
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

	BVM_METHOD(Heap_Alloc)
	{
		size = Stack::AlignUp(size); // use the same alignment for heap too

		uint32_t val;
		if (!m_Heap.Alloc(val, size))
			return 0;

		return Wasm::MemoryType::Global | val;
	}
	BVM_METHOD_HOST(Heap_Alloc)
	{
		uint32_t val;
		if (!m_Heap.Alloc(val, size))
			return nullptr;

		return reinterpret_cast<uint8_t*>(Cast::NotConst(m_LinearMem.p)) + val;
	}

	BVM_METHOD(Heap_Free)
	{
		m_Heap.Free(pPtr ^ Wasm::MemoryType::Global);
	}
	BVM_METHOD_HOST(Heap_Free)
	{
		auto val =
			reinterpret_cast<const uint8_t*>(pPtr) -
			reinterpret_cast<const uint8_t*>(m_LinearMem.p);

		m_Heap.Free(static_cast<uint32_t>(val));
	}

	void Processor::Heap::Init(uint32_t nRange)
	{
		Clear();

		auto* p = new Heap::Entry;
		p->m_Pos.m_Key = 0;
		p->m_Size.m_Key = nRange;
		Insert(*p, true);
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
			auto* p = new Heap::Entry;
			p->m_Pos.m_Key = e.m_Pos.m_Key;
			p->m_Size.m_Key = size;
			Insert(*p, false);

			e.m_Pos.m_Key += size; // no need to remove and re-insert, order should not change
			UpdateSizeFree(e, e.m_Size.m_Key - size);
		}

		return true;
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

	BVM_METHOD(LoadVar)
	{
		return OnHost_LoadVar(get_AddrR(pKey, nKey), nKey, get_AddrW(pVal, nVal), nVal);
	}
	BVM_METHOD_HOST(LoadVar)
	{
		VarKey vk;
		SetVarKeyInternal(vk, pKey, nKey);

		LoadVar(vk, static_cast<uint8_t*>(pVal), nVal);
		return nVal;
	}

	BVM_METHOD(SaveVar)
	{
		return OnHost_SaveVar(get_AddrR(pKey, nKey), nKey, get_AddrR(pVal, nVal), nVal);
	}
	BVM_METHOD_HOST(SaveVar)
	{
		VarKey vk;
		SetVarKeyInternal(vk, pKey, nKey);

		Wasm::Test(nVal <= Limits::VarSize);
		SaveVar(vk, static_cast<const uint8_t*>(pVal), nVal);
	}

	BVM_METHOD(CallFar)
	{
		// make sure the pArgs is not malicious.
		// Attacker may try to cause the target shader to overwrite its future stack operands, or const data in its data section.
		//
		switch (Wasm::MemoryType::Mask & pArgs)
		{
		case Wasm::MemoryType::Global:
			break; // this is allowed

		case Wasm::MemoryType::Stack:
			Wasm::Test(pArgs >= m_Stack.get_AlasSp());
			break;

		default:
			// invalid, null, or current data section pointer. NOT allowed!
			Wasm::Fail();
		}

		CallFar(get_AddrAsR<ContractID>(cid), iMethod, pArgs);
	}
	BVM_METHOD_HOST(CallFar)
	{
		Wasm::Fail(); // not supported in this form. Proper host code would invoke CallFar_T
	}

	BVM_METHOD(Halt)
	{
		Wasm::Fail();
	}
	BVM_METHOD_HOST_AUTO(Halt)

	BVM_METHOD(AddSig)
	{
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
		return OnHost_RefAdd(get_AddrAsR<ContractID>(cid));
	}
	BVM_METHOD_HOST(RefAdd)
	{
		return ProcessorPlus_Contract::From(*this).HandleRef(cid, true);
	}

	BVM_METHOD(RefRelease)
	{
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
		OnHost_VarsEnum(nTag0, get_AddrR(pKey0, nKey0), nKey0, nTag1, get_AddrR(pKey1, nKey1), nKey1);
	}
	BVM_METHOD_HOST(VarsEnum)
	{
		Wasm::Test(m_pCid);
		FreeAuxAllocGuarded();

		VarKey vk0, vk1;
		vk0.Set(*m_pCid);
		vk0.Append(nTag0, Blob(pKey0, nKey0));
		vk1.Set(*m_pCid);
		vk1.Append(nTag1, Blob(pKey1, nKey1));

		VarsEnum(vk0, vk1);

		m_EnumVars = true;
	}

	BVM_METHOD(VarsMoveNext)
	{
		auto pnTag_ = get_AddrW(pnTag, 1);
		auto ppKey_ = get_AddrW(ppKey, sizeof(Wasm::Word));
		auto pnKey_ = get_AddrW(pnKey, sizeof(Wasm::Word));
		auto ppVal_ = get_AddrW(ppVal, sizeof(Wasm::Word));
		auto pnVal_ = get_AddrW(pnVal, sizeof(Wasm::Word));

		const void *pKey, *pVal;
		uint32_t nKey, nVal;

		if (!OnHost_VarsMoveNext(pnTag_, &pKey, &nKey, &pVal, &nVal))
			return 0;

		uint32_t nSizeTotal = nKey + nVal;

		if (m_AuxAlloc.m_Size < nSizeTotal)
		{
			FreeAuxAllocGuarded();
			m_AuxAlloc.m_pPtr = ProcessorPlus::From(*this).OnMethod_Heap_Alloc(nSizeTotal);
			m_AuxAlloc.m_Size = nSizeTotal;
		}
		uint8_t* pDst = get_AddrW(m_AuxAlloc.m_pPtr, nSizeTotal);

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
		Wasm::Test(m_EnumVars); // illegal to call this method before VarsEnum

		Blob key, data;
		if (!VarsMoveNext(key, data))
		{
			FreeAuxAllocGuarded();
			m_EnumVars = false;
			return 0;
		}

		Wasm::Test(key.n > ContractID::nBytes); // assert should be enough, but this is for extra safety
		auto pKey = reinterpret_cast<const uint8_t*>(key.p);

		*pnTag = pKey[ContractID::nBytes];
		*ppKey = pKey + ContractID::nBytes + 1;
		*pnKey = key.n - (ContractID::nBytes + 1);
		*ppVal = data.p;
		*pnVal = data.n;

		return 1;
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
		Wasm::Test(m_pCid);

		ECC::Hash::Processor()
			<< "bvm.m.key"
			<< *m_pCid
			<< b
			>> hv;
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
		if (!nLen)
			return 0;

		auto pVal = FindArg(szID);
		if (!pVal)
		{
			szRes[0] = 0;
			return 0;
		}

		uint32_t n = static_cast<uint32_t>(pVal->size()) + 1;
		std::setmin(nLen, n);
		
		memcpy(szRes, pVal->c_str(), nLen - 1);
		szRes[nLen - 1] = 0;
		return n;
	}

	BVM_METHOD(DocGetBlob)
	{
		return OnHost_DocGetText(RealizeStr(szID), reinterpret_cast<char*>(get_AddrW(pOut, nLen)), nLen);
	}
	BVM_METHOD_HOST(DocGetBlob)
	{
		if (!nLen)
			return 0;

		auto pVal = FindArg(szID);
		if (!pVal)
			return 0;

		return uintBigImpl::_Scan(reinterpret_cast<uint8_t*>(pOut), pVal->c_str(), static_cast<uint32_t>(pVal->size()));
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
		DocQuotedText(sz);
		*m_pOut << ": ";
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

		GenerateKernel(iMethod, Blob(get_AddrR(pArg, nArg), nArg), pFunds_, nFunds, vPreimages.empty() ? nullptr : &vPreimages.front(), nSig);

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

		GenerateKernel(iMethod, Blob(pArg, nArg), pFunds, nFunds, vPreimages.empty() ? nullptr : &vPreimages.front(), nSig);
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

	bool ProcessorContract::SaveNnz(const VarKey& vk, const uint8_t* pVal, uint32_t n)
	{
		return SaveVar(vk, pVal, memis0(pVal, n) ? 0 : n);
	}

	void ProcessorContract::HandleAmount(Amount amount, Asset::ID aid, bool bLock)
	{
		HandleAmountInner(amount, aid, bLock);
		HandleAmountOuter(amount, aid, bLock);
	}

	void ProcessorContract::HandleAmountInner(Amount amount, Asset::ID aid, bool bLock)
	{
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

} // namespace bvm2
} // namespace beam
