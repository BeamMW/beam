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
#include "serialization_adapters.h"
#include "../utility/serialize.h"

namespace beam {
namespace Negotiator {


	struct Codes
	{
		// variables that peers are allowed to transmit. Overwrite is NOT allowed (i.e. peer can't modify your variable after it was set)
		static const uint32_t PeerVariable0 = 0;

		// Starting from this - peers are NOT allowed to set
		static const uint32_t Private = 128;

		static const uint32_t Input0 = 129; // Input parameters, usually set by the caller

		static const uint32_t Role = Input0; // initiator, acceptor, etc.

		static const uint32_t Variable0 = 190; // Internal variables, set during operation
		static const uint32_t Position = Variable0;

		static const uint32_t Status = 221;

		static const uint32_t Output0 = 230; // Results
	};

	struct Status
	{
		static const uint32_t Pending = 0;
		static const uint32_t Success = 1;
		static const uint32_t Error = 2; // generic
	};

	namespace Gateway
	{
		struct IBase {
			virtual void Send(uint32_t code, ByteBuffer&&) = 0;
		};
	}

	namespace Storage
	{
		struct IBase {
			virtual void Write(uint32_t code, ByteBuffer&&) = 0;
			virtual bool Read(uint32_t code, Blob&) = 0;
		};
	}


	class IBase
	{
		template <typename T>
		static void WriteRaw(ByteBuffer& buf, const T& val)
		{
			Serializer ser;
			ser & val;
			ser.swap_buf(buf);
		}

	protected:

		uint32_t m_Pos; // logical position. Should be updated at least when something is transmitted to peer, to prevent duplicated transmission

		template <typename T>
		void Send(const T& val, uint32_t code)
		{
			assert(code < Codes::Private);

			ByteBuffer buf;
			WriteRaw(buf, val);

			m_pGateway->Send(code, std::move(buf));
		}

		void OnFail();
		void OnDone();
		bool RaiseTo(uint32_t pos);

		virtual void Update2() = 0;

		class Router
			:public Gateway::IBase
			,public Storage::IBase
		{
		protected:
			const uint32_t m_iChannel;
			Gateway::IBase* m_pG;
			Storage::IBase* m_pS;

			uint32_t Remap(uint32_t code) const;
		public:

			Router(Gateway::IBase*, Storage::IBase*, uint32_t iChannel, Negotiator::IBase&);

			// Gateway::IBase
			virtual void Send(uint32_t code, ByteBuffer&& buf) override;
			// Storage::IBase
			virtual void Write(uint32_t code, ByteBuffer&&) override;
			virtual bool Read(uint32_t code, Blob& blob) override;
		};



	public:

		Key::IKdf::Ptr m_pKdf;
		Storage::IBase* m_pStorage = nullptr;
		Gateway::IBase* m_pGateway = nullptr;

		template <typename T>
		bool Get(T& val, uint32_t code)
		{
			Blob blob;
			if (m_pStorage->Read(code, blob))
			{
				Deserializer der;
				der.reset(blob.p, blob.n);

				try {
					der & val;
					return true;
				}
				catch (...) {
				}
			}

			return false;
		}

		template <typename T>
		void Set(const T& val, uint32_t code)
		{
			ByteBuffer buf;
			WriteRaw(buf, val);
			m_pStorage->Write(code, std::move(buf));
		}

		uint32_t Update();
	};

	namespace Gateway
	{
		// directly update peer's storage. Suitable for local peers
		struct Direct :public IBase
		{
			Negotiator::IBase& m_Peer;
			Direct(Negotiator::IBase& x) :m_Peer(x) {}

			virtual void Send(uint32_t code, ByteBuffer&& buf) override;
		};
	}

	namespace Storage
	{
		struct Map
			:public IBase
			,public std::map<uint32_t, ByteBuffer>
		{
			virtual void Write(uint32_t code, ByteBuffer&& buf) override;
			virtual bool Read(uint32_t code, Blob& blob) override;
		};
	}


	//////////////////////////////////////////
	// Multisig - create a multi-signed UTXO

	class Multisig
		:public IBase
	{
		struct Impl;
		virtual void Update2() override;
	public:

		struct Codes
			:public Negotiator::Codes
		{
			static const uint32_t Kidv = Input0 + 1;
			static const uint32_t ShareResult = Input0 + 2;
			static const uint32_t Commitment = Output0 + 1;
			static const uint32_t OutputTxo = Output0 + 0;
			static const uint32_t Nonce = Variable0 + 1;
			static const uint32_t PubKeyPlus = PeerVariable0 + 0;
			static const uint32_t BpPart2 = PeerVariable0 + 1;
			static const uint32_t BpBothPart = PeerVariable0 + 2;
			static const uint32_t BpPart3 = PeerVariable0 + 3;
			static const uint32_t BpFull = PeerVariable0 + 4;
		};
	};

	//////////////////////////////////////////
	// MultiTx - create a transaction with arbitrary inputs and outputs.
	// Up to 1 multisigned UTXO on both endings
	// Optionally with relative timelock

	class MultiTx
		:public IBase
	{
		void CalcInput(const Key::IDV& kidv, ECC::Scalar::Native& offs, ECC::Point& comm);
		void CalcMSig(const Key::IDV& kidv, ECC::Scalar::Native& offs);
		ECC::Point& PushInput(Transaction& tx);
		bool BuildTxPart(Transaction& tx, bool bIsSender, ECC::Scalar::Native& offs);
		bool ReadKrn(TxKernel& krn, ECC::Hash::Value& hv);

		virtual void Update2() override;

	public:

		struct Codes
			:public Negotiator::Codes
		{
			static const uint32_t ShareResult = Input0 + 2;

			static const uint32_t InpKidvs = Input0 + 21;
			static const uint32_t InpMsKidv = Input0 + 22;
			static const uint32_t InpMsCommitment = Input0 + 23;

			static const uint32_t OutpKidvs = Input0 + 11;
			static const uint32_t OutpMsKidv = Input0 + 12;
			static const uint32_t OutpMsTxo = Input0 + 13;

			static const uint32_t KrnH0 = Input0 + 5;
			static const uint32_t KrnH1 = Input0 + 6;
			static const uint32_t KrnFee = Input0 + 7;
			static const uint32_t KrnLockHeight = Input0 + 8;
			static const uint32_t KrnLockID = Input0 + 9;

			static const uint32_t Block = Input0 + 30; // don't continue beyond "point of no return", i.e. where the peer can complete the transaction
			static const uint32_t RestrictInputs = Input0 + 31; // peer isn't allowed to add inputs (i.e. spoil tx with non-existing inputs)
			static const uint32_t RestrictOutputs = Input0 + 32; // peer isn't allowed to "inflate" transaction (making it invalid or less likely to be mined). Can add up to 1 own output with DoNotDuplicate flag, no extra kernels

			static const uint32_t Nonce = Variable0 + 1;

			static const uint32_t KrnCommitment = PeerVariable0 + 0;
			static const uint32_t KrnNonce = PeerVariable0 + 1;
			static const uint32_t KrnSig = PeerVariable0 + 2;
			static const uint32_t TxPartial = PeerVariable0 + 3;

			static const uint32_t TxFinal = Output0 + 0;
			static const uint32_t KernelID = Output0 + 1;
		};
	};

	//////////////////////////////////////////
	// WithdrawTx - part of lightning network
	// Consists internally of 3 negotiations:
	//		create msig1 - a newer multisigned UTXO
	//		transaction mgis0 -> msig1
	//		transaction msig1 -> outputs (withdrawal) with relative timelock w.r.t. previous transaction

	class WithdrawTx
		:public IBase
	{
		virtual void Update2() override;

		struct Codes
			:public Negotiator::Codes
		{
			static const uint32_t One = Variable0 + 1;
		};

	public:

		Multisig m_MSig; // msig1
		MultiTx m_Tx1; // msig0 - > msig1
		MultiTx m_Tx2; // msig1 -> outputs, timelocked

		// Worker object, handles remapping of storage and gateway for inner objects
		class Worker
		{
			WithdrawTx& m_This;

			struct S0 :public Router
			{
				S0() :Router(get_ParentObj().m_This.m_pGateway, get_ParentObj().m_This.m_pStorage, 1, get_ParentObj().m_This.m_MSig) {}

				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_s0)
			} m_s0;

			struct S1 :public Router
			{
				S1() :Router(get_ParentObj().m_This.m_pGateway, get_ParentObj().m_This.m_pStorage, 2, get_ParentObj().m_This.m_Tx1) {}

				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_s1)
			} m_s1;

			struct S2 :public Router
			{
				S2() :Router(get_ParentObj().m_This.m_pGateway, get_ParentObj().m_This.m_pStorage, 3, get_ParentObj().m_This.m_Tx2) {}

				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_s2)
			} m_s2;

			bool get_One(Blob& blob);

		public:
			Worker(WithdrawTx& x);
		};
	};




} // namespace Negotiator
} // namespace beam
