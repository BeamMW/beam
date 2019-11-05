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

#define BeamNodeMsg_Status(macro) \
    macro(uint8_t, Value)

#define BeamNodeMsg_GetBody(macro) \
    macro(Block::SystemState::ID, ID)

#define BeamNodeMsg_GetBodyPack(macro) \
    macro(Block::SystemState::ID, Top) \
    macro(uint8_t, FlagP) \
    macro(uint8_t, FlagE) \
    macro(Height, CountExtra) \
    macro(Height, Height0) \
    macro(Height, HorizonLo1) \
    macro(Height, HorizonHi1)

#define BeamNodeMsg_Body(macro) \
    macro(BodyBuffers, Body)

#define BeamNodeMsg_BodyPack(macro) \
    macro(std::vector<BodyBuffers>, Bodies)

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

#define BeamNodeMsg_Login0(macro) \
    macro(ECC::Hash::Value, CfgChecksum) \
    macro(uint8_t, Flags)

#define BeamNodeMsg_Login(macro) \
    macro(std::vector<ECC::Hash::Value>, Cfgs) \
    macro(uint32_t, Flags)

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
    macro(ByteBuffer, Message) \
    macro(Bbs::NonceType, Nonce)

#define BeamNodeMsg_BbsHaveMsg(macro) \
    macro(BbsMsgID, Key)

#define BeamNodeMsg_BbsGetMsg(macro) \
    macro(BbsMsgID, Key)

#define BeamNodeMsg_BbsSubscribe(macro) \
    macro(BbsChannel, Channel) \
    macro(Timestamp, TimeFrom) \
    macro(bool, On)

#define BeamNodeMsg_BbsResetSync(macro) \
    macro(Timestamp, TimeFrom)

#define BeamNodeMsg_SChannelInitiate(macro) \
    macro(ECC::uintBig, NoncePub)

#define BeamNodeMsg_SChannelReady(macro)

#define BeamNodeMsg_Authentication(macro) \
    macro(PeerID, ID) \
    macro(uint8_t, IDType) \
    macro(ECC::Signature, Sig)

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
    macro(0x00, Login0) \
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
    macro(0x0e, Status) \
    macro(0x0f, Login) \
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
    /* macro(0x20, MacroblockGet) Deprecated */ \
    /* macro(0x21, Macroblock) Deprecated */ \
    macro(0x22, GetCommonState) \
    macro(0x23, ProofCommonState) \
    macro(0x24, GetProofKernel2) \
    macro(0x25, ProofKernel2) \
    macro(0x26, GetBodyPack) \
    macro(0x27, BodyPack) \
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
    /* macro(0x38, BbsMsgV0) Deprecated */ \
    macro(0x39, BbsHaveMsg) \
    macro(0x3a, BbsGetMsg) \
    macro(0x3b, BbsSubscribe) \
    /* macro(0x3c, BbsPickChannelV0) Deprecated */ \
    /* macro(0x3d, BbsPickChannelResV0) Deprecated */ \
    macro(0x3e, BbsResetSync) \
    macro(0x3f, BbsMsg) \


    struct LoginFlags {
        static const uint8_t SpreadingTransactions  = 0x1; // I'm spreading txs, please send
        static const uint8_t Bbs                    = 0x2; // I'm spreading bbs messages
        static const uint8_t SendPeers              = 0x4; // Please send me periodically peers recommendations
        static const uint8_t MiningFinalization     = 0x8; // I want to finalize block construction for my owned node
        static const uint8_t Extension1             = 0x10; // Supports Bbs with POW, more advanced proof/disproof scheme for SPV clients (?)
        static const uint8_t Extension2             = 0x20; // Supports large HdrPack, BlockPack with parameters
        static const uint8_t Extension3             = 0x40; // Supports Login1, Status (former Boolean) for NewTransaction result, compatible with Fork H1
	    static const uint8_t Recognized             = 0x7f;


		static const uint8_t ExtensionsBeforeHF1 =
			Extension1 |
			Extension2 |
			Extension3;

		static const uint8_t ExtensionsAll =
			ExtensionsBeforeHF1;
	};

    struct IDType
    {
        static const uint8_t Node        = 'N';
        static const uint8_t Owner        = 'O';
        static const uint8_t Viewer        = 'V';
    };

	static const uint32_t g_HdrPackMaxSize = 2048; // about 400K

    struct UtxoEvent
    {
        static const uint32_t s_Max = 64; // will send more, if the remaining events are on the same height

        Key::IDV m_Kidv;
        ECC::Point m_Commitment;
        AssetID m_AssetID;

        Height m_Height;
        Height m_Maturity;

        uint8_t m_Added; // 1 = add, 0 = spend


        template <typename Archive>
        void serialize(Archive& ar)
        {
            ar
                & m_Commitment
                & m_Kidv
                & m_AssetID
                & m_Height
                & m_Maturity
                & m_Added;
        }
    };

	struct BodyBuffers
	{
		ByteBuffer m_Perishable;
		ByteBuffer m_Eternal;
	
	    template <typename Archive>
	    void serialize(Archive& ar)
	    {
	        ar
	            & m_Perishable
	            & m_Eternal;
	    }

		// flags w.r.t. body request
		static const uint8_t Full = 0; // default
		static const uint8_t None = 1;
		static const uint8_t Recovery1 = 2; // part suitable for recovery (version 1). Suitable for Outputs

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
	inline void ZeroInit(BodyBuffers&) { }

    template <typename T> struct InitArg {
        typedef const T& TArg;
        static void Set(T& var, TArg arg) { var = arg; }
    };

    template <typename T> struct InitArg<std::unique_ptr<T> > {
        typedef std::unique_ptr<T>& TArg;
        static void Set(std::unique_ptr<T>& var, TArg arg) { var = std::move(arg); }
    };

	namespace Bbs
	{
		static const size_t s_MaxMsgSize = 1024 * 1024;

		static const uint32_t s_MaxChannels = 1024;
		// At peak load a single block contains ~1K txs. The lifetime of a bbs message is 12-24 hours. Means the total sbbs system can contain simultaneously info about ~1 million different txs.
		// Hence our sharding factor is 1K. Gives decent reduction of the traffic under peak loads, whereas maintains some degree of obfuscation on modest loads too.
		// In the future it can be changed without breaking compatibility

		typedef uintBig_t<4> NonceType;

		bool Encrypt(ByteBuffer& res, const PeerID& publicAddr, ECC::Scalar::Native& nonce, const void*, uint32_t); // will fail iff addr is invalid
		bool Decrypt(uint8_t*& p, uint32_t& n, const ECC::Scalar::Native& privateAddr);
	};

	struct TxStatus
	{
		// for backward compatibility, since it's former Boolean
		static const uint8_t Unspecified = 0;
		static const uint8_t Ok = 0x1;
		// advanced codes
		static const uint8_t TooSmall = 0x2; // doesn't contain minimal elements: at least 1 input and 1 kernel
		static const uint8_t Obscured = 0x3; // partial overlap with another tx. Dropped due to potential collision (not necessarily an error)

		static const uint8_t Invalid = 0x10; // context-free validation failed
		static const uint8_t InvalidContext = 0x11; // invalid in context (bad inputs, maturity or time lock problems)
		static const uint8_t LowFee = 0x12; // fee below minimum
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


	namespace Bbs
	{
		void get_HashPartial(ECC::Hash::Processor&, const BbsMsg&); // all except time and nonce
		void get_Hash(ECC::Hash::Value&, const BbsMsg&);
		bool IsHashValid(const ECC::Hash::Value&);
	}

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

    class NodeProcessingException : public std::runtime_error
    {
    public:
        enum class Type : uint8_t
        {
            Base,
            Incompatible,
			TimeOutOfSync,
        };

        NodeProcessingException(const std::string& str, Type type)
            : std::runtime_error(str)
            , m_type(type)
        {
        }

        Type type() const { return m_type; }

    private:
        Type m_type;
    };

    class NodeConnection
        :public INodeMsgHandler
    {
        ProtocolPlus m_Protocol;
        std::unique_ptr<Connection> m_Connection;
        io::AsyncEvent::Ptr m_pAsyncFail;
        bool m_ConnectPending;
		bool m_RulesCfgSent;

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

		void OnLoginInternal(Height hPeerMaxScheme, Login&&);

    public:

        NodeConnection();
        virtual ~NodeConnection();
        void Reset();

        static void ThrowUnexpected(const char* = NULL, NodeProcessingException::Type type = NodeProcessingException::Type::Base);

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
		virtual void OnMsg(Ping&&) override;
		virtual void OnMsg(GetTime&&) override;
		virtual void OnMsg(Time&&) override;
		virtual void OnMsg(Login0&&) override;
		virtual void OnMsg(Login&&) override;

        virtual void GenerateSChannelNonce(ECC::Scalar::Native&); // Must be overridden to support SChannel

		// Login-specific
		void SendLogin();
		virtual void SetupLogin(Login&);
		virtual void OnLogin(Login&&);
		virtual Height get_MinPeerFork();

        bool IsLive() const;
        bool IsSecureIn() const;
        bool IsSecureOut() const;

        const Connection* get_Connection() { return m_Connection.get(); }

        virtual void OnConnectedSecure() {}

        struct ByeReason
        {
            static const uint8_t Stopping    = 's';
            static const uint8_t Ban        = 'b';
            static const uint8_t Loopback    = 'L';
            static const uint8_t Duplicate    = 'd';
            static const uint8_t Timeout    = 't';
            static const uint8_t Other        = 'o';
        };

        struct DisconnectReason
        {
            DisconnectReason() {}
            DisconnectReason(const DisconnectReason&) = delete;

            enum Enum {
                Io,
                Protocol,
                ProcessingExc,
                Bye,
				Drown
            };

            struct ExceptionDetails
            {
                NodeProcessingException::Type m_ExceptionType = NodeProcessingException::Type::Base;
                const char* m_szErrorMsg = nullptr;
            };

            Enum m_Type;

            union {
                io::ErrorCode m_IoError;
                ProtocolError m_eProtoCode;
                uint8_t m_ByeReason;
                ExceptionDetails m_ExceptionDetails;
            };
        };

        virtual void OnDisconnect(const DisconnectReason&) {}

		size_t get_Unsent() const;
		size_t m_UnsentHiMark = 0;
		void TestNotDrown();

        void OnIoErr(io::ErrorCode);
        void OnExc(const std::exception&);
        void OnProcessingExc(const NodeProcessingException& exception);

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
