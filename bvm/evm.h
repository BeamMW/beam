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
namespace Evm {

	void AddressForContract(Address&, const Address& from, uint64_t nonce);

	struct Processor
	{
		static void Fail();
		static void Test(bool b);

		static uint32_t WtoU32(const Word& w);
		static uint64_t WtoU64(const Word& w);

		static void HashOf(Word&, const Blob&);

		struct BlockHeader
		{
			Word m_Hash;
			Word m_Difficulty;
			Word m_GasLimit;
			Address m_Coinbase;
			Timestamp m_Timestamp;
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

			void operator = (const Blob& x)
			{
				m_p = (const uint8_t*)x.p;
				m_n = x.n;
			}

		};

		struct Account
			:public intrusive::set_base_hook<Address>
		{
			virtual ~Account() {}
			typedef intrusive::multiset_autoclear<Account> Map;

			template <typename T>
			struct Variable {
				T m_Value;
				uint32_t m_Modified = 0;
			};

			Variable<Word> m_Balance;
			Variable<bool> m_Exists;
			Variable<Blob> m_Code;

			struct Slot
				:public intrusive::set_base_hook<Word>
			{
				virtual ~Slot() {}
				typedef intrusive::multiset_autoclear<Slot> Map;

				Variable<Word> m_Data;
			};

			Slot::Map m_Slots;
		};

		void Reset();

		struct Args
		{
			Blob m_Buf;
			Word m_CallValue; // amonth of eth to be received by the caller?
		};

		struct UndoOp
		{
			struct Base
				:public boost::intrusive::list_base_hook<>
			{
				virtual ~Base() {}
				virtual void Undo(Processor&) = 0;
			};

			typedef intrusive::list_autoclear<Base> List;

			struct TouchAccount;
			struct TouchSlot;
			struct SetCode;
			template <typename T> struct VarSet_T;
		};

		struct BaseFrame
		{
			Account* m_pAccount  = nullptr;
			UndoOp::List m_lstUndo;
			uint64_t m_Gas;

			void DrainGas(uint64_t);

			template <typename T>
			void UpdateVariable(Account::Variable<T>&, const T&);
			void UpdateCode(Account&, const Blob&);
			void UpdateBalance(Account&, const Word& val1);
			void EnsureAccountCreated(Account&);
		};

		Account& TouchAccount(BaseFrame&, const Address&, bool& wasWarm);
		Account::Slot& TouchSlot(BaseFrame&, Account&, const Word& key, bool& wasWarm);

		struct Frame
			:public boost::intrusive::list_base_hook<>
			,public BaseFrame
		{
			typedef intrusive::list_autoclear<Frame> List;

			enum struct Type {
				Normal,
				CreateContract,
				CallRetStatus,
			};

			Stack m_Stack;
			Memory m_Memory;
			Code m_Code;
			Args m_Args;
			Type m_Type = Type::Normal;
		};

		Frame::List m_lstFrames;

		BaseFrame m_Top;

		struct RetVal
		{
			Memory m_Memory;
			Blob m_Blob;
			bool m_Success;

		} m_RetVal;

		Account::Map m_Accounts;

		Processor()
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

		void RunOnce();

		virtual void get_ChainID(Word&);

		virtual Height get_Height() = 0;
		virtual bool get_BlockHeader(BlockHeader&, Height) = 0;

		// state access
		virtual Account& LoadAccount(const Address&) = 0;
		virtual Account::Slot& LoadSlot(Account&, const Word&) = 0;

		// hi-level
		void Call(const Address& to, const Args&, bool isDeploy);

	private:

		struct Context;
		void InitVars();

		void CallInternal(const Address&, const Args&, uint64_t gas, bool isDeploy);
	};

} // namespace Evm
} // namespace beam
