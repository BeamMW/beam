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

	class Reader
	{
		template <bool bSigned>
		uint64_t ReadInternal();
	public:

		const uint8_t* m_p0;
		const uint8_t* m_p1;

		void Ensure(uint32_t n);
		const uint8_t* Consume(uint32_t n);

		uint8_t Read1() { return *Consume(1); }

		template <typename T>
		T Read();
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
				n = inp.Read<uint32_t>();
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
			uint32_t m_TypeIdx;
			uint32_t m_Binding = 0; // set by the env before compiling
		};

		std::vector<PerImport> m_Imports;

		struct PerFunction
		{
			uint32_t m_TypeIdx;

			struct Locals
			{
				struct PerType {
					uint8_t m_Type;
					uint8_t m_Size;
					uint32_t m_Pos;
				};

				std::vector<PerType> m_v; // including args

				uint32_t get_Size() const;
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
		Reader m_Instruction;

		uint32_t m_Sp;
		uint32_t m_pStack[8192]; // stack in terms of 32-bit values, 32KiB
		uint8_t m_pLinearMem[32768];

		template <typename T> const T* Pop()
		{
			Test(m_Sp >= sizeof(T) / sizeof(uint32_t));
			m_Sp -= sizeof(T) / sizeof(uint32_t);

			return (T*)(m_pStack + m_Sp);
		}

		template <typename T> void Push(T x)
		{
			Test(m_Sp + sizeof(T) / sizeof(uint32_t) <= _countof(m_pStack));
			*(T*)(m_pStack + m_Sp) = x;
			m_Sp += sizeof(T) / sizeof(uint32_t);
		}

		void Jmp(uint32_t ip);

		void RunOnce();

		virtual void InvokeExt(uint32_t);
	};

} // namespace Wasm
} // namespace beam
