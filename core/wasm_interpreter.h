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
			uint32_t m_Binding = 0; // set by the env before compiling
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

		ByteBuffer m_Result;

		void Parse(const Reader&); // parses the wasm file info, sets labels for local functions. No compilation yet.
		// Time to set the external bindings, create a header, etc.

		void Build();
	};



	struct Processor
	{
		Blob m_Code;
		Blob m_LinearMem;
		Reader m_Instruction;

		struct Stack
		{
			Word* m_pPtr;
			Word m_Size = 0;
			Word m_Pos = 0;

			Word Pop1();
			void Push1(const Word&);

			template <typename T> T Pop();
			template <typename T> void Push(const T&);

		} m_Stack;

		std::ostringstream* m_pDbg = nullptr;

		void Jmp(uint32_t ip);
		void RunOnce();
		uint8_t* get_LinearAddr(uint32_t nOffset, uint32_t nSize);

		virtual void InvokeExt(uint32_t);
		virtual void OnGlobalVar(uint32_t, bool bGet);

	};

} // namespace Wasm
} // namespace beam
