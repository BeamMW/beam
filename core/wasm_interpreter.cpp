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
#include "wasm_interpreter.h"
#include "../utility/byteorder.h"
#include <sstream>

#define MY_TOKENIZE2(a, b) a##b
#define MY_TOKENIZE1(a, b) MY_TOKENIZE2(a, b)

namespace beam {
namespace Wasm {

	void Fail() {
		throw std::runtime_error("wasm");
	}
	void Test(bool b) {
		if (!b)
			Fail();
	}

	// wasm uses LE format
	template <typename T>
	T to_wasm(T x)
	{
		return ByteOrder::to_le(x);
	}

	template <typename T>
	void to_wasm(uint8_t* p, T x)
	{
		x = to_wasm(x);
		memcpy(p, &x, sizeof(x));
	}

	template <typename T>
	T from_wasm(T x)
	{
		return ByteOrder::from_le(x);
	}

	template <typename T>
	T from_wasm(const uint8_t* p)
	{
		T x;
		memcpy(&x, p, sizeof(x)); // fix alignment
		return from_wasm(x);
	}

	/////////////////////////////////////////////
	// Reader

	void Reader::Ensure(uint32_t n)
	{
		Test(static_cast<size_t>(m_p1 - m_p0) >= n);
	}

	const uint8_t* Reader::Consume(uint32_t n)
	{
		Ensure(n);

		const uint8_t* pRet = m_p0;
		m_p0 += n;

		return pRet;
	}

	template <typename T, bool bSigned>
	T Reader::ReadInternal()
	{
		static_assert(!std::numeric_limits<T>::is_signed); // the sign flag must be specified separately

		T ret = 0;
		for (unsigned int nShift = 0; ; )
		{
			uint8_t n = Read1();
			bool bEnd = !(0x80 & n);
			if (!bEnd)
				n &= ~0x80;

			ret |= T(n) << nShift;
			nShift += 7;

			if (bEnd)
			{
				if constexpr (bSigned)
				{
					if (0x40 & n)
						ret |= (~static_cast<T>(0) << nShift);
				}
				break;
			}

			Test(nShift < sizeof(ret) * 8);
		}

		return ret;
	}

	template <>
	uint32_t Reader::Read<uint32_t>()
	{
		return ReadInternal<uint32_t, false>();
	}

	template <>
	int32_t Reader::Read<int32_t>()
	{
		return ReadInternal<uint32_t, true>();
	}

	template <>
	int64_t Reader::Read<int64_t>()
	{
		return ReadInternal<uint64_t, true>();
	}

	/////////////////////////////////////////////
	// Common

	struct Type
	{
		static const uint8_t i32 = 0x7F;
		static const uint8_t i64 = 0x7E;
		static const uint8_t f32 = 0x7D;
		static const uint8_t f64 = 0x7C;

		static const uint8_t s_Base = 0x7C; // for the 2-bit type encoding

		static uint8_t Words(uint8_t t) {
			switch (t) {
			default:
				Fail();
			case i32:
			case f32:
				return 1;
			case i64:
			case f64:
				return 2;
			}
		}

		static int32_t SignedFrom(uint32_t x) { return x; }
		static int64_t SignedFrom(uint64_t x) { return x; }
	};

#define WasmInstructions_unop_Polymorphic_32(macro) \
	macro(eqz   , 0x45, 0x50) \

#define WasmInstructions_binop_Polymorphic_32(macro) \
	macro(eq    , 0x46, 0x51) \
	macro(ne    , 0x47, 0x52) \
	macro(lt_s  , 0x48, 0x53) \
	macro(lt_u  , 0x49, 0x54) \
	macro(gt_s  , 0x4A, 0x55) \
	macro(gt_u  , 0x4B, 0x56) \
	macro(le_s  , 0x4C, 0x57) \
	macro(le_u  , 0x4D, 0x58) \
	macro(ge_s  , 0x4E, 0x59) \
	macro(ge_u  , 0x4F, 0x5A) \

#define WasmInstructions_binop_Polymorphic_x(macro) \
	macro(add   , 0x6A, 0x7C) \
	macro(sub   , 0x6B, 0x7D) \
	macro(mul   , 0x6C, 0x7E) \
	macro(div_s , 0x6D, 0x7F) \
	macro(div_u , 0x6E, 0x80) \
	macro(rem_s , 0x6F, 0x81) \
	macro(rem_u , 0x70, 0x82) \
	macro(and   , 0x71, 0x83) \
	macro(or    , 0x72, 0x84) \
	macro(xor   , 0x73, 0x85) \
	macro(shl   , 0x74, 0x86) \
	macro(shr_s , 0x75, 0x87) \
	macro(shr_u , 0x76, 0x88) \
	macro(rotl  , 0x77, 0x89) \
	macro(rotr  , 0x78, 0x8A) \

#define WasmInstructions_CustomPorted(macro) \
	macro(0x1A, drop) \
	macro(0x1B, select) \
	macro(0x20, local_get) \
	macro(0x21, local_set) \
	macro(0x22, local_tee) \
	macro(0x23, global_get) \
	macro(0x24, global_set) \
	macro(0x28, i32_load) \
	macro(0x29, i64_load) \
	macro(0x2C, i32_load8_s) \
	macro(0x2D, i32_load8_u) \
	macro(0x36, i32_store) \
	macro(0x37, i64_store) \
	macro(0x3A, i32_store8) \
	macro(0x10, call) \
	macro(0x0C, br) \
	macro(0x0D, br_if) \
	macro(0x41, i32_const) \
	macro(0x42, i64_const) \

#define WasmInstructions_Proprietary(macro) \
	macro(0xf1, prolog) \
	macro(0xf2, ret) \
	macro(0xf3, call_ext) \
	macro(0xf8, global_get_imp) \
	macro(0xf9, global_set_imp) \

#define WasmInstructions_NotPorted(macro) \
	macro(0x01, nop) \
	macro(0x02, block) \
	macro(0x03, loop) \
	macro(0x0B, end_block) \

#define WasmInstructions_AllInitial(macro) \
	WasmInstructions_CustomPorted(macro) \
	WasmInstructions_NotPorted(macro)/* \
	WasmInstructions_unop_i32_i32(macro) \
	WasmInstructions_unop_i32_i64(macro) \
	WasmInstructions_binop_i32_i32(macro) \
	WasmInstructions_binop_i32_i64(macro) \
	WasmInstructions_binop_i64_i64(macro) \*/

	enum Instruction
	{
#define THE_MACRO(id, name) name = id,
		WasmInstructions_CustomPorted(THE_MACRO) \
		WasmInstructions_NotPorted(THE_MACRO) \
		WasmInstructions_Proprietary(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(name, id32, id64) \
		i32_##name = id32, \
		i64_##name = id64,

		WasmInstructions_unop_Polymorphic_32(THE_MACRO)
		WasmInstructions_binop_Polymorphic_32(THE_MACRO)
		WasmInstructions_binop_Polymorphic_x(THE_MACRO)
#undef THE_MACRO

	};

	/////////////////////////////////////////////
	// Compiler
	uint32_t Compiler::PerFunction::Locals::get_SizeWords() const
	{
		return m_v.empty() ? 0 : m_v.back().m_PosWords + m_v.back().m_SizeWords;
	}

	void Compiler::PerFunction::Locals::Add(uint8_t nType)
	{
		uint32_t nPosWords = get_SizeWords();

		auto& v = m_v.emplace_back();
		v.m_Type = nType;
		v.m_PosWords = nPosWords;
		v.m_SizeWords = Type::Words(nType);
	}

	struct CompilerPlus
		:public Compiler
	{
#define WasmParserSections(macro) \
		macro(1, Type) \
		macro(2, Import) \
		macro(3, Funcs) \
		macro(6, Global) \
		macro(7, Export) \
		macro(10, Code) \
		macro(11, Data) \
		macro(12, DataCount) \

#define THE_MACRO(id, name) void OnSection_##name(Reader&);
		WasmParserSections(THE_MACRO)
#undef THE_MACRO

		void ParsePlus(Reader);
	};

	void Compiler::Parse(const Reader& inp)
	{
		auto& c = Cast::Up<CompilerPlus>(*this);
		static_assert(sizeof(c) == sizeof(*this));
		c.ParsePlus(inp);

	}

	void CompilerPlus::ParsePlus(Reader inp)
	{
		static const uint8_t pMagic[] = { 0, 'a', 's', 'm' };
		Test(!memcmp(pMagic, inp.Consume(sizeof(pMagic)), sizeof(pMagic)));

		static const uint8_t pVer[] = { 1, 0, 0, 0 };
		Test(!memcmp(pVer, inp.Consume(sizeof(pVer)), sizeof(pVer)));

		for (uint8_t nPrevSection = 0; inp.m_p0 < inp.m_p1; )
		{
			auto nSection = inp.Read1();
			bool bIgnoreOrder = !nSection || (12 == nSection);
			Test(!nPrevSection || bIgnoreOrder || (nSection > nPrevSection));

			auto nLen = inp.Read<uint32_t>();

			Reader inpSection;
			inpSection.m_p0 = inp.Consume(nLen);
			inpSection.m_p1 = inpSection.m_p0 + nLen;

			switch (nSection)
			{
#define THE_MACRO(id, name) case id: OnSection_##name(inpSection); Test(inpSection.m_p0 == inpSection.m_p1); break;
			WasmParserSections(THE_MACRO)
#undef THE_MACRO

			}

			if (!bIgnoreOrder)
				nPrevSection = nSection;
		}

		m_Labels.m_Items.resize(m_Functions.size()); // function labels
	}


	void CompilerPlus::OnSection_Type(Reader& inp)
	{
		auto nCount = inp.Read<uint32_t>();
		m_Types.resize(nCount);

		for (uint32_t i = 0; i < nCount; i++)
		{
			auto& x = m_Types[i];

			Test(inp.Read1() == 0x60);

			x.m_Args.Read(inp);
			x.m_Rets.Read(inp);

			Test(x.m_Rets.n <= 1);
		}
	}

	void CompilerPlus::OnSection_Import(Reader& inp)
	{
		auto nCount = inp.Read<uint32_t>();

		for (uint32_t i = 0; i < nCount; i++)
		{
			PerImport pi;
			pi.m_sMod.Read(inp);
			pi.m_sName.Read(inp);

			uint8_t nKind = inp.Read1();
			switch (nKind)
			{
			case 0: {
				auto& x = m_ImportFuncs.emplace_back();
				Cast::Down<PerImport>(x) = pi;
				x.m_TypeIdx = inp.Read<uint32_t>();
				Test(x.m_TypeIdx < m_Types.size());
			}
			break;

			case 1: {
				// table. Ignore.
				uint32_t lim;
				lim = inp.Read<uint32_t>();
				lim = inp.Read<uint32_t>();
				lim;

				uint8_t nElemType = inp.Read1();
				nElemType;


			} break;

			case 2: {

				// mem type. Ignore.
				uint32_t lim;
				lim = inp.Read<uint32_t>();
				lim = inp.Read<uint32_t>();
				lim;
			}
			break;

			case 3: {
				// global
				auto& x = m_ImportGlobals.emplace_back();
				Cast::Down<PerImport>(x) = pi;
				x.m_Type = inp.Read1();
				x.m_IsVariable = inp.Read1();
			}
			break;

			default:
				Fail();

			}

		}
	}

	void CompilerPlus::OnSection_Funcs(Reader& inp)
	{
		auto nCount = inp.Read<uint32_t>();
		m_Functions.resize(nCount);

		for (uint32_t i = 0; i < nCount; i++)
		{
			auto& x = m_Functions[i];
			ZeroObject(x.m_Expression);

			x.m_TypeIdx = inp.Read<uint32_t>();
			Test(x.m_TypeIdx < m_Types.size());
		}
	}

	void CompilerPlus::OnSection_Global(Reader& inp)
	{
		auto nCount = inp.Read<uint32_t>();
		m_Globals.resize(nCount);

		for (uint32_t i = 0; i < nCount; i++)
		{
			auto& x = m_Globals[i];

			x.m_Type = inp.Read1();
			x.m_Mutable = inp.Read1();

			Fail(); // TODO: init expresssion
		}
	}

	void CompilerPlus::OnSection_Export(Reader& inp)
	{
		auto nCount = inp.Read<uint32_t>();
		m_Exports.resize(nCount);

		for (uint32_t i = 0; i < nCount; i++)
		{
			auto& x = m_Exports[i];
			x.m_sName.Read(inp);
			x.m_Kind = inp.Read1();
			x.m_Idx = inp.Read<uint32_t>();

			if (!x.m_Kind)
			{
				x.m_Idx -= static_cast<uint32_t>(m_ImportFuncs.size());
				Test(x.m_Idx < m_Functions.size());
			}
		}
	}

	void CompilerPlus::OnSection_Code(Reader& inp)
	{
		auto nCount = inp.Read<uint32_t>();
		Test(nCount == m_Functions.size());

		for (uint32_t i = 0; i < nCount; i++)
		{
			auto& x = m_Functions[i];

			auto nSize = inp.Read<uint32_t>();
			Reader inpFunc;
			inpFunc.m_p0 = inp.Consume(nSize);
			inpFunc.m_p1 = inpFunc.m_p0 + nSize;

			const auto& funcType = m_Types[x.m_TypeIdx];

			for (uint32_t iArg = 0; iArg < funcType.m_Args.n; iArg++)
				x.m_Locals.Add(funcType.m_Args.p[iArg]);

			auto nBlocks = inpFunc.Read<uint32_t>();
			for (uint32_t iBlock = 0; iBlock < nBlocks; iBlock++)
			{
				auto nVarsCount = inpFunc.Read<uint32_t>();
				uint8_t nType = inpFunc.Read1();

				while (nVarsCount--)
					x.m_Locals.Add(nType);
			}

			// the rest is the function body
			x.m_Expression = inpFunc;
		}
	}

	void CompilerPlus::OnSection_Data(Reader& inp)
	{
		uint32_t nCount = inp.Read<uint32_t>();

		for (uint32_t i = 0; i < nCount; i++)
		{
			uint32_t nAddr = inp.Read<uint32_t>();

			Test(Instruction::i32_const == inp.Read1());
			uint32_t nOffset = inp.Read<int32_t>();
			Test(Instruction::end_block == inp.Read1());
			nAddr += nOffset;

			Vec<uint8_t> data;
			data.Read(inp);
			data;

			if (data.n)
			{
				size_t n = nAddr + data.n;
				if (m_Data.size() < n)
					m_Data.resize(n);

				memcpy(&m_Data.front() + nAddr, data.p, data.n);
			}
		}
	}

	void CompilerPlus::OnSection_DataCount(Reader& inp)
	{
		uint32_t nCount = inp.Read<uint32_t>();
		nCount;
	}




	struct Compiler::Context
	{

		Compiler& m_This;
		const uint8_t* m_p0;
		uint32_t m_iFunc = 0;
		Reader m_Code;

		struct Block
		{
			PerType m_Type;
			size_t m_OperandsAtExit;
			uint32_t m_iLabel;
			bool m_Loop = false;
		};

		std::vector<Block> m_Blocks;
		std::vector<uint8_t> m_Operands;
		uint32_t m_WordsOperands = 0;


		Block& get_B() {
			Test(!m_Blocks.empty());
			return m_Blocks.back();
		}

		void Push(uint8_t x) {
			m_Operands.push_back(x);
			m_WordsOperands += Type::Words(x);
		}

		uint8_t Pop() {
			Test(!m_Operands.empty());
			uint8_t ret = m_Operands.back();
			m_Operands.pop_back();

			m_WordsOperands -= Type::Words(ret);
			return ret;
		}

		void Pop(uint8_t nType) {
			uint8_t x = Pop();
			Test(x == nType);
		}

		void TestOperands(const Vec<uint8_t>& v)
		{
			if (!v.n)
				return;

			Test(m_Operands.size() >= v.n);
			Test(!memcmp(&m_Operands.front() + m_Operands.size() - v.n, v.p, sizeof(*v.p) * v.n));
		}

		void BlockOpen(const PerType& tp)
		{
			auto& b = m_Blocks.emplace_back();
			b.m_OperandsAtExit = m_Operands.size();

			if (1 != m_Blocks.size())
			{
				TestOperands(tp.m_Args); // for most outer function block the args are not on the stack
				b.m_OperandsAtExit -= tp.m_Args.n;

				b.m_iLabel = static_cast<uint32_t>(m_This.m_Labels.m_Items.size());
				m_This.m_Labels.m_Items.push_back(0);
			}

			b.m_OperandsAtExit += tp.m_Rets.n;
			b.m_Type = tp;
		}

		void BlockOpen()
		{
			auto nType = m_Code.Read<uint32_t>();
			Test(0x40 == nType);
			PerType tp = { { 0 } };
			BlockOpen(tp);

			m_p0 = nullptr; // don't write
		}

		void TestBlockCanClose()
		{
			Test(m_Operands.size() == get_B().m_OperandsAtExit);
			TestOperands(get_B().m_Type.m_Rets);
		}

		void UpdTopBlockLabel()
		{
			m_This.m_Labels.m_Items[m_Blocks.back().m_iLabel] = static_cast<uint32_t>(m_This.m_Result.size());
		}

		static uint32_t WordsOfVars(const Vec<uint8_t>& v)
		{
			uint32_t nWords = 0;
			for (uint32_t i = 0; i < v.n; i++)
				nWords += Type::Words(v.p[i]);
			return nWords;
		}

		void WriteRet()
		{
			WriteRes(Instruction::ret);

			// stack layout
			// ...
			// args
			// retaddr (4 bytes)
			// locals
			// retval

			// to allow for correct return we need to provide the following:

			uint32_t nWordsLocal = m_This.m_Functions[m_iFunc].m_Locals.get_SizeWords(); // sizeof(args) + sizeof(locals)

			const auto& tp = get_B().m_Type;
			uint32_t nWordsArgs = WordsOfVars(tp.m_Args);

			WriteResU(WordsOfVars(tp.m_Rets)); // sizeof(retval)
			WriteResU(nWordsLocal - nWordsArgs); // sizeof(locals), excluding args
			WriteResU(nWordsArgs);
		}

		void BlockClose()
		{
			TestBlockCanClose();

			if (1 == m_Blocks.size())
				WriteRet(); // end of function
			else
			{
				if (!m_Blocks.back().m_Loop)
					UpdTopBlockLabel();
			}

			m_Blocks.pop_back();

			m_p0 = nullptr; // don't write
		}

		void PutLabelTrg(uint32_t iLabel)
		{
			auto& lbl = m_This.m_Labels.m_Targets.emplace_back();
			lbl.m_iItem = iLabel;
			lbl.m_Pos = static_cast<uint32_t>(m_This.m_Result.size());

			Word n = to_wasm(iLabel);
			WriteRes(reinterpret_cast<uint8_t*>(&n), sizeof(n));
		}

		void OnBranch()
		{
			auto nLabel = m_Code.Read<uint32_t>();
			Test(nLabel + 1 < m_Blocks.size());
			assert(nLabel < m_This.m_Labels.m_Items.size());

			auto& b = get_B();
			if (b.m_Loop)
			{
				assert(m_Blocks.size() > 1); // function block can't be loop

				size_t n = b.m_OperandsAtExit + b.m_Type.m_Args.n - b.m_Type.m_Rets.n;
				Test(m_Operands.size() == n);
				TestOperands(b.m_Type.m_Args);
			}
			else
			{
				TestBlockCanClose();
			}

			WriteRes(*m_p0); // opcode
			m_p0 = nullptr;

			PutLabelTrg(m_Blocks[m_Blocks.size() - (nLabel + 1)].m_iLabel);
		}

		uint8_t OnLocalVar()
		{
			WriteInstruction();

			const auto& f = m_This.m_Functions[m_iFunc];
			auto iVar = m_Code.Read<uint32_t>();

			Test(iVar < f.m_Locals.m_v.size());
			const auto var = f.m_Locals.m_v[iVar];


			// add type and position of the local wrt current stack

			// stack layout
			// ...
			// args
			// retaddr (4 bytes)
			// locals
			// current operand stack

			uint32_t nOffsWords = m_WordsOperands + f.m_Locals.get_SizeWords() - var.m_PosWords;

			const auto& fType = m_This.m_Types[f.m_TypeIdx];
			if (iVar < fType.m_Args.n)
				nOffsWords++; // retaddr

			assert(var.m_Type - Type::s_Base < sizeof(Word));

			uint32_t nValue = (nOffsWords * sizeof(Word)) | (var.m_Type - Type::s_Base);
			WriteResU(nValue);

			return var.m_Type;
		}

		void On_local_get() {
			Push(OnLocalVar());
		}
		void On_local_set() {
			Pop(OnLocalVar());
		}

		void On_local_tee() {
			auto nType = OnLocalVar();
			Pop(nType);
			Push(nType);
		}

		void On_drop() {
			WriteInstruction();
			WriteRes(Pop());
		}

		void On_select() {
			WriteInstruction();

			Pop(Type::i32);
			uint8_t nType = Pop();
			Pop(nType); // must be the same
			Push(nType); // result

			WriteRes(nType);
		}

		uint32_t ReadOffsAndPadding()
		{
			auto nPad = m_Code.Read<uint32_t>();
			nPad;
			return m_Code.Read<uint32_t>();
		}

		void On_i32_load() {
			ReadOffsAndPadding();
			Pop(Type::i32);
			Push(Type::i32);
		}

		void On_i64_load() {
			ReadOffsAndPadding();
			Pop(Type::i32);
			Push(Type::i64);
		}

		void On_i32_load8() {
			On_i32_load();
		}

		void On_i32_load8_u() {
			On_i32_load8();
		}
		void On_i32_load8_s() {
			On_i32_load8();
		}

		void On_i32_store() {
			ReadOffsAndPadding();
			Pop(Type::i32);
			Pop(Type::i32);
		}

		void On_i32_store8() {
			On_i32_store();
		}

		void On_i64_store() {
			ReadOffsAndPadding();
			Pop(Type::i64);
			Pop(Type::i32);
		}

		void On_block() {
			BlockOpen();
		}

		void On_loop() {
			BlockOpen();
			get_B().m_Loop = true;
			UpdTopBlockLabel();
		}

		void On_nop() {
			m_p0 = nullptr;
		}

		void On_end_block() {
			BlockClose();
		}

		void On_br() {
			OnBranch();
		}

		void On_br_if() {
			Pop(Type::i32); // conditional
			OnBranch();
		}

		void On_i32_const() {
			auto val = m_Code.Read<int32_t>();
			val;
			Push(Type::i32);
		}

		void On_i64_const() {
			auto val = m_Code.Read<int64_t>();
			val;
			Push(Type::i64);
		}

		void OnGlobal(bool bGet)
		{
			auto iVar = m_Code.Read<uint32_t>();
			bool bImported = (iVar < m_This.m_ImportGlobals.size());
			if (bImported)
			{
				const auto& x = m_This.m_ImportGlobals[iVar];

				if (bGet) {
					Push(x.m_Type);
					WriteRes(Instruction::global_get_imp);
				}
				else {
					Pop(x.m_Type);
					WriteRes(Instruction::global_set_imp);
				}

				WriteResU(x.m_Binding);
			}
			else
			{
				Fail(); // not supported atm
			}

			m_p0 = nullptr; // don't write
		}

		void On_global_get()
		{
			OnGlobal(true);
		}

		void On_global_set()
		{
			OnGlobal(false);
		}

		void On_call()
		{
			auto iFunc = m_Code.Read<uint32_t>();

			bool bImported = (iFunc < m_This.m_ImportFuncs.size());
			if (!bImported)
			{
				iFunc -= static_cast<uint32_t>(m_This.m_ImportFuncs.size());
				Test(iFunc < m_This.m_Functions.size());
			}

			uint32_t iTypeIdx = bImported ? m_This.m_ImportFuncs[iFunc].m_TypeIdx : m_This.m_Functions[iFunc].m_TypeIdx;
			const auto& tp = m_This.m_Types[iTypeIdx];

			for (uint32_t i = tp.m_Args.n; i--; )
				Pop(tp.m_Args.p[i]);

			for (uint32_t i = 0; i < tp.m_Rets.n; i++)
				Push(tp.m_Rets.p[i]);

			m_p0 = nullptr; // don't write

			if (bImported)
			{
				WriteRes(Instruction::call_ext);
				WriteResU(m_This.m_ImportFuncs[iFunc].m_Binding);
			}
			else
			{
				WriteRes(Instruction::call);
				PutLabelTrg(iFunc);
			}
		}

		void CompileFunc();


		Context(Compiler& x) :m_This(x) {}

		void WriteRes(uint8_t x) {
			m_This.m_Result.push_back(x);
		}

		void Write(Instruction x) {
			WriteRes(static_cast<uint8_t>(x));
		}

		void WriteRes(const uint8_t* p, uint32_t n);
		void WriteResU(uint64_t);
		void WriteResS(int64_t);

		void WriteInstruction();
	};

	void Compiler::Context::WriteInstruction()
	{
		if (m_p0)
		{
			WriteRes(m_p0, static_cast<uint32_t>(m_Code.m_p0 - m_p0));
			m_p0 = nullptr;
		}
	}

	void Compiler::Context::WriteRes(const uint8_t* p, uint32_t n)
	{
		if (n)
		{
			size_t n0 = m_This.m_Result.size();
			m_This.m_Result.resize(n0 + n);
			memcpy(&m_This.m_Result.front() + n0, p, n);
		}
	}

	void Compiler::Context::WriteResU(uint64_t x)
	{
		while (true)
		{
			uint8_t n = static_cast<uint8_t>(x);
			x >>= 7;

			if (!x)
			{
				assert(!(n & 0x80));
				WriteRes(n);
				break;
			}

			WriteRes(n | 0x80);
		}
	}

	void Compiler::Context::WriteResS(int64_t x)
	{
		while (true)
		{
			uint8_t n = static_cast<uint8_t>(x);
			x >>= 6; // sign bit is propagated

			if (!x)
			{
				assert((n & 0xC0) == 0);
				WriteRes(n);
				break;
			}

			if (-1 == x)
			{
				assert((n & 0xC0) == 0xC0);
				WriteRes(n & ~0x80); // sign bit must remain
				break;
			}

			WriteRes(n | 0x80);
			x >>= 1;
		}
	}



	void Compiler::Build()
	{
		for (uint32_t i = 0; i < m_Functions.size(); i++)
		{
			Context ctx(*this);
			m_Labels.m_Items[i] = static_cast<uint32_t>(m_Result.size());
			ctx.m_iFunc = i;
			ctx.CompileFunc();
		}

		for (uint32_t i = 0; i < m_Labels.m_Targets.size(); i++)
		{
			auto& trg = m_Labels.m_Targets[i];

			to_wasm(&m_Result.front() + trg.m_Pos, m_Labels.m_Items[trg.m_iItem]);
		}

	}



	void Compiler::Context::CompileFunc()
	{
		auto& func = m_This.m_Functions[m_iFunc];
		m_Code = func.m_Expression;

		const auto& tp = m_This.m_Types[func.m_TypeIdx];
		BlockOpen(tp);

		typedef Instruction I;

		const auto& v = func.m_Locals.m_v;
		assert(tp.m_Args.n <= v.size());
		if (tp.m_Args.n < v.size())
		{
			uint32_t nLocalVarsSize = func.m_Locals.get_SizeWords() - v[tp.m_Args.n].m_PosWords;

			WriteRes(I::prolog);
			WriteResU(nLocalVarsSize);
		}

		for (uint32_t nLine = 0; !m_Blocks.empty(); nLine++)
		{
			nLine; // for dbg
			m_p0 = m_Code.m_p0;

			I nInstruction = (I) m_Code.Read1();

			switch (nInstruction)
			{
#define THE_MACRO(id, name) \
			case I::name: \
				On_##name(); \
				break;

			WasmInstructions_CustomPorted(THE_MACRO)
			WasmInstructions_NotPorted(THE_MACRO)
#undef THE_MACRO


#define THE_MACRO_ID32(name, id32, id64) case id32:
#define THE_MACRO_ID64(name, id32, id64) case id64:

			WasmInstructions_unop_Polymorphic_32(THE_MACRO_ID32)
			{
				Pop(Type::i32);
				Push(Type::i32);

			} break;

			WasmInstructions_unop_Polymorphic_32(THE_MACRO_ID64)
			{
				Pop(Type::i64);
				Push(Type::i32);

			} break;

			WasmInstructions_binop_Polymorphic_32(THE_MACRO_ID32)
			WasmInstructions_binop_Polymorphic_x(THE_MACRO_ID32)
			{
				Pop(Type::i32);
				Pop(Type::i32);
				Push(Type::i32);

			} break;

			WasmInstructions_binop_Polymorphic_32(THE_MACRO_ID64)
			{
				Pop(Type::i64);
				Pop(Type::i64);
				Push(Type::i32);

			} break;

			WasmInstructions_binop_Polymorphic_x(THE_MACRO_ID64)
			{
				Pop(Type::i64);
				Pop(Type::i64);
				Push(Type::i64);

			} break;

#undef THE_MACRO_ID32
#undef THE_MACRO_ID64

			default:
				Fail();
			}

			WriteInstruction(); // unless already written
		}

		Test(m_Code.m_p0 == m_Code.m_p1);

	}




	/////////////////////////////////////////////
	// Processor
	Word Processor::Stack::Pop1()
	{
		Test(m_Pos);
		return m_pPtr[--m_Pos];
	}

	void Processor::Stack::Push1(const Word& x)
	{
		Test(m_Pos < m_Size);
		m_pPtr[m_Pos++] = x;
	}

	template <> Word Processor::Stack::Pop()
	{
		return Pop1();
	}

	template <> void Processor::Stack::Push<Word>(const Word& x)
	{
		Push1(x);
	}

	// for well-formed wasm program we don't need to care about multi-word types bits words order, since there should be no type mixing (i.e. push as uint64, pop as uint32).
	// But the attacker may violate this rule, and cause different behavior on different machines. So we do the proper conversion
	template <> uint64_t Processor::Stack::Pop()
	{
		uint64_t ret = Pop1(); // loword
		return ret | static_cast<uint64_t>(Pop1()) << 32; // hiword
	}

	template <> void Processor::Stack::Push(const uint64_t& x)
	{
		Push1(static_cast<Word>(x >> 32)); // hiword
		Push1(static_cast<Word>(x)); // loword
	}

	struct ProcessorPlus
		:public Processor
	{

#define THE_MACRO_unop(name) \
		template <typename TOut, typename TIn> \
		void On_##name() \
		{ \
			m_Stack.Push<TOut>(Eval_##name<TOut, TIn>(m_Stack.Pop<TIn>())); \
		}

#define THE_MACRO_binop(name) \
		template <typename TOut, typename TIn> \
		void On_##name() \
		{ \
			TIn b = m_Stack.Pop<TIn>(); \
			TIn a = m_Stack.Pop<TIn>(); \
			m_Stack.Push<TOut>(Eval_##name<TOut, TIn>(a, b)); \
		}


#define THE_MACRO(name, id32, id64) THE_MACRO_unop(name)
		WasmInstructions_unop_Polymorphic_32(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(name, id32, id64) THE_MACRO_binop(name)
		WasmInstructions_binop_Polymorphic_32(THE_MACRO)
		WasmInstructions_binop_Polymorphic_x(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(id, name) void On_##name();
		WasmInstructions_CustomPorted(THE_MACRO)
		WasmInstructions_Proprietary(THE_MACRO)
#undef THE_MACRO

#define UNOP(name) template <typename TOut, typename TIn> static TOut Eval_##name(TIn x)
#define BINOP(name) template <typename TOut, typename TIn> static TOut Eval_##name(TIn a, TIn b)

		UNOP(eqz) { return x == 0; }
		BINOP(eq) { return a == b; }
		BINOP(ne) { return a != b; }
		BINOP(lt_s) { return Type::SignedFrom(a) < Type::SignedFrom(b); }
		BINOP(gt_s) { return Type::SignedFrom(a) > Type::SignedFrom(b); }
		BINOP(le_s) { return Type::SignedFrom(a) <= Type::SignedFrom(b); }
		BINOP(ge_s) { return Type::SignedFrom(a) >= Type::SignedFrom(b); }
		BINOP(lt_u) { return a < b; }
		BINOP(gt_u) { return a > b; }
		BINOP(le_u) { return a <= b; }
		BINOP(ge_u) { return a >= b; }
		BINOP(add) { return a + b; }
		BINOP(sub) { return a - b; }
		BINOP(mul) { return a * b; }
		BINOP(div_s) { Test(b);  return Type::SignedFrom(a) / Type::SignedFrom(b); }
		BINOP(div_u) { Test(b);  return a / b; }
		BINOP(rem_s) { Test(b);  return Type::SignedFrom(a) % Type::SignedFrom(b); }
		BINOP(rem_u) { Test(b);  return a % b; }
		BINOP(and) { return a & b; }
		BINOP(or) { return a | b; }
		BINOP(xor) { return a ^ b; }
		BINOP(shl) { Test(b < (sizeof(a) * 8)); return a << b; }
		BINOP(shr_s) { Test(b < (sizeof(a) * 8)); return Type::SignedFrom(a) >> b; }
		BINOP(shr_u) { Test(b < (sizeof(a) * 8)); return a >> b; }
		BINOP(rotl) { Test(b < (sizeof(a) * 8)); if (!b) return a; return (a << b) | (a >> ((sizeof(a) * 8) - b)); }
		BINOP(rotr) { Test(b < (sizeof(a) * 8)); if (!b) return a; return (a >> b) | (a << ((sizeof(a) * 8) - b)); }


		Word ReadAddr()
		{
			return from_wasm<Word>(m_Instruction.Consume(sizeof(Word)));
		}

		void OnLocal(bool bSet, bool bGet)
		{
			uint32_t nOffset = m_Instruction.Read<uint32_t>();

			uint8_t nType = Type::s_Base + static_cast<uint8_t>((sizeof(Word) - 1) & (nOffset - Type::s_Base));
			uint8_t nWords = Type::Words(nType);

			nOffset /= sizeof(Word);

			Test((nOffset >= nWords) && (nOffset <= m_Stack.m_Pos));

			uint32_t* pSrc = m_Stack.m_pPtr + m_Stack.m_Pos;
			uint32_t* pDst = pSrc - nOffset;

			if (!bSet)
			{
				std::swap(pDst, pSrc);
				m_Stack.m_Pos += nWords;
				Test(m_Stack.m_Pos <= m_Stack.m_Size);
			}
			else
			{
				pSrc -= nWords;

				if (!bGet)
					m_Stack.m_Pos -= nWords;
			}

			for (uint32_t i = 0; i < nWords; i++)
				pDst[i] = pSrc[i];
		}

		void OnGlobal(bool bGet)
		{
			Fail();
		}

		void OnGlobalImp(bool bGet)
		{
			auto iVar = m_Instruction.Read<uint32_t>();
			OnGlobalVar(iVar, bGet);
		}

		uint8_t* MemArg(uint32_t nSize)
		{
			auto nPad = m_Instruction.Read<Word>(); nPad;
			auto nOffs = m_Instruction.Read<Word>();
			nOffs += m_Stack.Pop1();

			return get_LinearAddr(nOffs, nSize);
		}



		void RunOncePlus()
		{
			typedef Instruction I;
			I nInstruction = (I) m_Instruction.Read1();

				if (m_pDbg)
					*m_pDbg << "ip=" << uintBigFrom(get_Ip()) << ", sp=" << uintBigFrom(m_Stack.m_Pos) << ' ';

			switch (nInstruction)
			{
#define THE_CASE(name) case I::name: if (m_pDbg) (*m_pDbg) << #name << std::endl;

#define THE_MACRO(id, name) THE_CASE(name) On_##name(); break;
			WasmInstructions_CustomPorted(THE_MACRO)
			WasmInstructions_Proprietary(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(name, id32, id64) \
			THE_CASE(i32_##name) On_##name<uint32_t, uint32_t>(); break; \
			THE_CASE(i64_##name) On_##name<uint32_t, uint64_t>(); break;

			WasmInstructions_unop_Polymorphic_32(THE_MACRO)
			WasmInstructions_binop_Polymorphic_32(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(name, id32, id64) \
			THE_CASE(i32_##name) On_##name<uint32_t, uint32_t>(); break; \
			THE_CASE(i64_##name) On_##name<uint64_t, uint64_t>(); break;

			WasmInstructions_binop_Polymorphic_x(THE_MACRO)
#undef THE_MACRO

			default:
				Fail();
			}

		}

		Word get_Ip() const
		{
			return static_cast<Word>(m_Instruction.m_p0 - (const uint8_t*) m_Code.p);
		}

	};

	uint8_t* Processor::get_LinearAddr(uint32_t nOffset, uint32_t nSize)
	{
		Test(!(MemoryType::Mask & nSize));
		const Blob* pMem = &m_Data;

		switch (MemoryType::Mask & nOffset)
		{
		case MemoryType::Global:
			pMem = &m_LinearMem;
			break;

		case MemoryType::Data:
			break;

		default:
			Fail();
		}

		nOffset &= ~MemoryType::Mask;


		nSize += nOffset;
		assert(nSize >= nOffset); // can't overflow, hi-order bits are zero in both
		Test(nSize <= pMem->n);
		return reinterpret_cast<uint8_t*>(Cast::NotConst(pMem->p)) + nOffset;
	}

	void Processor::RunOnce()
	{
		auto& p = Cast::Up<ProcessorPlus>(*this);
		static_assert(sizeof(p) == sizeof(*this));
		p.RunOncePlus();
	}

	void Processor::InvokeExt(uint32_t)
	{
		Fail(); // unresolved binding
	}

	void Processor::OnGlobalVar(uint32_t iVar, bool bGet)
	{
		Fail();
	}

	void Processor::Jmp(uint32_t ip)
	{
		Test(ip < m_Code.n);

		m_Instruction.m_p0 = reinterpret_cast<const uint8_t*>(m_Code.p) + ip;
		m_Instruction.m_p1 = m_Instruction.m_p0 + m_Code.n - ip;
	}

	void ProcessorPlus::On_local_get()
	{
		OnLocal(false, true);
	}

	void ProcessorPlus::On_local_set()
	{
		OnLocal(true, false);
	}

	void ProcessorPlus::On_local_tee()
	{
		OnLocal(true, true);
	}

	void ProcessorPlus::On_global_get()
	{
		OnGlobal(true);
	}

	void ProcessorPlus::On_global_set()
	{
		OnGlobal(false);
	}

	void ProcessorPlus::On_global_get_imp()
	{
		OnGlobalImp(true);
	}

	void ProcessorPlus::On_global_set_imp()
	{
		OnGlobalImp(false);
	}

	void ProcessorPlus::On_drop()
	{
		uint32_t nWords = Type::Words(m_Instruction.Read1());
		Test(m_Stack.m_Pos >= nWords);
		m_Stack.m_Pos -= nWords;
	}

	void ProcessorPlus::On_select()
	{
		uint32_t nWords = Type::Words(m_Instruction.Read1());
		auto nSel = m_Stack.Pop1();

		Test(m_Stack.m_Pos >= (nWords << 1)); // must be at least 2 such operands
		m_Stack.m_Pos -= nWords;

		if (!nSel)
		{
			for (uint32_t i = 0; i < nWords; i++)
				m_Stack.m_pPtr[m_Stack.m_Pos + i - nWords] = m_Stack.m_pPtr[m_Stack.m_Pos + i];
		}
	}

	void ProcessorPlus::On_i32_load()
	{
		auto n = from_wasm<uint32_t>(MemArg(sizeof(uint32_t)));
		m_Stack.Push1(n);
	}

	void ProcessorPlus::On_i64_load()
	{
		auto n = from_wasm<uint64_t>(MemArg(sizeof(uint64_t)));
		m_Stack.Push(n);
	}

	void ProcessorPlus::On_i32_load8_u()
	{
		uint32_t val = *MemArg(1);
		m_Stack.Push1(val);
	}

	void ProcessorPlus::On_i32_load8_s()
	{
		char ch = *MemArg(1);
		int32_t val = ch; // promoted w.r.t. sign
		m_Stack.Push1(val);
	}

	void ProcessorPlus::On_i32_store()
	{
		Word val = m_Stack.Pop1();
		to_wasm(MemArg(sizeof(val)), val);
	}

	void ProcessorPlus::On_i64_store()
	{
		auto val = m_Stack.Pop<uint64_t>();
		to_wasm(MemArg(sizeof(val)), val);
	}

	void ProcessorPlus::On_i32_store8()
	{
		Word val = m_Stack.Pop1();
		*MemArg(1) = static_cast<uint8_t>(val);
	}

	void ProcessorPlus::On_br()
	{
		Jmp(ReadAddr());
	}

	void ProcessorPlus::On_br_if()
	{
		Word addr = ReadAddr();
		if (m_Stack.Pop1())
			Jmp(addr);
	}

	void ProcessorPlus::On_call()
	{
		Word nAddr = ReadAddr();
		Word nRetAddr = get_Ip();
		m_Stack.Push1(nRetAddr);
		Jmp(nAddr);
	}

	void ProcessorPlus::On_call_ext()
	{
		InvokeExt(m_Instruction.Read<uint32_t>());
	}


	void ProcessorPlus::On_i32_const()
	{
		m_Stack.Push<uint32_t>(m_Instruction.Read<int32_t>());
	}

	void ProcessorPlus::On_i64_const()
	{
		m_Stack.Push<uint64_t>(m_Instruction.Read<int64_t>());
	}

	void ProcessorPlus::On_prolog()
	{
		auto nWords = m_Instruction.Read<uint32_t>();
		while (nWords--)
			m_Stack.Push1(0); // for more safety - zero-init locals. This way we don't need initial stack initialization 
	}

	void ProcessorPlus::On_ret()
	{
		auto nRets = m_Instruction.Read<uint32_t>();
		auto nLocals = m_Instruction.Read<uint32_t>();
		auto nArgs = m_Instruction.Read<uint32_t>();

		// stack layout
		// ...
		// args
		// retaddr (4 bytes)
		// locals
		// retval

		uint32_t nPosRetSrc = m_Stack.m_Pos - nRets;
		Test(nPosRetSrc <= m_Stack.m_Pos);

		uint32_t nPosAddr = nPosRetSrc - (nLocals + 1);
		Test(nPosAddr < nPosRetSrc);

		uint32_t nPosRetDst = nPosAddr - nArgs;
		Test(nPosRetDst <= nPosAddr);

		Word nRetAddr = m_Stack.m_pPtr[nPosAddr];

		for (uint32_t i = 0; i < nRets; i++)
			m_Stack.m_pPtr[nPosRetDst + i] = m_Stack.m_pPtr[nPosRetSrc + i];


		m_Stack.m_Pos = nPosRetDst + nRets;
		Jmp(nRetAddr);
	}





} // namespace Wasm
} // namespace beam
