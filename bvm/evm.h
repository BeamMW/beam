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
		static uint64_t WtoU64(const Word& w);

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

		EvmProcessor()
		{
			InitVars();
		}

		void Reset();

		enum struct State {
			Running,
			Done,
			Failed,
		};

		State m_State;
		Blob m_RetVal;

		struct Args
		{
			Blob m_Buf;
			Word m_CallValue; // amonth of eth to be received by the caller?
			Word m_Caller;
		} m_Args;

		uint64_t m_Gas;

		bool ShouldRun() const
		{
			return State::Running == m_State;
		}

#pragma pack (push, 1)
		struct Method {
			uintBigFor<uint32_t>::Type m_MethodHash;
		};
#pragma pack (pop)

		void RunOnce();

		virtual void SStore(const Word& key, const Word&) = 0;
		virtual bool SLoad(const Word& key, Word&) = 0;

	private:

		void InitVars();
	};

} // namespace beam
