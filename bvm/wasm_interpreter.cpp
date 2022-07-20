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
#include "../core/uintBig.h"
#include <sstream>

#define MY_TOKENIZE2(a, b) a##b
#define MY_TOKENIZE1(a, b) MY_TOKENIZE2(a, b)

namespace beam {
namespace Wasm {

	thread_local Checkpoint* Checkpoint::s_pTop = nullptr;

	Checkpoint::Checkpoint()
	{
		m_pNext = s_pTop;
		s_pTop = this;
	}

	Checkpoint::~Checkpoint()
	{
		s_pTop = m_pNext;
	}

	uint32_t Checkpoint::DumpAll(std::ostream& os)
	{
		uint32_t ret = 0;
		for (Checkpoint* p = s_pTop; p; p = p->m_pNext)
		{
			os << " <- ";
			p->Dump(os);

			if (!ret)
				ret = p->get_Type();
		}
		return ret;
	}

	void CheckpointTxt::Dump(std::ostream& os) {
		os << m_sz;
	}

	void Fail()
	{
		Fail("Error");
	}

	void Fail(const char* sz)
	{
		std::ostringstream os;
		os << sz << ": ";

		uint32_t nType = Checkpoint::DumpAll(os);

		Exc exc(os.str());
		exc.m_Type = nType;

		throw exc;
	}

	void Test(bool b) {
		if (!b)
			Fail();
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
		constexpr unsigned int nBitsMax = sizeof(ret) * 8;

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
					{
						// Attention: Bug workaround.
						// According to the standard we must pad the remaining bits of the result with 1's.
						// However this should only be done if there are bits left. That is, only if nShift is lesser than the number of result bits.
						// Original code didn't take care of this.

						if (nShift >= nBitsMax)
						{
							m_ModeTriggered = true;

							constexpr uint32_t nBitsJustFed = ((nBitsMax - 1) % 7) + 1; // how many bits were just fed into result
							static_assert(nBitsJustFed < 6); // the 0x40 bit was not fed. It's unnecessary

							switch (m_Mode)
							{
							case Mode::AutoWorkAround:
								n &= ~0x40; // safe to remove this flag, it's redundant and didn't have to appear at all
								Cast::NotConst(m_p0[-1]) = n; // replace it back!
								break;

							case Mode::Emulate_x86:
								// emulate the unfixed behavior. On x86 bitshift is effective modulo size of operand
								ret |= (~static_cast<T>(0) << (nShift % nBitsMax));
								break;

							case Mode::Standard:
								// Standard behavior, ignore padding
								break;

							default:
								assert(false);
								// no break;
							case Mode::Restrict:
								Fail("Conflicting flags");
							}
						}
						else
							// standard padding
							ret |= (~static_cast<T>(0) << nShift);
					}
				}
				break;
			}

			Test(nShift < nBitsMax);
		}

		return ret;
	}

	/////////////////////////////////////////////
	// Common

	struct Type
		:public TypeCode
	{
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

		template <uint8_t nType> struct Code2Type;
		template <typename T, bool bSigned> struct ToFlexible;

		template <typename TIn>
		static typename ToFlexible<TIn, true>::T SignedFrom(TIn x) { return x; }

		template <typename TOut, typename TIn>
		static TOut Extend(TIn x)
		{
			// promote w.r.t. sign
			static_assert(sizeof(TOut) >= sizeof(TIn));
			return (typename ToFlexible<TOut, std::numeric_limits<TIn>::is_signed>::T) x;
		}
	};

	template <> struct Type::Code2Type<Type::i32> { typedef uint32_t T; };
	template <> struct Type::Code2Type<Type::i64> { typedef uint64_t T; };

	template <> struct Type::ToFlexible<uint8_t, true> { typedef int8_t T; };
	template <> struct Type::ToFlexible<int8_t, true> { typedef int8_t T; };
	template <> struct Type::ToFlexible<uint8_t, false> { typedef uint8_t T; };
	template <> struct Type::ToFlexible<int8_t, false> { typedef uint8_t T; };

	template <> struct Type::ToFlexible<uint16_t, true> { typedef int16_t T; };
	template <> struct Type::ToFlexible<int16_t, true> { typedef int16_t T; };
	template <> struct Type::ToFlexible<uint16_t, false> { typedef uint16_t T; };
	template <> struct Type::ToFlexible<int16_t, false> { typedef uint16_t T; };

	template <> struct Type::ToFlexible<uint32_t, true> { typedef int32_t T; };
	template <> struct Type::ToFlexible<int32_t, true> { typedef int32_t T; };
	template <> struct Type::ToFlexible<uint32_t, false> { typedef uint32_t T; };
	template <> struct Type::ToFlexible<int32_t, false> { typedef uint32_t T; };

	template <> struct Type::ToFlexible<uint64_t, true> { typedef int64_t T; };
	template <> struct Type::ToFlexible<int64_t, true> { typedef int64_t T; };
	template <> struct Type::ToFlexible<uint64_t, false> { typedef uint64_t T; };
	template <> struct Type::ToFlexible<int64_t, false> { typedef uint64_t T; };


	template <typename T>
	T Reader::Read()
	{
		return ReadInternal<typename Type::ToFlexible<T, false>::T, std::numeric_limits<T>::is_signed>();
	}


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

#define WasmInstructions_Store(macro) \
	macro(0x36, i32, store  , uint32_t) \
	macro(0x37, i64, store  , uint64_t) \
	macro(0x3A, i32, store8 , uint8_t) \
	macro(0x3B, i32, store16, uint16_t) \
	macro(0x3C, i64, store8 , uint8_t) \
	macro(0x3D, i64, store16, uint16_t) \
	macro(0x3E, i64, store32, uint32_t) \

#define WasmInstructions_Load(macro) \
	macro(0x28, i32, load    , uint32_t) \
	macro(0x29, i64, load    , uint64_t) \
	macro(0x2C, i32, load8_s , int8_t) \
	macro(0x2D, i32, load8_u , uint8_t) \
	macro(0x2E, i32, load16_s, int16_t) \
	macro(0x2F, i32, load16_u, uint16_t) \
	macro(0x30, i64, load8_s , int8_t) \
	macro(0x31, i64, load8_u , uint8_t) \
	macro(0x32, i64, load16_s, int16_t) \
	macro(0x33, i64, load16_u, uint16_t) \
	macro(0x34, i64, load32_s, int32_t) \
	macro(0x35, i64, load32_u, uint32_t) \


#define WasmInstructions_CustomPorted(macro) \
	macro(0x1A, drop) \
	macro(0x1B, select) \
	macro(0x20, local_get) \
	macro(0x21, local_set) \
	macro(0x22, local_tee) \
	macro(0x23, global_get) \
	macro(0x24, global_set) \
	macro(0xA7, i32_wrap_i64) \
	macro(0xAC, i64_extend_i32_s) \
	macro(0xAD, i64_extend_i32_u) \
	macro(0x10, call) \
	macro(0x11, call_indirect) \
	macro(0x0C, br) \
	macro(0x0D, br_if) \
	macro(0x0E, br_table) \
	macro(0x0F, ret) \
	macro(0x41, i32_const) \
	macro(0x42, i64_const) \

#define WasmInstructions_Proprietary(macro) \
	macro(0xf1, prolog) \
	macro(0xf3, call_ext) \
	macro(0xf8, global_get_imp) \
	macro(0xf9, global_set_imp) \

#define WasmInstructions_NotPorted(macro) \
	macro(0x00, unreachable) \
	macro(0x01, nop) \
	macro(0x02, block) \
	macro(0x03, loop) \
	macro(0x0B, end_block) \

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

#define THE_MACRO(id, type, name, tmem) type##_##name = id,
		WasmInstructions_Store(THE_MACRO)
		WasmInstructions_Load(THE_MACRO)
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
		macro(0, Custom) \
		macro(1, Type) \
		macro(2, Import) \
		macro(3, Funcs) \
		macro(4, Table) \
		macro(6, Global) \
		macro(7, Export) \
		macro(9, Element) \
		macro(10, Code) \
		macro(11, Data) \
		macro(12, DataCount) \

#define THE_MACRO(id, name) void OnSection_##name(Reader&);
		WasmParserSections(THE_MACRO)
#undef THE_MACRO

		void ParsePlus(Reader);

		int32_t ReadI32Initializer(Reader& inp)
		{
			// initialization expression for the global variable. Ignore it, we don't really support globals, it's just needed for a non-imported stack pointer worakround.
			Test(Instruction::i32_const == inp.Read1());
			auto val = inp.Read<int32_t>();
			Test(Instruction::end_block == inp.Read1());
			return val;
		}
	};

	void Compiler::Parse(const Reader& inp)
	{
		auto& c = Cast::Up<CompilerPlus>(*this);
		static_assert(sizeof(c) == sizeof(*this));
		c.ParsePlus(inp);

	}

	void CompilerPlus::ParsePlus(Reader inp)
	{
		CheckpointTxt cp("wasm/parse");

		static const uint8_t pMagic[] = { 0, 'a', 's', 'm' };
		Test(!memcmp(pMagic, inp.Consume(sizeof(pMagic)), sizeof(pMagic)));

		static const uint8_t pVer[] = { 1, 0, 0, 0 };
		Test(!memcmp(pVer, inp.Consume(sizeof(pVer)), sizeof(pVer)));

		m_cmplData0 = 0;

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
	}

#define STR_MATCH(vec, txt) ((vec.n == sizeof(txt)-1) && !memcmp(vec.p, txt, sizeof(txt)-1))

	void CompilerPlus::OnSection_Custom(Reader& inp)
	{
		Vec<char> sName;
		sName.Read(inp);

		if (STR_MATCH(sName, "name"))
		{
			auto nType = inp.Read1();
			auto nSize = inp.Read<uint32_t>();

			Reader r;
			r.m_p0 = inp.Consume(nSize);
			r.m_p1 = r.m_p0 + nSize;

			if (1 == nType)
			{
				// function names
				auto nCount = r.Read<uint32_t>();
				for (uint32_t i = 0; i < nCount; i++)
				{
					auto iFunc = r.Read<uint32_t>();
					sName.Read(r);

					// the convention looks to be: first all the imported names, then the internal
					iFunc -= (uint32_t) m_ImportFuncs.size();

					if (iFunc < m_Functions.size())
						m_Functions[iFunc].m_sName = sName;
				}
			}
		}

		inp.m_p0 = inp.m_p1; // skip the rest
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

	void TestBinding(const Compiler::PerImport& x)
	{
		Test(x.m_Binding != static_cast<uint32_t>(-1));
	}

	void CompilerPlus::OnSection_Import(Reader& inp)
	{
		auto nCount = inp.Read<uint32_t>();

		for (uint32_t i = 0; i < nCount; i++)
		{
			PerImport pi;
			pi.m_sMod.Read(inp);
			pi.m_sName.Read(inp);
			pi.m_Binding = static_cast<uint32_t>(-1);

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
				[[maybe_unused]] uint32_t lim;
				lim = inp.Read<uint32_t>();
				lim = inp.Read<uint32_t>();
				//lim;

				[[maybe_unused]] uint8_t nElemType = inp.Read1();
				//nElemType;


			} break;

			case 2: {

				// mem type. Ignore.
				[[maybe_unused]] uint32_t lim;
				lim = inp.Read<uint32_t>();
				lim = inp.Read<uint32_t>();
				//lim;
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
			ZeroObject(x.m_sName);

			x.m_TypeIdx = inp.Read<uint32_t>();
			Test(x.m_TypeIdx < m_Types.size());
		}
	}

	void CompilerPlus::OnSection_Table(Reader& inp)
	{
		auto nCount = inp.Read<uint32_t>();

		for (uint32_t i = 0; i < nCount; i++)
		{
			auto tblType = inp.Read<uint32_t>(); 
			Test(0x70 == tblType); // must by anyfunc

			auto nFlags = inp.Read<uint32_t>();
			[[maybe_unused]] auto nLimitMin = inp.Read<uint32_t>();
			//nLimitMin;
			[[maybe_unused]] auto nLimitMax = nLimitMin;

			if (1 & nFlags)
				nLimitMax = inp.Read<uint32_t>(); // max

			//nLimitMax;
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
			x.m_IsVariable = inp.Read1();

			// initialization expression for the global variable. Ignore it, we don't really support globals, it's just needed for a non-imported stack pointer worakround.
			ReadI32Initializer(inp);
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

	void CompilerPlus::OnSection_Element(Reader& inp)
	{
		auto nCount = inp.Read<uint32_t>();
		Test(1 == nCount);

		for (uint32_t i = 0; i < nCount; i++)
		{
			auto iTable = inp.Read<uint32_t>();
			Test(0 == iTable);
			uint32_t nOffset = ReadI32Initializer(inp);
			Test(1 == nOffset); // seems irrelevant

			auto nFuncs = inp.Read<uint32_t>();
			m_IndirectFuncs.m_vec.resize(nFuncs);

			for (uint32_t iFunc = 0; iFunc < nFuncs; iFunc++)
			{
				auto val = inp.Read<uint32_t>();
				val -= static_cast<uint32_t>(m_ImportFuncs.size());
				Test(val < m_Functions.size());

				m_IndirectFuncs.m_vec[iFunc] = val;
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

			uint32_t nOffset = ReadI32Initializer(inp);
			nAddr += nOffset;

			Vec<uint8_t> data;
			data.Read(inp);

			if (i) {
				// assume data blocks are sorted w.r.t. their virtual offset, and no overlap
				Test(nAddr >= m_cmplData0 + m_Data.size());
			}
			else
				m_cmplData0 = nAddr;

			if (data.n)
			{
				nAddr -= m_cmplData0; // to rel

				size_t n = nAddr + data.n;
				if (m_Data.size() < n)
					m_Data.resize(n);

				memcpy(&m_Data.front() + nAddr, data.p, data.n);
			}
		}
	}

	void CompilerPlus::OnSection_DataCount(Reader& inp)
	{
		[[maybe_unused]] uint32_t nCount = inp.Read<uint32_t>();
		//nCount;
	}




	struct Compiler::Context
	{

		Compiler& m_This;
		const uint8_t* m_p0;
		uint32_t m_iFunc = 0;
		Reader m_Code;
		bool m_BuildDependency;

		struct Block
		{
			PerType m_Type;
			std::vector<uint8_t> m_OperandsAtExit;
			uint32_t m_iLabel;
			bool m_Loop = false;
		};

		std::vector<Block> m_Blocks;
		std::vector<uint8_t> m_Operands;
		uint32_t m_WordsOperands = 0;
		bool m_Unreachable = false;

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

		void Pop(const Vec<uint8_t>& vArgs)
		{
			for (uint32_t i = vArgs.n; i--; )
				Pop(vArgs.p[i]);
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
			size_t nOperandsAtExit = m_Operands.size();

			if (1 != m_Blocks.size())
			{
				TestOperands(tp.m_Args); // for most outer function block the args are not on the stack
				nOperandsAtExit -= tp.m_Args.n;

				b.m_iLabel = static_cast<uint32_t>(m_This.m_Labels.m_Items.size());
				m_This.m_Labels.m_Items.push_back(0);
			}

			b.m_OperandsAtExit.resize(nOperandsAtExit + tp.m_Rets.n);
			std::copy(m_Operands.begin(), m_Operands.begin() + nOperandsAtExit, b.m_OperandsAtExit.begin());
			std::copy(tp.m_Rets.p, tp.m_Rets.p + tp.m_Rets.n, b.m_OperandsAtExit.begin() + nOperandsAtExit);

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

		void TestBlockCanClose(const Block& b)
		{
			Test(m_Operands == b.m_OperandsAtExit);
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

			const auto& tp = m_Blocks.front().m_Type;
			uint32_t nWordsArgs = WordsOfVars(tp.m_Args);

			WriteResU(WordsOfVars(tp.m_Rets)); // sizeof(retval)
			WriteResU(nWordsLocal - nWordsArgs); // sizeof(locals), excluding args
			WriteResU(nWordsArgs);
		}

		void BlockClose()
		{
			auto& b = get_B();

			if (m_Unreachable)
			{
				if (m_Operands != b.m_OperandsAtExit)
				{
					// sometimes the compiler won't bother to restore stack operands past unconditional return. Ignore this.
					m_Operands.swap(b.m_OperandsAtExit); // sometimes the compiler won't bother to restore stack operands past unconditional return. Ignore this.

					m_WordsOperands = 0;
					for (size_t i = 0; i < m_Operands.size(); i++)
						m_WordsOperands += Type::Words(m_Operands[i]);
				}
			}
			else
				TestBlockCanClose(b);

			if (1 == m_Blocks.size())
				WriteRet(); // end of function
			else
			{
				if (!b.m_Loop)
					UpdTopBlockLabel();
			}

			m_Blocks.pop_back();
			m_Unreachable = false;

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

		void PutBranchLabel(uint32_t nLabel)
		{
			Test(nLabel + 1 < m_Blocks.size());
			assert(nLabel < m_This.m_Labels.m_Items.size());

			auto iBlock = m_Blocks.size() - (nLabel + 1);
			auto& b = m_Blocks[iBlock];

			size_t nOperands = b.m_OperandsAtExit.size();
			if (b.m_Loop)
			{
				assert(iBlock); // function block can't be loop
				nOperands += b.m_Type.m_Args.n - b.m_Type.m_Rets.n;
			}

			TestOperands(b.m_Type.m_Args);
			Test(m_Operands.size() == nOperands);

			PutLabelTrg(m_Blocks[m_Blocks.size() - (nLabel + 1)].m_iLabel);
		}

		void OnBranch()
		{
			WriteRes(*m_p0); // opcode
			m_p0 = nullptr;

			auto nLabel = m_Code.Read<uint32_t>();
			PutBranchLabel(nLabel);
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

			assert(static_cast<size_t>(var.m_Type) - static_cast<size_t>(Type::s_Base) < sizeof(Word));

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
			auto nAlign = m_Code.Read<uint32_t>();
			Processor::Stack::TestAlignmentPower(nAlign);
			return m_Code.Read<uint32_t>();
		}

		void On_i32_wrap_i64() {
			Pop(Type::i64);
			Push(Type::i32);
		}

		void On_i64_extend_i32_s() {
			Pop(Type::i32);
			Push(Type::i64);
		}

		void On_i64_extend_i32_u() {
			On_i64_extend_i32_s();
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

		void On_unreachable() {
			m_p0 = nullptr;
			m_Unreachable = true;
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

		void On_br_table()
		{
			Pop(Type::i32); // operand

			WriteRes(*m_p0); // opcode
			m_p0 = nullptr;

			uint32_t nLabels;
			m_Code.Read(nLabels);
			WriteResU(nLabels);

			// the following loop includes the 'def' label too
			do
			{
				auto nLabel = m_Code.Read<uint32_t>();
				PutBranchLabel(nLabel);

			} while (nLabels--);

		}

		void On_ret()
		{
			TestBlockCanClose(m_Blocks.front()); // if the return is from a nested block - assume the necessary unwind has already been done
			WriteRet(); // end of function
			m_Unreachable = true; // ignore operand stack state until the next block closes.
		}

		void On_i32_const() {
			[[maybe_unused]] auto val = m_Code.Read<int32_t>();
			//val;
			Push(Type::i32);
		}

		void On_i64_const() {
			[[maybe_unused]] auto val = m_Code.Read<int64_t>();
			//val;
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

				TestBinding(x);
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

		void OnCallType(uint32_t iTypeIdx)
		{
			const auto& tp = m_This.m_Types[iTypeIdx];

			Pop(tp.m_Args);

			for (uint32_t i = 0; i < tp.m_Rets.n; i++)
				Push(tp.m_Rets.p[i]);
		}

		void OnDep(uint32_t iTrg)
		{
			if (m_BuildDependency)
				m_This.m_Functions[m_iFunc].m_Dep.m_Set.insert(iTrg);
		}

		void On_call()
		{
			auto iFunc = m_Code.Read<uint32_t>();

			bool bImported = (iFunc < m_This.m_ImportFuncs.size());
			if (!bImported)
			{
				iFunc -= static_cast<uint32_t>(m_This.m_ImportFuncs.size());
				Test(iFunc < m_This.m_Functions.size());

				OnDep(iFunc);
			}

			uint32_t iTypeIdx = bImported ? m_This.m_ImportFuncs[iFunc].m_TypeIdx : m_This.m_Functions[iFunc].m_TypeIdx;
			OnCallType(iTypeIdx);

			m_p0 = nullptr; // don't write

			if (bImported)
			{
				WriteRes(Instruction::call_ext);

				const auto& f = m_This.m_ImportFuncs[iFunc];
				TestBinding(f);
				WriteResU(f.m_Binding);
			}
			else
			{
				WriteRes(Instruction::call);
				PutLabelTrg(iFunc);
			}
		}

		void On_call_indirect()
		{
			auto iType = m_Code.Read<uint32_t>();
			Test(iType < m_This.m_Types.size());

			auto iTable = m_Code.Read<uint32_t>();
			Test(!iTable);

			Pop(Type::i32); // func index
			OnCallType(iType);

			m_p0 = nullptr; // don't write

			WriteRes(Instruction::call_indirect);

			OnDep(Dependency::s_IdxIndirect);
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
		CheckpointTxt cp("wasm/Compiler/build");

		auto n0 = m_Result.size();

		BuildPass(false);

		uint32_t nNumIncluded = CalcDependencies();
		uint32_t nNumMax = static_cast<uint32_t>(m_Functions.size());
		if (!m_IndirectFuncs.m_vec.empty())
			nNumMax++;

		if (nNumIncluded < nNumMax)
		{
			m_Result.resize(n0);
			BuildPass(true);
		}
	}

	void Compiler::BuildPass(bool bDependentOnly)
	{
		if (m_pDebugInfo)
		{
			m_pDebugInfo->m_vFuncs.clear();
			m_pDebugInfo->m_vFuncs.reserve(m_Functions.size());
		}

		m_Labels.m_Items.resize(m_Functions.size()); // function labels
		m_Labels.m_Targets.clear();

		for (uint32_t i = 0; i < m_Functions.size(); i++)
		{
			Context ctx(*this);
			m_Labels.m_Items[i] = static_cast<uint32_t>(m_Result.size());
			ctx.m_iFunc = i;
			ctx.m_BuildDependency = !bDependentOnly;
			ctx.CompileFunc();
		}

		m_cmplTable0 = static_cast<uint32_t>(m_Result.size());

		if (!bDependentOnly || m_IndirectFuncs.m_Dep.m_Include)
		{
			for (uint32_t i = 0; i < m_IndirectFuncs.m_vec.size(); i++)
			{
				m_Result.resize(m_Result.size() + sizeof(Word));
				to_wasm(&m_Result.back() - (sizeof(Word) - 1), m_Labels.m_Items[m_IndirectFuncs.m_vec[i]]);
			}
		}

		for (uint32_t i = 0; i < m_Labels.m_Targets.size(); i++)
		{
			auto& trg = m_Labels.m_Targets[i];

			to_wasm(&m_Result.front() + trg.m_Pos, m_Labels.m_Items[trg.m_iItem]);
		}
	}

	uint32_t Compiler::CalcDependencies()
	{
		std::vector<uint32_t> vec;
		vec.reserve(m_Functions.size() + 1);

		for (uint32_t i = 0; i < m_Functions.size(); i++)
		{
			auto& func = m_Functions[i];
			if (func.m_Dep.m_Include)
				vec.push_back(i);
		}

		if (m_IndirectFuncs.m_Dep.m_Include)
			vec.push_back(Dependency::s_IdxIndirect);

		for (uint32_t i = 0; i < vec.size(); i++)
		{
			auto iIdx = vec[i];
			auto& dep = (Dependency::s_IdxIndirect == iIdx) ? m_IndirectFuncs.m_Dep : m_Functions[iIdx].m_Dep;
			assert(dep.m_Include);

			for (auto it = dep.m_Set.begin(); dep.m_Set.end() != it; it++)
			{
				auto iIdx2 = *it;
				auto& dep2 = (Dependency::s_IdxIndirect == iIdx2) ? m_IndirectFuncs.m_Dep : m_Functions[iIdx2].m_Dep;
				if (!dep2.m_Include)
				{
					dep2.m_Include = true;
					vec.push_back(iIdx2);
				}
			}
		}

		return static_cast<uint32_t>(vec.size());
	}

	void Compiler::Context::CompileFunc()
	{
		struct MyCheckpoint :public Checkpoint {
			uint32_t m_iFunc;
			uint32_t m_Line = 0;
			virtual void Dump(std::ostream& os) override {
				os << "iFunc=" << m_iFunc << ", Line=" << m_Line;
			}

		} cp;
		cp.m_iFunc = m_iFunc;

		auto& func = m_This.m_Functions[m_iFunc];

		if (!m_BuildDependency && !func.m_Dep.m_Include)
			return; // skip it

		DebugInfo::Function* pDbg = nullptr;
		if (m_This.m_pDebugInfo)
		{
			pDbg = &m_This.m_pDebugInfo->m_vFuncs.emplace_back();
			pDbg->m_sName.assign(func.m_sName.p, func.m_sName.n);
			pDbg->m_Pos = (uint32_t)m_This.m_Result.size();
		}

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

		for ( ; !m_Blocks.empty(); cp.m_Line++)
		{
			m_p0 = m_Code.m_p0;

			I nInstruction = (I) m_Code.Read1();

			if (pDbg)
			{
				auto& de = pDbg->m_vOps.emplace_back();
				de.m_Opcode = (uint8_t) nInstruction;
				de.m_Pos = (uint32_t) m_This.m_Result.size();
			}

			switch (nInstruction)
			{
#define THE_MACRO(id, name) \
			case I::name: \
				On_##name(); \
				break;

			WasmInstructions_CustomPorted(THE_MACRO)
			WasmInstructions_NotPorted(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(id, type, name, tmem) \
			case I::type##_##name: { \
				ReadOffsAndPadding(); \
				Pop(Type::i32); \
				Push(Type::type); \
			} break;

			WasmInstructions_Load(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(id, type, name, tmem) \
			case I::type##_##name: { \
				ReadOffsAndPadding(); \
				Pop(Type::type); \
				Pop(Type::i32); \
			} break;

			WasmInstructions_Store(THE_MACRO)
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
		Test(m_Pos > m_PosMin);
		return m_pPtr[--m_Pos];
	}

	void Processor::Stack::Push1(Word x)
	{
		Test(m_Pos < m_BytesCurrent / sizeof(Word));
		m_pPtr[m_Pos++] = x;
	}

	// for well-formed wasm program we don't need to care about multi-word types bits words order, since there should be no type mixing (i.e. push as uint64, pop as uint32).
	// But the attacker may violate this rule, and cause different behavior on different machines. So we do the proper conversion
	uint64_t Processor::Stack::Pop2()
	{
		uint64_t ret = Pop1(); // loword
		return ret | static_cast<uint64_t>(Pop1()) << 32; // hiword
	}

	void Processor::Stack::Push2(uint64_t x)
	{
		Push1(static_cast<Word>(x >> 32)); // hiword
		Push1(static_cast<Word>(x)); // loword
	}

#ifdef WASM_INTERPRETER_DEBUG
	template <typename T>
	void Processor::Stack::Log(T x, bool bPush)
	{
		if (get_ParentObj().m_Dbg.m_Stack)
			*get_ParentObj().m_Dbg.m_pOut << (bPush ? "  <- " : "  -> ") << uintBigFrom(x) << std::endl;
	}

	template void Processor::Stack::Log(uint32_t, bool);
	template void Processor::Stack::Log(uint64_t, bool);
#endif // WASM_INTERPRETER_DEBUG

	Word Processor::Stack::get_AlasSp() const
	{
		assert(AlignUp(m_BytesCurrent) == m_BytesCurrent);
		return MemoryType::Stack | m_BytesCurrent;
	}

	void Processor::Stack::set_AlasSp(Word x)
	{
		Test((MemoryType::Mask & x) == MemoryType::Stack);
		m_BytesCurrent = (x & ~MemoryType::Mask);
		TestSelf();
	}

	void Processor::Stack::AliasAlloc(Word nSize)
	{
		nSize = AlignUp(nSize);
		Test(nSize <= m_BytesCurrent);
		m_BytesCurrent -= nSize;
		TestSelf();
	}

	void Processor::Stack::AliasFree(Word nSize)
	{
		nSize = AlignUp(nSize);
		m_BytesCurrent += nSize;
		Test(nSize <= m_BytesCurrent); // no overflow
		TestSelf();
	}

	uint8_t* Processor::Stack::get_AliasPtr() const
	{
		return reinterpret_cast<uint8_t*>(m_pPtr) + m_BytesCurrent;
	}

	void Processor::Stack::PushAlias(const Blob& b)
	{
		AliasAlloc(b.n);
		memcpy(get_AliasPtr(), b.p, b.n);
	}

	void Processor::Stack::TestSelf() const
	{
		// Test(m_Pos >= m_PosMin); - this test is not needed
		Test(m_BytesCurrent <= m_BytesMax);
		Test(m_Pos <= m_BytesCurrent / sizeof(Word));
		assert(AlignUp(m_BytesCurrent) == m_BytesCurrent);
	}

	Word Processor::Stack::AlignUp(Word n)
	{
		return (n + s_Alignment-1) & ~(s_Alignment-1);
	}

	void Processor::Stack::TestAlignmentPower(uint32_t n)
	{
		static_assert(s_Alignment == (1 << 4));
		Test(n <= 4);
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

			Test((nOffset >= nWords) && (nOffset <= m_Stack.m_Pos - m_Stack.m_PosMin));

			uint32_t* pSrc = m_Stack.m_pPtr + m_Stack.m_Pos;
			uint32_t* pDst = pSrc - nOffset;

			if (!bSet)
			{
				std::swap(pDst, pSrc);
				m_Stack.m_Pos += nWords;
				Test(m_Stack.m_Pos <= m_Stack.m_BytesCurrent / sizeof(Word));
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

		uint8_t* MemArgEx(uint32_t nSize, bool bW)
		{
			auto nAlign = m_Instruction.Read<Word>();
			Stack::TestAlignmentPower(nAlign);

			auto nOffs = m_Instruction.Read<Word>();
			nOffs += m_Stack.Pop<Word>();

			return get_AddrEx(nOffs, nSize, bW);
		}

		uint8_t* MemArgW(uint32_t nSize) {
			return MemArgEx(nSize, true);
		}

		const uint8_t* MemArgR(uint32_t nSize) {
			return MemArgEx(nSize, false);
		}

		void RunOncePlus()
		{
			struct MyCheckpoint :public Checkpoint {
				Word m_Ip;
				virtual void Dump(std::ostream& os) override {
					os << "wasm/Run, Ip=" << uintBigFrom(m_Ip);
				}
			} cp;
			cp.m_Ip = get_Ip();

			typedef Instruction I;
			I nInstruction = (I) m_Instruction.Read1();

#ifdef WASM_INTERPRETER_DEBUG
			if (m_Dbg.m_Instructions)
				*m_Dbg.m_pOut << "ip=" << uintBigFrom(cp.m_Ip) << ", sp=" << uintBigFrom(m_Stack.m_Pos) << ' ';

#	define WASM_LOG_INSTRUCTION(name) if (m_Dbg.m_Instructions) (*m_Dbg.m_pOut) << #name << std::endl;
#else // WASM_INTERPRETER_DEBUG
#	define WASM_LOG_INSTRUCTION(name)
#endif // WASM_INTERPRETER_DEBUG

			switch (nInstruction)
			{
#define THE_CASE(name) case I::name: WASM_LOG_INSTRUCTION(name)

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


#define THE_MACRO(id, type, name, tmem) \
			THE_CASE(type##_##name) { \
				tmem val1 = from_wasm<Type::ToFlexible<tmem, false>::T>(MemArgR(sizeof(tmem))); \
				auto valExt = Type::Extend<Type::Code2Type<Type::type>::T, tmem>(val1); \
				m_Stack.Push(valExt); \
			} break;

			WasmInstructions_Load(THE_MACRO)
#undef THE_MACRO

#define THE_MACRO(id, type, name, tmem) \
			THE_CASE(type##_##name) { \
				auto val = m_Stack.Pop<Type::Code2Type<Type::type>::T>(); \
				to_wasm(MemArgW(sizeof(tmem)), static_cast<tmem>(val)); \
			} break;

			WasmInstructions_Store(THE_MACRO)
#undef THE_MACRO

			default:
				Fail();
			}

		}
	};

	Word Processor::get_Ip() const
	{
		return static_cast<Word>(m_Instruction.m_p0 - (const uint8_t*)m_Code.p);
	}

	uint8_t* Processor::get_AddrExVar(uint32_t nOffset, uint32_t& nSizeOut, bool bW) const
	{
		CheckpointTxt cp("mem/probe");

		Blob blob;

		Word nMemType = MemoryType::Mask & nOffset;
		nOffset &= ~MemoryType::Mask;

		switch (nMemType)
		{
		case MemoryType::Global:
			blob = m_LinearMem;
			break;

		case MemoryType::Data:
			//Test(!bW); Enable write to data. This enables normal use of global variables.
			blob = m_Data;
			nOffset -= m_prData0; // data va start at specific offset
			break;

		case MemoryType::Stack:
			// should be no access below current stack pointer
			if (nOffset < m_Stack.m_BytesCurrent)
			{
				// sometimes the compiler may omit updating the stack pointer yet write below it (currently this happens in debug build empty function with a single parameter)
				// We allow it, as long as it's above wasm operand stack
				Test(m_Stack.m_Pos <= nOffset / sizeof(Word));
			}

			blob.p = m_Stack.m_pPtr;
			blob.n = m_Stack.m_BytesMax;
			break;

		default:
			Fail();
		}

		Test(nOffset <= blob.n);
		nSizeOut = blob.n - nOffset;
		return reinterpret_cast<uint8_t*>(Cast::NotConst(blob.p)) + nOffset;
	}

	uint8_t* Processor::get_AddrEx(uint32_t nOffset, uint32_t nSize, bool bW) const
	{
		if (!nSize)
			return nullptr;

		uint32_t nSizeOut;
		auto pRet = get_AddrExVar(nOffset, nSizeOut, bW);

		CheckpointTxt cp("mem/bounds");
		Test(nSize <= nSizeOut);

		return pRet;
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
		switch (static_cast<VariableType>(iVar))
		{
		case VariableType::StackPointer:
			if (bGet)
				m_Stack.Push(m_Stack.get_AlasSp());
			else
			{
				Word val = m_Stack.Pop<Word>();
				Test(m_Stack.AlignUp(val) == val);
				m_Stack.set_AlasSp(val);
			}
			break;

		default:
			Fail();
		}

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
		Test(m_Stack.m_Pos - m_Stack.m_PosMin >= nWords);
		m_Stack.m_Pos -= nWords;
	}

	void ProcessorPlus::On_select()
	{
		uint32_t nWords = Type::Words(m_Instruction.Read1());
		auto nSel = m_Stack.Pop<Word>();

		Test(m_Stack.m_Pos - m_Stack.m_PosMin >= (nWords << 1)); // must be at least 2 such operands
		m_Stack.m_Pos -= nWords;

		if (!nSel)
		{
			for (uint32_t i = 0; i < nWords; i++)
				m_Stack.m_pPtr[m_Stack.m_Pos + i - nWords] = m_Stack.m_pPtr[m_Stack.m_Pos + i];
		}
	}

	void ProcessorPlus::On_i32_wrap_i64()
	{
		auto val = m_Stack.Pop<uint64_t>();
		m_Stack.Push(static_cast<uint32_t>(val));
	}

	void ProcessorPlus::On_i64_extend_i32_s()
	{
		m_Stack.Push(Type::Extend<uint64_t>(m_Stack.Pop<int32_t>()));
	}

	void ProcessorPlus::On_i64_extend_i32_u()
	{
		m_Stack.Push(Type::Extend<uint64_t>(m_Stack.Pop<uint32_t>()));
	}

	void ProcessorPlus::On_br()
	{
		Jmp(ReadAddr());
	}

	void ProcessorPlus::On_br_if()
	{
		Word addr = ReadAddr();
		if (m_Stack.Pop<Word>())
			Jmp(addr);
	}

	void ProcessorPlus::On_br_table()
	{
		uint32_t nLabels;
		m_Instruction.Read(nLabels);

		Word nOperand = m_Stack.Pop<Word>();
		std::setmin(nOperand, nLabels); // fallback to 'def' if out-of-range

		nLabels++;
		uint32_t nSize = sizeof(Word) * nLabels;
		Test(nSize / sizeof(Word) == nLabels); // overflow check

		auto* pAddrs = reinterpret_cast<const Word*>(m_Instruction.Consume(nSize));
		Jmp(from_wasm<Word>(pAddrs[nOperand]));
	}

	void ProcessorPlus::On_call()
	{
		Word nAddr = ReadAddr();
		Word nRetAddr = get_Ip();
		m_Stack.Push(nRetAddr);
		OnCall(nAddr);
	}

	void ProcessorPlus::On_call_ext()
	{
		uint32_t iExt = m_Instruction.Read<uint32_t>();

		struct MyCheckpoint :public Checkpoint {
			uint32_t m_iExt;
			virtual void Dump(std::ostream& os) override {
				os << "InvokeExt=" << m_iExt;
			}

		} cp;
		cp.m_iExt = iExt;

		InvokeExt(iExt);
	}

	Word Processor::ReadTable(Word iItem) const
	{
		iItem--; // it's 1-based
		assert(m_prTable0 <= m_Code.n); // must be checked during setup
		Test(iItem < (m_Code.n - m_prTable0) / sizeof(Word));
		return from_wasm<Word>(static_cast<const uint8_t*>(m_Code.p) + m_prTable0 + sizeof(Word) * iItem);
	}

	Word Processor::ReadVFunc(Word pObject, Word iFunc) const
	{
		Word pVTable = from_wasm<Word>(get_AddrR(pObject, sizeof(Word)));
		Word iFuncIdx = from_wasm<Word>(get_AddrR(pVTable + sizeof(Word) * iFunc, sizeof(Word)));

		return ReadTable(iFuncIdx);
	}

	void ProcessorPlus::On_call_indirect()
	{
		Word iFunc = m_Stack.Pop<Word>();
		Word nAddr = ReadTable(iFunc);

		Word nRetAddr = get_Ip();
		m_Stack.Push(nRetAddr);
		OnCall(nAddr);
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
		Test(nPosRetDst >= m_Stack.m_PosMin);

		Word nRetAddr = m_Stack.m_pPtr[nPosAddr];

		for (uint32_t i = 0; i < nRets; i++)
			m_Stack.m_pPtr[nPosRetDst + i] = m_Stack.m_pPtr[nPosRetSrc + i];


		m_Stack.m_Pos = nPosRetDst + nRets;
		OnRet(nRetAddr);
	}

	void Processor::OnCall(Word nAddr)
	{
		Jmp(nAddr);
	}

	void Processor::OnRet(Word nRetAddr)
	{
		Jmp(nRetAddr);
	}




} // namespace Wasm
} // namespace beam
