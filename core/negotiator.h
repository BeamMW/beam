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
		struct IBase
			:public Gateway::IBase
		{
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
			m_pStorage->Send(code, std::move(buf));
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
			virtual void Send(uint32_t code, ByteBuffer&& buf) override;
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

} // namespace Negotiator
} // namespace beam
