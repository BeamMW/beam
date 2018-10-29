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
#include "aes.h"
#include "block_crypt.h"
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>

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
	macro(ByteBuffer, Buffer)

#define BeamNodeMsg_GetProofState(macro) \
	macro(Height, Height)

#define BeamNodeMsg_GetCommonState(macro) \
	macro(std::vector<Block::SystemState::ID>, IDs)

#define BeamNodeMsg_GetProofKernel(macro) \
	macro(Merkle::Hash, ID)

#define BeamNodeMsg_GetProofUtxo(macro) \
	macro(Input, Utxo) \
	macro(Height, MaturityMin) /* set to non-zero in case the result is too big, and should be retrieved within multiple queries */

#define BeamNodeMsg_GetProofChainWork(macro) \
	macro(Difficulty::Raw, LowerBound)

#define BeamNodeMsg_ProofKernel(macro) \
	macro(Merkle::Proof, Proof)

#define BeamNodeMsg_ProofUtxo(macro) \
	macro(std::vector<Input::Proof>, Proofs)

#define BeamNodeMsg_ProofState(macro) \
	macro(Merkle::HardProof, Proof)

#define BeamNodeMsg_ProofCommonState(macro) \
	macro(uint32_t, iState) \
	macro(Merkle::HardProof, Proof)

#define BeamNodeMsg_ProofChainWork(macro) \
	macro(Block::ChainWorkProof, Proof)

#define BeamNodeMsg_GetMined(macro) \
	macro(Height, HeightMin)

#define BeamNodeMsg_Mined(macro) \
	macro(std::vector<PerMined>, Entries)

#define BeamNodeMsg_Config(macro) \
	macro(ECC::Hash::Value, CfgChecksum) \
	macro(bool, SpreadingTransactions) \
	macro(bool, Bbs) \
	macro(bool, SendPeers)

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
	macro(ByteBuffer, Portion)

#define BeamNodeMsg_Recover(macro) \
	macro(bool, Private) \
	macro(bool, Public)

#define BeamNodeMsg_Recovered(macro) \
	macro(std::vector<Key::IDV>, Private) \
	macro(std::vector<Key::IDV>, Public)

#define BeamNodeMsgsAll(macro) \
	/* general msgs */ \
	macro(0x00, Config) /* usually sent by node once when connected, but theoretically me be re-sent if cfg changes. */ \
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
	/* onwer-relevant */ \
	macro(0x28, GetMined) \
	macro(0x29, Mined) \
	macro(0x2a, Recover) \
	macro(0x2b, Recovered) \
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


	struct PerMined
	{
		Block::SystemState::ID m_ID;
		Amount m_Fees;
		bool m_Active; // mined on active(longest) branch

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_ID
				& m_Fees
				& m_Active;
		}

		static const uint32_t s_EntriesMax = 200; // if this is the size of the vector - the result is probably trunacted
	};

	struct IDType
	{
		static const uint8_t Node		= 'N';
		static const uint8_t Owner		= 'O';
	};

	static const uint32_t g_HdrPackMaxSize = 128;

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
	template <uint32_t nBits_>
	inline void ZeroInit(uintBig_t<nBits_>& x) { x = ECC::Zero; }
	inline void ZeroInit(io::Address& x) { }
	inline void ZeroInit(ByteBuffer&) { }
	inline void ZeroInit(Block::SystemState::ID& x) { ZeroObject(x); }
	inline void ZeroInit(Block::SystemState::Full& x) { ZeroObject(x); }
	inline void ZeroInit(Block::SystemState::Sequence::Prefix& x) { ZeroObject(x); }
	inline void ZeroInit(Block::ChainWorkProof& x) {}
	inline void ZeroInit(Input& x) { ZeroObject(x); }
	inline void ZeroInit(ECC::Signature& x) { ZeroObject(x); }


#define THE_MACRO6(type, name) m_##name = name;
#define THE_MACRO5(type, name) const type& name,
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

		typedef uintBig_t<64> MacValue;
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
	bool BbsEncrypt(ByteBuffer& res, const PeerID& publicAddr, ECC::Scalar::Native& nonce, const void*, uint32_t); // will fail iff addr is invalid
	bool BbsDecrypt(uint8_t*& p, uint32_t& n, ECC::Scalar::Native& privateAddr);

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

		virtual void OnMsg(SChannelInitiate&&) override;
		virtual void OnMsg(SChannelReady&&) override;
		virtual void OnMsg(Authentication&&) override;
		virtual void OnMsg(Bye&&) override;

		virtual void GenerateSChannelNonce(ECC::Scalar::Native&); // Must be overridden to support SChannel

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


	class PeerManager
	{
	public:

		// Rating system:
		//	Initially set to default (non-zero)
		//	Increased after a valid data is received from this peer (minor for header and transaction, major for a block)
		//	Decreased if the peer fails to accomplish the data request ()
		//	Decreased on network error shortly after connect/accept (or inability to connect)
		//	Reset to 0 for banned peers. Triggered upon:
		//		Any protocol violation (including running with incompatible configuration)
		//		invalid block received from this peer
		//
		// Policy wrt peers:
		//	Connection to banned peers is disallowed for at least specified time period (even if no other options left)
		//	We calculate two ratings for all the peers:
		//		Raw rating, based on its behavior
		//		Adjusted rating, which is increased with the starvation time, i.e. how long ago it was connected
		//	The selection of the peer to performed by selecting two (non-overlapping) groups.
		//		Those with highest ratings
		//		Those with highest *adjusted* ratings.
		//	So that we effectively always try to maintain connection with the best peers, but also shuffle and connect to others.
		//
		//	There is a min threshold for connection time, i.e. we won't disconnect shortly after connecting because the rating of this peer went slightly below another candidate

		struct Rating
		{
			static const uint32_t Initial = 1024;
			static const uint32_t RewardHeader = 64;
			static const uint32_t RewardTx = 16;
			static const uint32_t RewardBlock = 512;
			static const uint32_t PenaltyTimeout = 256;
			static const uint32_t PenaltyNetworkErr = 128;
			static const uint32_t Max = 10240; // saturation

			static uint32_t Saturate(uint32_t);
			static void Inc(uint32_t& r, uint32_t delta);
			static void Dec(uint32_t& r, uint32_t delta);
		};

		struct Cfg {
			uint32_t m_DesiredHighest = 5;
			uint32_t m_DesiredTotal = 10;
			uint32_t m_TimeoutDisconnect_ms = 1000 * 60 * 2; // connected for less than 2 minutes -> penalty
			uint32_t m_TimeoutReconnect_ms	= 1000;
			uint32_t m_TimeoutBan_ms		= 1000 * 60 * 10;
			uint32_t m_TimeoutAddrChange_s	= 60 * 60 * 2;
			uint32_t m_StarvationRatioInc	= 1; // increase per second while not connected
			uint32_t m_StarvationRatioDec	= 2; // decrease per second while connected (until starvation reward is zero)
		} m_Cfg;


		struct PeerInfo
		{
			struct ID
				:public boost::intrusive::set_base_hook<>
			{
				PeerID m_Key;
				bool operator < (const ID& x) const { return (m_Key < x.m_Key); }

				IMPLEMENT_GET_PARENT_OBJ(PeerInfo, m_ID)
			} m_ID;

			struct RawRating
				:public boost::intrusive::set_base_hook<>
			{
				uint32_t m_Value;
				bool operator < (const RawRating& x) const { return (m_Value > x.m_Value); } // reverse order, begin - max

				IMPLEMENT_GET_PARENT_OBJ(PeerInfo, m_RawRating)
			} m_RawRating;

			struct AdjustedRating
				:public boost::intrusive::set_base_hook<>
			{
				uint32_t m_Increment;
				uint32_t get() const;
				bool operator < (const AdjustedRating& x) const { return (get() > x.get()); } // reverse order, begin - max

				IMPLEMENT_GET_PARENT_OBJ(PeerInfo, m_AdjustedRating)
			} m_AdjustedRating;

			struct Active
				:public boost::intrusive::list_base_hook<>
			{
				bool m_Now;
				bool m_Next; // used internally during switching
				IMPLEMENT_GET_PARENT_OBJ(PeerInfo, m_Active)
			} m_Active;

			struct Addr
				:public boost::intrusive::set_base_hook<>
			{
				io::Address m_Value;
				bool operator < (const Addr& x) const { return (m_Value < x.m_Value); }

				IMPLEMENT_GET_PARENT_OBJ(PeerInfo, m_Addr)
			} m_Addr;

			Timestamp m_LastSeen; // needed to filter-out dead peers, and to know when to update the address
			uint32_t m_LastActivity_ms; // updated on connection attempt, and disconnection.
		};

		typedef boost::intrusive::multiset<PeerInfo::ID> PeerIDSet;
		typedef boost::intrusive::multiset<PeerInfo::RawRating> RawRatingSet;
		typedef boost::intrusive::multiset<PeerInfo::AdjustedRating> AdjustedRatingSet;
		typedef boost::intrusive::multiset<PeerInfo::Addr> AddrSet;
		typedef boost::intrusive::list<PeerInfo::Active> ActiveList;

		void Update(); // will trigger activation/deactivation of peers
		PeerInfo* Find(const PeerID& id, bool& bCreate);

		void OnActive(PeerInfo&, bool bActive);
		void ModifyRating(PeerInfo&, uint32_t, bool bAdd);
		void Ban(PeerInfo&);
		void OnSeen(PeerInfo&);
		void OnRemoteError(PeerInfo&, bool bShouldBan);

		void ModifyAddr(PeerInfo&, const io::Address&);
		void RemoveAddr(PeerInfo&);

		PeerInfo* OnPeer(const PeerID&, const io::Address&, bool bAddrVerified);

		void Delete(PeerInfo&);
		void Clear();

		virtual void ActivatePeer(PeerInfo&) {}
		virtual void DeactivatePeer(PeerInfo&) {}
		virtual PeerInfo* AllocPeer() = 0;
		virtual void DeletePeer(PeerInfo&) = 0;

		const RawRatingSet& get_Ratings() const { return m_Ratings; }

	private:
		PeerIDSet m_IDs;
		RawRatingSet m_Ratings;
		AdjustedRatingSet m_AdjustedRatings;
		AddrSet m_Addr;
		ActiveList m_Active;
		uint32_t m_TicksLast_ms = 0;

		void UpdateRatingsInternal(uint32_t t_ms);

		void ActivatePeerInternal(PeerInfo&, uint32_t nTicks_ms, uint32_t& nSelected);
		void ModifyRatingInternal(PeerInfo&, uint32_t, bool bAdd, bool ban);
	};


	std::ostream& operator << (std::ostream& s, const PeerManager::PeerInfo&);

} // namespace proto
} // namespace beam
