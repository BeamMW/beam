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
#include "../utility/containers.h"

namespace beam {

	struct EvmProcessor
	{
		typedef uintBig_t<32> Word;

		static void Fail();
		static void Test(bool b);

		static uint32_t WtoU32(const Word& w);
		static uint64_t WtoU64(const Word& w);

		static void HashOf(Word&, const Blob&);

		struct Address
			:public uintBig_t<20>
		{
			typedef uintBig_t<20> Base;

			void ToWord(Word& w) const
			{
				static_assert(Word::nBytes >= nBytes);
				memset0(w.m_pData, Word::nBytes - nBytes);
				memcpy(w.m_pData + Word::nBytes - nBytes, m_pData, nBytes);
			}

			Word ToWord() const
			{
				Word w;
				ToWord(w);
				return w;
			}
		};


		struct Stack
		{
			std::vector<Word> m_v;

			Word& Push()
			{
				Test(m_v.size() < 1024);
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

		};

		struct Memory
		{
			std::vector<uint8_t> m_v;
		};

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

			void operator = (const Blob& x)
			{
				m_p = (const uint8_t*)x.p;
				m_n = x.n;
			}

		};


		void Reset();

		struct Args
		{
			Blob m_Buf;
			Word m_CallValue; // amonth of eth to be received by the caller?
		};

		struct Frame
			:public boost::intrusive::list_base_hook<>
		{
			typedef intrusive::list_autoclear<Frame> List;

			Address m_Address;
			Stack m_Stack;
			Memory m_Memory;
			Code m_Code;
			Args m_Args;
			uint64_t m_Gas;
		};

		Frame::List m_lstFrames;
		Address m_Caller;

		struct RetVal
		{
			Memory m_Memory;
			Blob m_Blob;
			bool m_Success;

		} m_RetVal;

		EvmProcessor()
		{
			InitVars();
		}

#pragma pack (push, 1)
		struct Method {
			uintBigFor<uint32_t>::Type m_Selector;
			void SetSelector(const Blob&);
			void SetSelector(const char*);
		};
#pragma pack (pop)

		Frame& PushFrame(const Address&);

		void RunOnce();

		virtual void SStore(const Word& key, const Word&) = 0;
		virtual bool SLoad(const Word& key, Word&) = 0;
		virtual void get_ChainID(Word&);

	private:

		struct Context;
		void InitVars();
	};

} // namespace beam
