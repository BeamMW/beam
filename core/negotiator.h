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
		static const uint32_t Scheme = Input0 + 1;

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

	namespace Serialization
	{
		template <typename T>
		static void Write(ByteBuffer& buf, const T& val)
		{
			Serializer ser;
			ser & val;
			ser.swap_buf(buf);
		}

		template <typename T>
		bool Read(T& val, const Blob& blob)
		{
			Deserializer der;
			der.reset(blob.p, blob.n);

			try {
				der & val;
			}
			catch (...) {
				return false;
			}

			return true;
		}
	}

	namespace Gateway
	{
		struct IBase
		{
			virtual void Send(uint32_t code, ByteBuffer&&) = 0;

			template <typename T>
			void Send(const T& val, uint32_t code)
			{
				assert(code < Codes::Private);

				ByteBuffer buf;
				Serialization::Write(buf, val);
				Send(code, std::move(buf));
			}

		};
	}

	namespace Storage
	{
		struct IBase
		{
			virtual void Write(uint32_t code, ByteBuffer&&) = 0;
			virtual bool Read(uint32_t code, Blob&) = 0;

			template <typename T>
			bool Get(T& val, uint32_t code)
			{
				Blob blob;
				return
					Read(code, blob) &&
					Serialization::Read(val, blob);
			}

			template <typename T>
			void Set(const T& val, uint32_t code)
			{
				ByteBuffer buf;
				Serialization::Write(buf, val);
				Write(code, std::move(buf));
			}

			template <typename T>
			bool ReadConst(uint32_t code, Blob& blob, const T& val)
			{
				if (Read(code, blob))
					return true;

				// set it artifica
				Set(val, code);

				return Read(code, blob);
			}
		};
	}


	class IBase
	{
	protected:

		uint32_t m_Pos; // logical position. Should be updated at least when something is transmitted to peer, to prevent duplicated transmission

		template <typename T>
		void Send(const T& val, uint32_t code) { m_pGateway->Send(val, code); }

		bool RaiseTo(uint32_t pos);

		virtual uint32_t Update2() = 0;

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

			Storage::IBase* get_S() { return m_pS; }

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
		bool Get(T& val, uint32_t code) { return m_pStorage->Get(val, code); }

		template <typename T>
		void Set(const T& val, uint32_t code) { m_pStorage->Set(val, code); }

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
		virtual uint32_t Update2() override;
	public:

		struct Codes
			:public Negotiator::Codes
		{
			static const uint32_t Kidv = Input0 + 5;
			static const uint32_t ShareResult = Input0 + 6;
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

		virtual uint32_t Update2() override;

	public:

		struct Codes
			:public Negotiator::Codes
		{
			static const uint32_t ShareResult = Input0 + 35;

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

			static const uint32_t Barrier = Input0 + 30; // don't continue beyond "point of no return", i.e. where the peer can complete the transaction
			static const uint32_t RestrictInputs = Input0 + 31; // peer isn't allowed to add inputs (i.e. spoil tx with non-existing inputs)
			static const uint32_t RestrictOutputs = Input0 + 32; // peer isn't allowed to "inflate" transaction (making it invalid or less likely to be mined). Can add up to 1 own output with DoNotDuplicate flag, no extra kernels

			static const uint32_t Nonce = Variable0 + 1;

			static const uint32_t KrnCommitment = PeerVariable0 + 0;
			static const uint32_t KrnNonce = PeerVariable0 + 1;
			static const uint32_t KrnSig = PeerVariable0 + 2;
			static const uint32_t TxPartial = PeerVariable0 + 3;

			static const uint32_t TxFinal = Output0 + 0;
			static const uint32_t KernelID = Output0 + 1;
			static const uint32_t BarrierCrossed = Output0 + 2; // peer may come up with a valid tx
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
		virtual uint32_t Update2() override;

		struct Codes
			:public Negotiator::Codes
		{
			static const uint32_t One = Variable0 + 1;
		};

	public:

		Multisig m_MSig; // msig1
		MultiTx m_Tx1; // msig0 - > msig1
		MultiTx m_Tx2; // msig1 -> outputs, timelocked

		void Setup(
			const Key::IDV* pMsig1,
			const Key::IDV* pMsig0,
			const ECC::Point* pComm0,
			const std::vector<Key::IDV>* pOuts,
			Height hLock);

		struct Result
		{
			ECC::Point m_Comm1;
			Transaction m_tx1; // msig0 -> msig1. Set iff Role==0
			Transaction m_tx2; // msig1 -> outputs, timelocked
		};

		void get_Result(Result&);

		// Worker object, handles remapping of storage and gateway for inner objects
		class Worker
		{
			struct S0 :public Router
			{
				using Router::Router;
				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_s0)
			} m_s0;

			struct S1 :public Router
			{
				using Router::Router;
				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_s1)
			} m_s1;

			struct S2 :public Router
			{
				using Router::Router;
				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_s2)
			} m_s2;

			bool get_One(Blob& blob);

		public:
			Worker(WithdrawTx&);
		};

		static const uint32_t s_Channels = 4;
	};

	//////////////////////////////////////////
	// ChannelWithdrawal - base (abstract)
	class ChannelWithdrawal
		:public IBase
	{
	public:
		WithdrawTx m_WdA; // withdraw for Role==0
		WithdrawTx m_WdB; // withdraw for Role==1

		struct Result
		{
			// My withdrawal path
			ECC::Point m_Comm1;
			Transaction m_tx1;
			Transaction m_tx2;
			// Peer's 2nd withdrawal tx
			ECC::Point m_CommPeer1;
			Transaction m_txPeer2;
		};

	protected:

		ChannelWithdrawal() {}

		void get_Result(Result&);
	};

	//////////////////////////////////////////
	// ChannelOpen
	class ChannelOpen
		:public ChannelWithdrawal
	{
		virtual uint32_t Update2() override;

		struct Codes
			:public Negotiator::Codes
		{
			static const uint32_t One = Variable0 + 1;
		};

	public:

		Multisig m_MSig; // msig0
		MultiTx m_Tx0; // inputs -> msig0

		void Setup(
			const std::vector<Key::IDV>* pInps,
			const std::vector<Key::IDV>* pOutsChange,
			const Key::IDV* pMsig0,
			const Key::IDV* pMsig1A,
			const Key::IDV* pMsig1B,
			const std::vector<Key::IDV>* pOutsWd,
			Height hLock);

		struct Result
			:public ChannelWithdrawal::Result
		{
			// Channel open
			ECC::Point m_Comm0;
			Transaction m_txOpen; // Set iff Role==0.
		};

		void get_Result(Result&);

		// Worker object, handles remapping of storage and gateway for inner objects
		class Worker
		{
			struct S0 :public Router
			{
				using Router::Router;
				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_s0)
			} m_s0;

			struct S1 :public Router
			{
				using Router::Router;
				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_s1)
			} m_s1;

			struct SA :public Router
			{
				using Router::Router;
				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_sa)
			} m_sa;

			struct SB :public Router
			{
				using Router::Router;
				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_sb)
			} m_sb;

			WithdrawTx::Worker m_wrkA;
			WithdrawTx::Worker m_wrkB;

			bool get_One(Blob& blob);

		public:
			Worker(ChannelOpen&);
		};

		static const uint32_t s_Channels = 3 + WithdrawTx::s_Channels * 2;
	};

	//////////////////////////////////////////
	// ChannelUpdate
	class ChannelUpdate
		:public ChannelWithdrawal
	{
		virtual uint32_t Update2() override;

		struct Codes
			:public Negotiator::Codes
		{
			static const uint32_t One = Variable0 + 1;

			static const uint32_t PeerBlindingFactor = PeerVariable0 + 0;

			static const uint32_t KidvPrev = Input0 + 5;
			static const uint32_t KidvPrevPeer = Input0 + 6;
			static const uint32_t CommitmentPrevPeer = Input0 + 7;

			static const uint32_t PeerKey = Output0 + 0;
			static const uint32_t SelfKeyRevealed = Output0 + 1;
		};

	public:

		void Setup(
			const Key::IDV* pMsig0,
			const ECC::Point* pComm0,
			const Key::IDV* pMsig1A,
			const Key::IDV* pMsig1B,
			const std::vector<Key::IDV>* pOutsWd,
			Height hLock,
			const Key::IDV* pMsigPrevMy, // should be revealed to the peer
			const Key::IDV* pMsigPrevPeer, // my part, that must be complemented by peer
			const ECC::Point* pCommPrevPeer); // the commitment, that should be obtained from my part and the blinding factor revealed by the peer


		struct Result
			:public ChannelWithdrawal::Result
		{
			// prev channel state
			bool m_RevealedSelfKey;
			bool m_PeerKeyValid;
			ECC::Scalar m_PeerKey;
		};

		void get_Result(Result&);

		// Worker object, handles remapping of storage and gateway for inner objects
		class Worker
		{
			struct SA :public Router
			{
				using Router::Router;
				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_sa)
			} m_sa;

			struct SB :public Router
			{
				using Router::Router;
				virtual bool Read(uint32_t code, Blob& blob) override;

				IMPLEMENT_GET_PARENT_OBJ(Worker, m_sb)
			} m_sb;

			WithdrawTx::Worker m_wrkA;
			WithdrawTx::Worker m_wrkB;

			bool get_One(Blob& blob);

		public:
			Worker(ChannelUpdate&);
		};

		static const uint32_t s_Channels = 1 + WithdrawTx::s_Channels * 2;
	};


} // namespace Negotiator
} // namespace beam
