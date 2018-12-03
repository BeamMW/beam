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

#include "common.h"
#include "ecc_native.h"
#include "../utility/bridge.h"
#include "../p2p/protocol.h"
#include "../p2p/connection.h"
#include "../utility/io/tcpserver.h"
#include "../utility/io/timer.h"
#include "aes.h"
#include "block_crypt.h"

namespace beam {
namespace proto {

#define BeamNodeMsg_NewTip(macro) \
	macro(Block::SystemState::Full, Description)

#define BeamNodeMsg_GetHdr(macro) \
	macro(Block::SystemState::ID, ID)

#define BeamNodeMsg_Hdr(macro) \
	macro(Block::SystemState::Full, Description)

#define BeamNodeMsg_GetHdrPack(macro) \
	macro(Block::SystemState::ID, Top) \
	macro(uint32_t, Count)

#define BeamNodeMsg_HdrPack(macro) \
	macro(Block::SystemState::Sequence::Prefix, Prefix) \
	macro(std::vector<Block::SystemState::Sequence::Element>, vElements)

#define BeamNodeMsg_DataMissing(macro)

#define BeamNodeMsg_Boolean(macro) \
	macro(bool, Value)

#define BeamNodeMsg_GetBody(macro) \
	macro(Block::SystemState::ID, ID)

#define BeamNodeMsg_Body(macro) \
	macro(ByteBuffer, Perishable) \
	macro(ByteBuffer, Eternal)

#define BeamNodeMsg_GetProofState(macro) \
	macro(Height, Height)

#define BeamNodeMsg_GetCommonState(macro) \
	macro(std::vector<Block::SystemState::ID>, IDs)

#define BeamNodeMsg_GetProofKernel(macro) \
	macro(Merkle::Hash, ID)

#define BeamNodeMsg_GetProofKernel2(macro) \
	macro(Merkle::Hash, ID) \
	macro(bool, Fetch)

#define BeamNodeMsg_GetProofUtxo(macro) \
	macro(ECC::Point, Utxo) \
	macro(Height, MaturityMin) /* set to non-zero in case the result is too big, and should be retrieved within multiple queries */

#define BeamNodeMsg_GetProofChainWork(macro) \
	macro(Difficulty::Raw, LowerBound)

#define BeamNodeMsg_ProofKernel(macro) \
	macro(TxKernel::LongProof, Proof)

#define BeamNodeMsg_ProofKernel2(macro) \
	macro(Merkle::Proof, Proof) \
	macro(Height, Height) \
	macro(TxKernel::Ptr, Kernel)

#define BeamNodeMsg_ProofUtxo(macro) \
	macro(std::vector<Input::Proof>, Proofs)

#define BeamNodeMsg_ProofState(macro) \
	macro(Merkle::HardProof, Proof)

#define BeamNodeMsg_ProofCommonState(macro) \
	macro(Block::SystemState::ID, ID) \
	macro(Merkle::HardProof, Proof)

#define BeamNodeMsg_ProofChainWork(macro) \
	macro(Block::ChainWorkProof, Proof)

#define BeamNodeMsg_Login(macro) \
	macro(ECC::Hash::Value, CfgChecksum) \
	macro(uint8_t, Flags)

#define BeamNodeMsg_Ping(macro)
#define BeamNodeMsg_Pong(macro)

#define BeamNodeMsg_NewTransaction(macro) \
	macro(Transaction::Ptr, Transaction) \
	macro(bool, Fluff)

#define BeamNodeMsg_HaveTransaction(macro) \
	macro(Transaction::KeyType, ID)

#define BeamNodeMsg_GetTransaction(macro) \
	macro(Transaction::KeyType, ID)

#define BeamNodeMsg_Bye(macro) \
	macro(uint8_t, Reason)

#define BeamNodeMsg_PeerInfoSelf(macro) \
	macro(uint16_t, Port)

#define BeamNodeMsg_PeerInfo(macro) \
	macro(PeerID, ID) \
	macro(io::Address, LastAddr)

#define BeamNodeMsg_GetTime(macro)

#define BeamNodeMsg_Time(macro) \
	macro(Timestamp, Value)

#define BeamNodeMsg_GetExternalAddr(macro)

#define BeamNodeMsg_ExternalAddr(macro) \
	macro(uint32_t, Value)

#define BeamNodeMsg_BbsMsg(macro) \
	macro(BbsChannel, Channel) \
	macro(Timestamp, TimePosted) \
	macro(ByteBuffer, Message)

#define BeamNodeMsg_BbsHaveMsg(macro) \
	macro(BbsMsgID, Key)

#define BeamNodeMsg_BbsGetMsg(macro) \
	macro(BbsMsgID, Key)

#define BeamNodeMsg_BbsSubscribe(macro) \
	macro(BbsChannel, Channel) \
	macro(Timestamp, TimeFrom) \
	macro(bool, On)

#define BeamNodeMsg_BbsPickChannel(macro)

#define BeamNodeMsg_BbsPickChannelRes(macro) \
	macro(BbsChannel, Channel)

#define BeamNodeMsg_SChannelInitiate(macro) \
	macro(ECC::uintBig, NoncePub)

#define BeamNodeMsg_SChannelReady(macro)

#define BeamNodeMsg_Authentication(macro) \
	macro(PeerID, ID) \
	macro(uint8_t, IDType) \
	macro(ECC::Signature, Sig)

#define BeamNodeMsg_MacroblockGet(macro) \
	macro(Block::SystemState::ID, ID) \
	macro(uint8_t, Data) \
	macro(uint64_t, Offset)

#define BeamNodeMsg_Macroblock(macro) \
	macro(Block::SystemState::ID, ID) \
	macro(ByteBuffer, Portion) \
	macro(uint64_t, SizeTotal)

#define BeamNodeMsg_GetUtxoEvents(macro) \
	macro(Height, HeightMin)

#define BeamNodeMsg_UtxoEvents(macro) \
	macro(std::vector<UtxoEvent>, Events)

#define BeamNodeMsg_GetBlockFinalization(macro) \
	macro(Height, Height) \
	macro(Amount, Fees)

#define BeamNodeMsg_BlockFinalization(macro) \
	macro(Transaction::Ptr, Value)

#define BeamNodeMsgsAll(macro) \
	/* general msgs */ \
	macro(0x00, Login) /* usually sent by node once when connected, but theoretically me be re-sent if cfg changes. */ \
	macro(0x01, Bye) \
	macro(0x02, Ping) \
	macro(0x03, Pong) \
	macro(0x04, SChannelInitiate) \
	macro(0x05, SChannelReady) \
	macro(0x06, Authentication) \
	macro(0x07, PeerInfoSelf) \
	macro(0x08, PeerInfo) \
	macro(0x09, GetExternalAddr) \
	macro(0x0a, ExternalAddr) \
	macro(0x0b, GetTime) \
	macro(0x0c, Time) \
	macro(0x0d, DataMissing) \
	macro(0x0e, Boolean) \
	/* blockchain status */ \
	macro(0x10, NewTip) \
	macro(0x11, GetHdr) \
	macro(0x12, Hdr) \
	macro(0x13, GetHdrPack) \
	macro(0x14, HdrPack) \
	macro(0x15, GetBody) \
	macro(0x16, Body) \
	macro(0x17, GetProofState) \
	macro(0x18, ProofState) \
	macro(0x19, GetProofKernel) \
	macro(0x1a, ProofKernel) \
	macro(0x1b, GetProofUtxo) \
	macro(0x1c, ProofUtxo) \
	macro(0x1d, GetProofChainWork) \
	macro(0x1e, ProofChainWork) \
	macro(0x20, MacroblockGet) \
	macro(0x21, Macroblock) \
	macro(0x22, GetCommonState) \
	macro(0x23, ProofCommonState) \
	macro(0x24, GetProofKernel2) \
	macro(0x25, ProofKernel2) \
	/* onwer-relevant */ \
	macro(0x2c, GetUtxoEvents) \
	macro(0x2d, UtxoEvents) \
	macro(0x2e, GetBlockFinalization) \
	macro(0x2f, BlockFinalization) \
	/* tx broadcast and replication */ \
	macro(0x30, NewTransaction) \
	macro(0x31, HaveTransaction) \
	macro(0x32, GetTransaction) \
	/* bbs */ \
	macro(0x38, BbsMsg) \
	macro(0x39, BbsHaveMsg) \
	macro(0x3a, BbsGetMsg) \
	macro(0x3b, BbsSubscribe) \
	macro(0x3c, BbsPickChannel) \
	macro(0x3d, BbsPickChannelRes) \


	struct LoginFlags {
		static const uint8_t SpreadingTransactions	= 0x1; // I'm spreading txs, please send
		static const uint8_t Bbs					= 0x2; // I'm spreading bbs messages
		static const uint8_t SendPeers				= 0x4; // Please send me periodically peers recommendations
		static const uint8_t MiningFinalization		= 0x8; // I want to finalize block construction for my owned node
	};

	struct IDType
	{
		static const uint8_t Node		= 'N';
		static const uint8_t Owner		= 'O';
		static const uint8_t Viewer		= 'V';
	};

	static const uint32_t g_HdrPackMaxSize = 128;

	struct UtxoEvent
	{
		static const uint32_t s_Max = 64; // will send more, if the remaining events are on the same height

		Key::IDVC m_Kidvc;

		Height m_Height;
		Height m_Maturity;

		uint8_t m_Added; // 1 = add, 0 = spend


		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_Kidvc
				& m_Height
				& m_Maturity
				& m_Added;
		}
	};

	enum Unused_ { Unused };
	enum Uninitialized_ { Uninitialized };

	template <typename T>
	inline void ZeroInit(T& x) { x = 0; }
	template <typename T>
	inline void ZeroInit(std::vector<T>&) { }
	template <typename T>
	inline void ZeroInit(std::shared_ptr<T>&) { }
	template <typename T>
	inline void ZeroInit(std::unique_ptr<T>&) { }
	template <uint32_t nBytes_>
	inline void ZeroInit(uintBig_t<nBytes_>& x) { x = ECC::Zero; }
	inline void ZeroInit(io::Address& x) { }
	inline void ZeroInit(ByteBuffer&) { }
	inline void ZeroInit(Block::SystemState::ID& x) { ZeroObject(x); }
	inline void ZeroInit(Block::SystemState::Full& x) { ZeroObject(x); }
	inline void ZeroInit(Block::SystemState::Sequence::Prefix& x) { ZeroObject(x); }
	inline void ZeroInit(Block::ChainWorkProof& x) {}
	inline void ZeroInit(ECC::Point& x) { ZeroObject(x); }
	inline void ZeroInit(ECC::Signature& x) { ZeroObject(x); }
	inline void ZeroInit(TxKernel::LongProof& x) { ZeroObject(x.m_State); }

	template <typename T> struct InitArg {
		typedef const T& TArg;
		static void Set(T& var, TArg arg) { var = arg; }
	};

	template <typename T> struct InitArg<std::unique_ptr<T> > {
		typedef std::unique_ptr<T>& TArg;
		static void Set(std::unique_ptr<T>& var, TArg arg) { var = std::move(arg); }
	};


#define THE_MACRO6(type, name) InitArg<type>::Set(m_##name, arg##name);
#define THE_MACRO5(type, name) typename InitArg<type>::TArg arg##name,
#define THE_MACRO4(type, name) ZeroInit(m_##name);
#define THE_MACRO3(type, name) & m_##name
#define THE_MACRO2(type, name) type m_##name;
#define THE_MACRO1(code, msg) \
	struct msg \
	{ \
		static const uint8_t s_Code = code; \
		BeamNodeMsg_##msg(THE_MACRO2) \
		template <typename Archive> void serialize(Archive& ar) { ar BeamNodeMsg_##msg(THE_MACRO3); } \
		msg(Zero_ = Zero) { BeamNodeMsg_##msg(THE_MACRO4) } /* default c'tor, zero-init everything */ \
		msg(Uninitialized_) { } /* don't init members */ \
		msg(BeamNodeMsg_##msg(THE_MACRO5) Unused_ = Unused) { BeamNodeMsg_##msg(THE_MACRO6) } /* explicit init */ \
	}; \
	struct msg##_NoInit :public msg { \
		msg##_NoInit() :msg(Uninitialized) {} \
	}; \

	BeamNodeMsgsAll(THE_MACRO1)
#undef THE_MACRO1
#undef THE_MACRO2
#undef THE_MACRO3
#undef THE_MACRO4
#undef THE_MACRO5
#undef THE_MACRO6

	struct ProtocolPlus
		:public Protocol
	{
		AES::Encoder m_Enc;
		AES::StreamCipher m_CipherIn;
		AES::StreamCipher m_CipherOut;

		ECC::Scalar::Native m_MyNonce;
		ECC::uintBig m_RemoteNonce;
		ECC::Hash::Mac m_HMac;

		struct Mode {
			enum Enum {
				Plaintext,
				Outgoing,
				Duplex
			};
		};

		Mode::Enum m_Mode;

		typedef uintBig_t<8> MacValue;
		static void get_HMac(ECC::Hash::Mac&, MacValue&);

		ProtocolPlus(uint8_t v0, uint8_t v1, uint8_t v2, size_t maxMessageTypes, IErrorHandler& errorHandler, size_t serializedFragmentsSize);
		void ResetVars();
		void InitCipher();

		// Protocol
		virtual void Decrypt(uint8_t*, uint32_t nSize) override;
		virtual uint32_t get_MacSize() override;
		virtual bool VerifyMsg(const uint8_t*, uint32_t nSize) override;

		void Encrypt(SerializedMsg&, MsgSerializer&);
	};

	void Sk2Pk(PeerID&, ECC::Scalar::Native&); // will negate the scalar iff necessary
	bool ImportPeerID(ECC::Point::Native&, const PeerID&);
	bool BbsEncrypt(ByteBuffer& res, const PeerID& publicAddr, ECC::Scalar::Native& nonce, const void*, uint32_t); // will fail iff addr is invalid
	bool BbsDecrypt(uint8_t*& p, uint32_t& n, const ECC::Scalar::Native& privateAddr);

	struct INodeMsgHandler
		:public IErrorHandler
	{
#define THE_MACRO(code, msg) \
		virtual void OnMsg(msg&&) {} \
		virtual bool OnMsg2(msg&& v) \
		{ \
			OnMsg(std::move(v)); \
			return true; \
		}
		BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO
	};


	class NodeConnection
		:public INodeMsgHandler
	{
		ProtocolPlus m_Protocol;
		std::unique_ptr<Connection> m_Connection;
		io::AsyncEvent::Ptr m_pAsyncFail;
		bool m_ConnectPending;

		SerializedMsg m_SerializeCache;

		void TestIoResultAsync(const io::Result& res);
		void TestInputMsgContext(uint8_t);

		static void OnConnectInternal(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode);
		void OnConnectInternal2(io::TcpStream::Ptr&& newStream, io::ErrorCode);

		virtual void on_protocol_error(uint64_t, ProtocolError error) override;
		virtual void on_connection_error(uint64_t, io::ErrorCode errorCode) override;

#define THE_MACRO(code, msg) bool OnMsgInternal(uint64_t, msg##_NoInit&& v);
		BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO

		void HashAddNonce(ECC::Hash::Processor&, bool bRemote);

	public:

		NodeConnection();
		virtual ~NodeConnection();
		void Reset();

		static void ThrowUnexpected(const char* = NULL);

		void Connect(const io::Address& addr);
		void Accept(io::TcpStream::Ptr&& newStream);

		// Secure-channel-specific
		void SecureConnect(); // must be connected already

		void ProveID(ECC::Scalar::Native&, uint8_t nIDType); // secure channel must be established
		void ProveKdfObscured(Key::IKdf&, uint8_t nIDType); // prove ownership of the kdf to the one with pkdf, otherwise reveal no info
		void ProvePKdfObscured(Key::IPKdf&, uint8_t nIDType);
		bool IsKdfObscured(Key::IPKdf&, const PeerID&);
		bool IsPKdfObscured(Key::IPKdf&, const PeerID&);


		virtual void OnMsg(SChannelInitiate&&) override;
		virtual void OnMsg(SChannelReady&&) override;
		virtual void OnMsg(Authentication&&) override;
		virtual void OnMsg(Bye&&) override;

		virtual void GenerateSChannelNonce(ECC::Scalar::Native&); // Must be overridden to support SChannel

		bool IsLive() const;
		bool IsSecureIn() const;
		bool IsSecureOut() const;

		const Connection* get_Connection() { return m_Connection.get(); }

		virtual void OnConnectedSecure() {}

		struct ByeReason
		{
			static const uint8_t Stopping	= 's';
			static const uint8_t Ban		= 'b';
			static const uint8_t Loopback	= 'L';
			static const uint8_t Duplicate	= 'd';
			static const uint8_t Timeout	= 't';
			static const uint8_t Other		= 'o';
		};

		struct DisconnectReason
		{
			enum Enum {
				Io,
				Protocol,
				ProcessingExc,
				Bye,
			};

			Enum m_Type;

			union {
				io::ErrorCode m_IoError;
				ProtocolError m_eProtoCode;
				const char* m_szErrorMsg;
				uint8_t m_ByeReason;
			};
		};

		virtual void OnDisconnect(const DisconnectReason&) {}

		void OnIoErr(io::ErrorCode);
		void OnExc(const std::exception&);

#define THE_MACRO(code, msg) void Send(const msg& v);
		BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO

		struct Server
		{
			io::TcpServer::Ptr m_pServer; // just delete it to stop listening
			void Listen(const io::Address& addr);

			virtual void OnAccepted(io::TcpStream::Ptr&&, int errorCode) = 0;
		};
	};

	std::ostream& operator << (std::ostream& s, const NodeConnection::DisconnectReason&);

} // namespace proto
} // namespace beam
