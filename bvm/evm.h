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

			static void WPad(Word& w)
			{
				memset0(w.m_pData, Word::nBytes - nBytes);
			}
			static Address& W2A(Word& w)
			{
				static_assert(Word::nBytes >= nBytes);
				return *(Address*) (w.m_pData + Word::nBytes - nBytes);
			}

			void ToWord(Word& w) const
			{
				WPad(w);
				W2A(w) = *this;
			}

			Word ToWord() const
			{
				Word w;
				ToWord(w);
				return w;
			}

			void FromPubKey(const ECC::Point::Storage&);
			bool FromPubKey(const ECC::Point&);
			bool FromPubKey(const PeerID&);
		};

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

		struct IAccount
		{
			virtual void Release() = 0;

			struct Guard {
				IAccount* m_p = nullptr;
				~Guard()
				{
					if (m_p)
						m_p->Release();
				}
			};

			// each account has the following 4 fields:
			//		nonce
			//		balance
			//		codeHash
			//		storageRoot

			virtual const Address& get_Address() = 0;

			virtual void get_Balance(Word&) = 0;
			virtual void set_Balance(const Word&) = 0;

			virtual void SStore(const Word& key, const Word&, Word& wPrev) = 0;
			virtual bool SLoad(const Word& key, Word&) = 0;

			virtual void SetCode(const Blob&) = 0;
			virtual void GetCode(Blob&) = 0;

			virtual void Delete() = 0;
		};

		struct UndoOp
		{
			struct Base
				:public boost::intrusive::list_base_hook<>
			{
				IAccount* m_pAccount;

				virtual ~Base() {}
				virtual void Undo() = 0;
			};

			typedef intrusive::list_autoclear<Base> List;

			struct Guard;
			struct VarSet;
			struct AccountDelete;
			struct BalanceChange;
		};

		struct BaseFrame
		{
			IAccount* m_pAccount = nullptr;
			UndoOp::List m_lstUndo;
			uint64_t m_Gas;

			void InitAccount(IAccount::Guard&);
			void UndoChanges();
			void DrainGas(uint64_t);
		};

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

		void RunOnce();

		virtual bool GetAccount(const Address&, bool bCreate, IAccount::Guard&) = 0; // returns if the account was created
		virtual void get_ChainID(Word&);

		virtual Height get_Height() = 0;
		virtual bool get_BlockHeader(BlockHeader&, Height) = 0;

		void UpdateBalance(IAccount*, const Word& val0, const Word& val1);

		// hi-level
		void Call(const Address& to, const Args&);
		void Deploy(Address& to, const Args&, const Word& wNonce);

	private:

		struct Context;
		void InitVars();

		void CallInternal(const Address&, const Args&, uint64_t gas, bool isDeploy);
	};

} // namespace beam
