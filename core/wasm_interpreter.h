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
#include "../utility/containers.h"

namespace beam {
namespace Wasm {

	void Fail();
	void Test(bool);

	typedef uint32_t Word;

	struct TypeCode
	{
		static const uint8_t i32 = 0x7F;
		static const uint8_t i64 = 0x7E;
		static const uint8_t f32 = 0x7D;
		static const uint8_t f64 = 0x7C;
	};

	class Reader
	{
		template <typename T, bool bSigned>
		T ReadInternal();
	public:

		const uint8_t* m_p0;
		const uint8_t* m_p1;

		void Ensure(uint32_t n);
		const uint8_t* Consume(uint32_t n);

		uint8_t Read1() { return *Consume(1); }

		template <typename T>
		T Read();

		template <typename T>
		void Read(T& x) {
			x = Read<T>();
		}
	};


	struct Compiler
	{
		struct Context;

		template <typename T>
		struct Vec
		{
			uint32_t n;
			const T* p;

			void Read(Reader& inp) {
				inp.Read(n);
				ReadArr(inp);
			}

			void ReadArr(Reader& inp) {
				p = reinterpret_cast<const T*>(inp.Consume(sizeof(T) * n));
			}
		};

		struct PerType {
			Vec<uint8_t> m_Args;
			Vec<uint8_t> m_Rets;
		};

		std::vector<PerType> m_Types;

		struct PerImport {
			Vec<char> m_sMod;
			Vec<char> m_sName;
			uint32_t m_Binding; // set by the env before compiling
		};

		struct PerImportFunc :public PerImport {
			uint32_t m_TypeIdx;
		};
		std::vector<PerImportFunc> m_ImportFuncs;

		struct PerImportGlobal :public PerImport {
			uint8_t m_Type;
			uint8_t m_IsVariable;
		};
		std::vector<PerImportGlobal> m_ImportGlobals;

		struct PerFunction
		{
			uint32_t m_TypeIdx;

			struct Locals
			{
				struct PerType {
					uint8_t m_Type;
					uint8_t m_SizeWords;
					uint32_t m_PosWords;
				};

				std::vector<PerType> m_v; // including args

				uint32_t get_SizeWords() const;
				void Add(uint8_t nType);

			} m_Locals;

			Reader m_Expression;
		};

		std::vector<PerFunction> m_Functions;

		struct PerGlobal {
			uint8_t m_Type;
			uint8_t m_Mutable;
			Reader m_InitExpression;
		};

		std::vector<PerGlobal> m_Globals;

		struct PerExport {
			Vec<char> m_sName;
			uint8_t m_Kind; // 0 for function
			uint32_t m_Idx; // Type index for function
		};

		std::vector<PerExport> m_Exports;

		struct LabelSet
		{
			struct Target
			{
				uint32_t m_Pos;
				uint32_t m_iItem;
			};

			std::vector<Target> m_Targets;
			std::vector<uint32_t> m_Items;
		};

		LabelSet m_Labels;
		ByteBuffer m_Data;
		ByteBuffer m_Result;

		void Parse(const Reader&); // parses the wasm file info, sets labels for local functions. No compilation yet.
		// Time to set the external bindings, create a header, etc.

		void Build();
	};

	struct MemoryType {
		static const Word Data   = 0;
		static const Word Global = 1U << (sizeof(Word) * 8 - 2);
		static const Word Stack  = 2U << (sizeof(Word) * 8 - 2);
		static const Word Mask   = 3U << (sizeof(Word) * 8 - 2);
	};

	enum struct VariableType
	{
		StackPointer = 0,
		count
	};


	struct Processor
	{
		Blob m_Code;
		Blob m_Data;
		Blob m_LinearMem;
		Reader m_Instruction;

		struct Stack
		{
			Word* m_pPtr;
			Word m_Pos = 0;
			Word m_BytesMax = 0; // total stack size
			Word m_BytesCurrent = 0; // operand stack remaining, before it collides with alias stack

			Word get_AlasSp() const;
			void set_AlasSp(Word);

			Word Pop1();
			void Push1(Word);

			uint64_t Pop2();
			void Push2(uint64_t);

			template <typename T>
			T Pop()
			{
				static_assert(std::numeric_limits<T>::is_integer);
				if constexpr (sizeof(T) <= sizeof(Word))
					return static_cast<T>(Pop1());
				else
				{
					static_assert(sizeof(T) <= sizeof(uint64_t));
					return static_cast<T>(Pop2());
				}
			}

			template <typename T>
			void Push(const T& x)
			{
				static_assert(std::numeric_limits<T>::is_integer);
				if constexpr (sizeof(T) <= sizeof(Word))
					Push1(static_cast<T>(x));
				else
				{
					static_assert(sizeof(T) <= sizeof(uint64_t));
					Push2(static_cast<T>(x));
				}
			}

		} m_Stack;

		std::ostringstream* m_pDbg = nullptr;

		Word get_Ip() const;
		void Jmp(uint32_t ip);

		void RunOnce();
		uint8_t* get_LinearAddr(uint32_t nOffset, uint32_t nSize);

		virtual void OnCall(Word nAddr);
		virtual void OnRet(Word nRetAddr);

		virtual void InvokeExt(uint32_t);
		virtual void OnGlobalVar(uint32_t, bool bGet);

	};

} // namespace Wasm
} // namespace beam
