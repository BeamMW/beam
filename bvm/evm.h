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
#include "../core/block_crypt.h"

namespace beam {

	struct EvmProcessor
	{
		typedef uintBig_t<32> Word;

		static void Fail();
		static void Test(bool b);

		static uint32_t WtoU32(const Word& w);

		struct Stack
		{
			std::vector<Word> m_v;

			Word& Push()
			{
				return m_v.emplace_back();
			}

			Word& get_At(uint32_t i)
			{
				Test(i < m_v.size());
				return m_v[m_v.size() - (i + 1)];
			}

			Word& Pop()
			{
				auto& ret = get_At(0);
				m_v.resize(m_v.size() - 1); // won't invalidate pointer
				return ret;
			}

		} m_Stack;

		struct Memory
		{
			std::vector<uint8_t> m_v;
			const uint32_t m_Max = 0x1000000; // 16MB

			static uint32_t get_Order(uint32_t n);

			uint8_t* get_Addr(const Word& wAddr, uint32_t nSize);

		} m_Memory;

		struct Code
		{
			const uint8_t* m_p;
			uint32_t m_n;
			uint32_t m_Ip;

			void TestIp() const
			{
				Test(m_Ip <= m_n);
			}

			void TestAccess(uint32_t n0, uint32_t nSize) const
			{
				Test(nSize <= (m_n - n0));
			}

			const uint8_t* Consume(uint32_t n)
			{
				TestAccess(m_Ip, n);

				auto ret = m_p + m_Ip;
				m_Ip += n;

				return ret;
			}

			uint8_t Read1()
			{
				return *Consume(1);
			}

		} m_Code;

		void RunOnce();


		void get_CallValue(Word& w)
		{
			w = Zero;
		}

		EvmProcessor()
		{
			InitVars();
		}

		void Reset();

		bool m_Finished;
		Blob m_RetVal;

	private:

		void InitVars();
	};

} // namespace beam
