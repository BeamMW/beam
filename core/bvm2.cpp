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
#include "bvm2.h"
#include "bvm2_opcodes.h"
#include "../utility/byteorder.h"
#include <sstream>

namespace beam {
namespace bvm2 {

	void get_Cid(ContractID& cid, const Blob& data, const Blob& args)
	{
		ECC::Hash::Processor()
			<< "bvm.cid"
			<< data.n
			<< args.n
			<< data
			<< args
			>> cid;
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
		static const uint32_t s_Version = 1;

		uint32_t m_Version;
		uint32_t m_NumMethods;
		uint32_t m_Data0;

		static const uint32_t s_MethodsMin = 2; // c'tor and d'tor
		static const uint32_t s_MethodsMax = 1U << 28; // would be much much less of course, this is just to ensure no-overflow for offset calculation

		uint32_t m_pMethod[s_MethodsMin]; // var size
	};
#pragma pack (pop)


	void Processor::InitStack(const Blob& args, uint8_t nFill /* = 0 */)
	{
		m_Stack.m_pPtr = m_pStack;
		m_Stack.m_BytesMax = sizeof(m_pStack);
		m_Stack.m_Pos = 0;

		Wasm::Test(args.n <= sizeof(m_pStack));
		m_Stack.m_BytesCurrent = sizeof(m_pStack) - m_Stack.AlignUp(args.n);

		memset(m_pStack, nFill, sizeof(m_pStack));
		memcpy(reinterpret_cast<uint8_t*>(m_pStack) + m_Stack.m_BytesCurrent, args.p, args.n);

		ZeroObject(m_Code);
		ZeroObject(m_Data);
		ZeroObject(m_LinearMem);
		ZeroObject(m_Instruction);
	}

	const Processor::Header& Processor::ParseMod()
	{
		m_Code = m_FarCalls.m_Stack.back().m_Body;

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

		return hdr;
	}

	void Processor::CallFar(const ContractID& cid, uint32_t iMethod, Wasm::Word pArgs)
	{
		uint32_t nRetAddr = get_Ip();

		Wasm::Test(m_FarCalls.m_Stack.size() < Limits::FarCallDepth);
		auto& x = *m_FarCalls.m_Stack.Create_back();

		x.m_Cid = cid;
		x.m_LocalDepth = 0;

		VarKey vk;
		SetVarKey(vk);
		LoadVar(vk, x.m_Body);

		const Header& hdr = ParseMod();
		Wasm::Test(iMethod < ByteOrder::from_le(hdr.m_NumMethods));

		m_Stack.Push(pArgs);
		m_Stack.Push(nRetAddr);

		uint32_t nAddr = ByteOrder::from_le(hdr.m_pMethod[iMethod]);
		Jmp(nAddr);
	}

	void Processor::OnCall(Wasm::Word nAddr)
	{
		m_FarCalls.m_Stack.back().m_LocalDepth++;
		Wasm::Processor::OnCall(nAddr);
	}

	void Processor::OnRet(Wasm::Word nRetAddr)
	{
		auto& nDepth = m_FarCalls.m_Stack.back().m_LocalDepth;
		if (nDepth)
			nDepth--;
		else
		{
			m_FarCalls.m_Stack.Delete(m_FarCalls.m_Stack.back());
			if (m_FarCalls.m_Stack.empty())
				return; // finished

			ParseMod(); // restore code/data sections
		}

		Wasm::Processor::OnRet(nRetAddr);
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

	void Processor::SetVarKey(VarKey& vk)
	{
		vk.Set(m_FarCalls.m_Stack.back().m_Cid);
	}

	void Processor::SetVarKey(VarKey& vk, uint8_t nTag, const Blob& blob)
	{
		SetVarKey(vk);
		vk.Append(nTag, blob);
	}

	void Processor::SetVarKeyInternal(VarKey& vk, Wasm::Word pKey, Wasm::Word nKey)
	{
		Wasm::Test(nKey <= Limits::VarKeySize);
		const uint8_t* pKey_ = get_AddrR(pKey, nKey);
		SetVarKey(vk, VarKey::Tag::Internal, Blob(pKey_, nKey));
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

	void Processor::Compile(ByteBuffer& res, const Blob& src)
	{
		Wasm::Reader inp;
		inp.m_p0 = reinterpret_cast<const uint8_t*>(src.p);
		inp.m_p1 = inp.m_p0 + src.n;

		Wasm::Compiler c;
		c.Parse(inp);

		ResolveBindings(c);

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

#define MACRO_NOP
#define MACRO_COMMA ,

	typedef ECC::Point PubKey;
	typedef Asset::ID AssetID;

	struct ProcessorPlus
		:public Processor
	{
		void InvokeExtPlus(uint32_t nBinding);

#define PAR_DECL(type, name) ParamWrap<type>::Type name
#define THE_MACRO(id, ret, name) \
		typedef ParamWrap<ret>::Type RetType_##name; \
		RetType_##name OnMethod_##name(BVMOp_##name(PAR_DECL, MACRO_COMMA));
		BVMOpsAll(THE_MACRO)
#undef THE_MACRO
#undef PAR_DECL

		static void ResolveBindings(Wasm::Compiler&);

		template <typename TRes> struct Caller;

		void HandleAmount(Amount, Asset::ID, bool bLock);
		void HandleAmountInner(Amount, Asset::ID, bool bLock);
		void HandleAmountOuter(Amount, Asset::ID, bool bLock);

		uint8_t HandleRef(Wasm::Word pContractID, bool bAdd);
		bool HandleRefRaw(const VarKey&, bool bAdd);

		template <typename T> const T& get_AddrAsR(uint32_t nOffset) {
			return *reinterpret_cast<const T*>(get_AddrR(nOffset, sizeof(T)));
		}
	};

	template <typename TRes> struct ProcessorPlus::Caller {
		template <typename TArgs>
		static void Call(ProcessorPlus& me, const TArgs& args) {
			me.m_Stack.Push<TRes>(args.Call(me));
		}
	};

	template <> struct ProcessorPlus::Caller<void> {
		template <typename TArgs>
		static void Call(ProcessorPlus& me, const TArgs& args) {
			args.Call(me);
		}
	};


	void ProcessorPlus::InvokeExtPlus(uint32_t nBinding)
	{
		switch (nBinding)
		{
#define PAR_PASS(type, name) m_##name.V
#define PAR_DECL(type, name) ParamWrap<type> m_##name;
#define PAR_ASSIGN(type, name) args.m_##name =

#define THE_MACRO(id, ret, name) \
		case id: { \
			if (m_Dbg.m_ExtCall) \
				*m_Dbg.m_pOut << "  " #name << std::endl; \
			struct Args { \
				BVMOp_##name(PAR_DECL, MACRO_NOP) \
				RetType_##name Call(ProcessorPlus& me) const { return me.OnMethod_##name(BVMOp_##name(PAR_PASS, MACRO_COMMA)); } \
			} args; \
			BVMOp_##name(PAR_ASSIGN, MACRO_NOP) *this; \
			Caller<RetType_##name>::Call(*this, args); \
		} break;

		BVMOpsAll(THE_MACRO)

#undef THE_MACRO
#undef PAR_PASS
#undef PAR_DECL
#undef PAR_ASSIGN

		default:
			Wasm::Processor::InvokeExt(nBinding);
		}
	}

	void Processor::InvokeExt(uint32_t nBinding)
	{
		static_assert(sizeof(*this) == sizeof(ProcessorPlus));
		Cast::Up<ProcessorPlus>(*this).InvokeExtPlus(nBinding);
	}

	void TestStackPtr(const Wasm::Compiler::GlobalVar& x)
	{
		Wasm::Test(x.m_IsVariable && (Wasm::TypeCode::i32 == x.m_Type));
	}

	void Processor::ResolveBindings(Wasm::Compiler& c)
	{
		for (uint32_t i = 0; i < c.m_ImportFuncs.size(); i++)
		{
			auto& x = c.m_ImportFuncs[i];

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
			} else
			
			BVMOpsAll(THE_MACRO)
#undef THE_MACRO

				Wasm::Fail(); // name not found
		}

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

#define BVM_METHOD_PAR_DECL(type, name) ParamWrap<type>::Type name
#define BVM_METHOD(name) ProcessorPlus::RetType_##name ProcessorPlus::OnMethod_##name(BVMOp_##name(BVM_METHOD_PAR_DECL, MACRO_COMMA))

	BVM_METHOD(Memcpy)
	{
		// prefer to use memmove
		memmove(
			get_AddrW(pDst, size),
			get_AddrR(pSrc, size),
			size);

		return pDst;
	}

	BVM_METHOD(Memset)
	{
		memset(get_AddrW(pDst, size), val, size);
		return pDst;
	}

	BVM_METHOD(Memcmp)
	{
		return memcmp(
			get_AddrR(p1, size),
			get_AddrR(p2, size),
			size);
	}
	BVM_METHOD(Memis0)
	{
		bool bRes = memis0(
			get_AddrR(p, size),
			size);

		return !!bRes;
	}

	BVM_METHOD(StackAlloc)
	{
		Wasm::Test(size <= m_Stack.m_BytesCurrent);
		m_Stack.m_BytesCurrent -= m_Stack.AlignUp(size);
		m_Stack.TestSelf();

		return m_Stack.get_AlasSp();
	}

	BVM_METHOD(StackFree)
	{
		m_Stack.m_BytesCurrent += m_Stack.AlignUp(size);
		Wasm::Test(size <= m_Stack.m_BytesCurrent); // no overflow
		m_Stack.TestSelf();
	}

	BVM_METHOD(LoadVar)
	{
		VarKey vk;
		SetVarKeyInternal(vk, pKey, nKey);

		uint8_t* pVal_ = get_AddrW(pVal, nVal);
		LoadVar(vk, pVal_, nVal);
		return nVal;
	}

	BVM_METHOD(SaveVar)
	{
		VarKey vk;
		SetVarKeyInternal(vk, pKey, nKey);

		Wasm::Test(nVal <= Limits::VarSize);
		uint8_t* pVal_ = get_AddrW(pVal, nVal);

		SaveVar(vk, pVal_, nVal);
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

	BVM_METHOD(Halt)
	{
		Wasm::Fail();
	}

	BVM_METHOD(AddSig)
	{
		const auto& pk = get_AddrAsR<ECC::Point>(pubKey);

		if (m_pSigValidate)
			AddSigInternal(pk);
	}

	BVM_METHOD(FundsLock)
	{
		HandleAmount(amount, aid, true);
	}

	BVM_METHOD(FundsUnlock)
	{
		HandleAmount(amount, aid, false);
	}

	BVM_METHOD(RefAdd)
	{
		return HandleRef(cid, true);
	}

	BVM_METHOD(RefRelease)
	{
		return HandleRef(cid, false);
	}

	BVM_METHOD(AssetCreate)
	{
		Wasm::Test(nMeta && (nMeta <= Asset::Info::s_MetadataMaxSize));

		Asset::Metadata md;
		Blob(get_AddrR(pMeta, nMeta), nMeta).Export(md.m_Value);
		md.UpdateHash();

		AssetVar av;
		get_AssetOwner(av.m_Owner, m_FarCalls.m_Stack.back().m_Cid, md);

		Asset::ID ret = AssetCreate(md, av.m_Owner);
		if (ret)
		{
			HandleAmountOuter(Rules::get().CA.DepositForList, Zero, true);

			SetAssetKey(av, ret);
			SaveVar(av.m_vk, av.m_Owner.m_pData, av.m_Owner.nBytes);
		}

		return ret;
	}

	void Processor::SetAssetKey(AssetVar& av, Asset::ID aid)
	{
		SetVarKey(av.m_vk);
		av.m_vk.Append(VarKey::Tag::OwnedAsset, uintBigFrom(aid));
	}

	void Processor::get_AssetStrict(AssetVar& av, Asset::ID aid)
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

#undef BVM_METHOD_BinaryVar
#undef BVM_METHOD
#undef THE_MACRO_ParamDecl
#undef THE_MACRO_IsConst_w
#undef THE_MACRO_IsConst_r

	/////////////////////////////////////////////
	// Other funcs

	bool Processor::LoadFixedOrZero(const VarKey& vk, uint8_t* pVal, uint32_t n)
	{
		uint32_t n0 = n;
		LoadVar(vk, pVal, n);

		if (n == n0)
			return true;

		memset0(pVal, n0);
		return false;
	}

	bool Processor::SaveNnz(const VarKey& vk, const uint8_t* pVal, uint32_t n)
	{
		return SaveVar(vk, pVal, memis0(pVal, n) ? 0 : n);
	}

	void ProcessorPlus::HandleAmount(Amount amount, Asset::ID aid, bool bLock)
	{
		HandleAmountInner(amount, aid, bLock);
		HandleAmountOuter(amount, aid, bLock);
	}

	void ProcessorPlus::HandleAmountInner(Amount amount, Asset::ID aid, bool bLock)
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

	void ProcessorPlus::HandleAmountOuter(Amount amount, Asset::ID aid, bool bLock)
	{
		if (m_pSigValidate)
		{
			ECC::Point::Native pt;
			CoinID::Generator(aid).AddValue(pt, amount);

			if (bLock)
				pt = -pt;

			m_FundsIO += pt;
		}
	}

	bool ProcessorPlus::HandleRefRaw(const VarKey& vk, bool bAdd)
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

	uint8_t ProcessorPlus::HandleRef(Wasm::Word pCID, bool bAdd)
	{
		const auto& cid = get_AddrAsR<ContractID>(pCID);

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
	ECC::Point::Native& Processor::AddSigInternal(const ECC::Point& pk)
	{
		(*m_pSigValidate) << pk;

		auto& ret = m_vPks.emplace_back();
		Wasm::Test(ret.ImportNnz(pk));
		return ret;
	}

	void Processor::CheckSigs(const ECC::Point& pt, const ECC::Signature& sig)
	{
		if (!m_pSigValidate)
			return;

		auto& comm = AddSigInternal(pt);
		comm += m_FundsIO;

		ECC::Hash::Value hv;
		(*m_pSigValidate) >> hv;

		ECC::SignatureBase::Config cfg = ECC::Context::get().m_Sig.m_CfgG1; // copy
		cfg.m_nKeys = static_cast<uint32_t>(m_vPks.size());

		Wasm::Test(Cast::Down<ECC::SignatureBase>(sig).IsValid(cfg, hv, &sig.m_k, &m_vPks.front()));
	}

	/////////////////////////////////////////////
	// Sort
	void Processor::ArrayContext::MergeSort(uint8_t* p) const
	{
		if (m_nCount <= 1)
			return;

		ByteBuffer buf;
		buf.resize(m_nSize);
		uint8_t* pDst = &buf.front();

		for (uint32_t nW = 1; nW < m_nCount; )
		{
			uint32_t n0 = 0;
			uint32_t nStepBytes = m_nElementWidth * nW;
			uint32_t nW1 = nW;

			for (uint32_t nPos = 0; ; )
			{
				nPos += nW;
				if (nPos >= m_nCount)
				{
					// just copy what's left
					memcpy(pDst + n0, p + n0, m_nSize - n0);
					break;
				}

				uint32_t n1 = n0 + nStepBytes;

				nPos += nW;
				bool bFin = (nPos > m_nCount);
				if (bFin)
					nW1 -= (nPos - m_nCount);

				MergeOnce(pDst, p, n0, nW, n1, nW1);

				if (bFin)
					break;

				n0 = n1 + nStepBytes;
			}

			nW <<= 1;
			Wasm::Test(nW); // overflow test. Though can't happen with our restricted array sizes

			std::swap(p, pDst);
		}

		if (&buf.front() != pDst)
			memcpy(pDst, p, m_nSize);
	}

	void Processor::ArrayContext::MergeOnce(uint8_t* pDst, const uint8_t* pSrc, uint32_t p0, uint32_t n0, uint32_t p1, uint32_t n1) const
	{
		assert(n0 && n1);

		const uint8_t* ppSrc[] = { pSrc + p0, pSrc + p1 };
		uint32_t pN[] = { n0, n1 };

		pDst += p0;

		unsigned int iIdx = 0;
		while (true)
		{
			int n = memcmp(ppSrc[0] + m_nKeyPos, ppSrc[1] + m_nKeyPos, m_nKeyWidth);
			iIdx = (n > 0);

			memcpy(pDst, ppSrc[iIdx], m_nElementWidth);

			pDst += m_nElementWidth;
			ppSrc[iIdx] += m_nElementWidth;
			pN[iIdx]--;

			if (!pN[iIdx])
			{
				iIdx = !iIdx;
				memcpy(pDst, ppSrc[iIdx], m_nElementWidth * pN[iIdx]);
				break;
			}
		}
	}

} // namespace bvm2
} // namespace beam
