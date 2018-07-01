#pragma once

#include "common.h"
#include "../utility/bridge.h"
#include "../p2p/protocol.h"
#include "../p2p/connection.h"
#include "../utility/io/tcpserver.h"
#include "../tiny-AES/aes.hpp"
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>

namespace beam {
namespace proto {

#define BeamNodeMsg_NewTip(macro) \
	macro(Block::SystemState::ID, ID)

#define BeamNodeMsg_GetHdr(macro) \
	macro(Block::SystemState::ID, ID)

#define BeamNodeMsg_Hdr(macro) \
	macro(Block::SystemState::Full, Description)

#define BeamNodeMsg_DataMissing(macro)

#define BeamNodeMsg_Boolean(macro) \
	macro(bool, Value)

#define BeamNodeMsg_GetBody(macro) \
	macro(Block::SystemState::ID, ID)

#define BeamNodeMsg_Body(macro) \
	macro(ByteBuffer, Buffer)

#define BeamNodeMsg_GetProofState(macro) \
	macro(Height, Height)

#define BeamNodeMsg_GetProofKernel(macro) \
	macro(Merkle::Hash, KernelHash)

#define BeamNodeMsg_GetProofUtxo(macro) \
	macro(Input, Utxo) \
	macro(Height, MaturityMin) /* set to non-zero in case the result is too big, and should be retrieved within multiple queries */


#define BeamNodeMsg_Proof(macro) \
	macro(Merkle::Proof, Proof)

#define BeamNodeMsg_ProofUtxo(macro) \
	macro(std::vector<Input::Proof>, Proofs)

#define BeamNodeMsg_GetMined(macro) \
	macro(Height, HeightMin)

#define BeamNodeMsg_Mined(macro) \
	macro(std::vector<PerMined>, Entries)

#define BeamNodeMsg_Config(macro) \
	macro(ECC::Hash::Value, CfgChecksum) \
	macro(bool, SpreadingTransactions) \
	macro(bool, Mining) \
	macro(bool, AutoSendHdr) /* prefer the header in addition to the NewTip message */

#define BeamNodeMsg_Ping(macro)
#define BeamNodeMsg_Pong(macro)

#define BeamNodeMsg_NewTransaction(macro) \
	macro(Transaction::Ptr, Transaction)

#define BeamNodeMsg_HaveTransaction(macro) \
	macro(Transaction::KeyType, ID)

#define BeamNodeMsg_GetTransaction(macro) \
	macro(Transaction::KeyType, ID)

#define BeamNodeMsg_GetTopPeers(macro)

#define BeamNodeMsg_TopPeers(macro) \
	macro(std::vector<PerPeer>, Peers)

#define BeamNodeMsg_SChannelInitiate(macro) \
	macro(ECC::Point, NoncePub)

#define BeamNodeMsg_SChannelReady(macro)

#define BeamNodeMsg_SChannelAuthentication(macro) \
	macro(ECC::Point, MyID) \
	macro(ECC::Signature, Sig)

#define BeamNodeMsgsAll(macro) \
	macro(1, NewTip) /* Also the first message sent by the node */ \
	macro(2, GetHdr) \
	macro(3, Hdr) \
	macro(4, DataMissing) \
	macro(5, Boolean) \
	macro(6, GetBody) \
	macro(7, Body) \
	macro(8, GetProofState) \
	macro(9, GetProofKernel) \
	macro(10, GetProofUtxo) \
	macro(11, Proof) /* for states and kernels */ \
	macro(12, ProofUtxo) \
	macro(15, GetMined) \
	macro(16, Mined) \
	macro(20, Config) /* usually sent by node once when connected, but theoretically me be re-sent if cfg changes. */ \
	macro(21, Ping) \
	macro(22, Pong) \
	macro(23, NewTransaction) \
	macro(24, HaveTransaction) \
	macro(25, GetTransaction) \
	macro(31, GetTopPeers) \
	macro(32, TopPeers) \
	macro(61, SChannelInitiate) \
	macro(62, SChannelReady) \
	macro(63, SChannelAuthentication) \


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

	struct PerPeer
	{
		PeerID m_ID;
		io::Address m_LastAddr;

		template <typename Archive>
		void serialize(Archive& ar)
		{
			ar
				& m_ID
				& m_LastAddr;
		}
	};

#define THE_MACRO3(type, name) & m_##name
#define THE_MACRO2(type, name) type m_##name;
#define THE_MACRO1(code, msg) \
	struct msg \
	{ \
		BeamNodeMsg_##msg(THE_MACRO2) \
		template <typename Archive> void serialize(Archive& ar) { ar BeamNodeMsg_##msg(THE_MACRO3); } \
	};

	BeamNodeMsgsAll(THE_MACRO1)
#undef THE_MACRO1
#undef THE_MACRO2
#undef THE_MACRO3

	struct ProtocolPlus
		:public Protocol
	{
		struct Cipher
			:public AES_StreamCipher
		{
			bool m_bON;
		};

		Cipher m_CipherIn;
		Cipher m_CipherOut;

		bool m_bHandshakeSent;

		ECC::NoLeak<ECC::Scalar> m_MyNonce;
		ECC::Point m_RemoteNonce;
		ECC::Point m_RemoteID;

		ProtocolPlus(uint8_t v0, uint8_t v1, uint8_t v2, size_t maxMessageTypes, IErrorHandler& errorHandler, size_t serializedFragmentsSize);
		void ResetVars();
		void InitCipher(Cipher&);

		// Protocol
		virtual void Decrypt(uint8_t*, uint32_t nSize) override;

		void Encrypt(SerializedMsg&);
	};

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
		bool m_ConnectPending;

		SerializedMsg m_SerializeCache;

		static void TestIoResult(const io::Result& res);

		static void OnConnectInternal(uint64_t tag, io::TcpStream::Ptr&& newStream, int status);
		void OnConnectInternal2(io::TcpStream::Ptr&& newStream, int status);

		virtual void on_protocol_error(uint64_t, ProtocolError error) override;
		virtual void on_connection_error(uint64_t, io::ErrorCode errorCode) override;

#define THE_MACRO(code, msg) bool OnMsgInternal(uint64_t, msg&& v);
		BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO

	public:

		NodeConnection();
		virtual ~NodeConnection();
		void Reset();

		void Connect(const io::Address& addr);
		void Accept(io::TcpStream::Ptr&& newStream);

		// Secure-channel-specific
		void SecureConnect(); // must be connected already

		virtual void OnMsg(SChannelInitiate&&) override;
		virtual void OnMsg(SChannelReady&&) override;
		virtual void OnMsg(SChannelAuthentication&&) override;

		virtual void get_MyID(ECC::Scalar::Native&); // by default no-ID (secure channel, but no authentication)
		virtual void GenerateSChannelNonce(ECC::Scalar&); // Must be overridden to support SChannel

		bool IsSecureIn() const;
		bool IsSecureOut() const;
		const ECC::Point* get_RemoteID() const;


		const Connection* get_Connection() { return m_Connection.get(); }

		virtual void OnConnected() {}
		virtual void OnClosed(int errorCode) {}

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

			static void Inc(uint32_t& r, uint32_t delta) {
				r = std::min(r + delta, Max);
			}
			static void Dec(uint32_t& r, uint32_t delta) {
				r = (r > delta) ? (r - delta) : 1;
			}
		};

		struct Cfg {
			uint32_t m_DesiredHighest = 5;
			uint32_t m_DesiredTotal = 10;
			uint32_t m_TimeoutLo_ms = 60 * 2; // connected for less than 2 minutes -> penalty
			uint32_t m_StarvationRatioInc = 1; // increase per second while not connected
			uint32_t m_StarvationRatioDec = 2; // decrease per second while connected (until starvation reward is zero)
			uint32_t m_BanTimeout_s = 60 * 10;
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
				uint32_t get() const {
					AdjustedRating* p = (AdjustedRating*)this;
					return std::min(p->get_ParentObj().m_RawRating.m_Value + m_Increment, Rating::Max);
				}

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

			Timestamp m_LastSeen; // needed to filter-out dead peers, and to know when to update the address
			io::Address m_LastAddr; // address for Connect(), not the accepted (the port is likely to be different)
		};

		typedef boost::intrusive::multiset<PeerInfo::ID> PeerIDSet;
		typedef boost::intrusive::multiset<PeerInfo::RawRating> RawRatingSet;
		typedef boost::intrusive::multiset<PeerInfo::AdjustedRating> AdjustedRatingSet;
		typedef boost::intrusive::list<PeerInfo::Active> ActiveList;

		void UpdateTime(Timestamp t);
		PeerInfo* Find(const PeerID& id, bool& bCreate);

		void OnActive(PeerInfo&, bool bActive);
		void ModifyRating(PeerInfo&, uint32_t, bool bAdd, bool ban = false);

		PeerInfo* OnPeer(const PeerID&, const io::Address&, bool bAddrVerified);

		virtual void ActivatePeer(PeerInfo&) {}
		virtual void DeactivatePeer(PeerInfo&) {}

	private:
		PeerIDSet m_IDs;
		RawRatingSet m_Ratings;
		AdjustedRatingSet m_AdjustedRatings;
		ActiveList m_Active;
		Timestamp m_tLast = 0;

		void UpdateTimeInternal(Timestamp t);
		void ActivatePeerInternal(PeerInfo&, uint32_t& nSelected);
	};


} // namespace proto
} // namespace beam
