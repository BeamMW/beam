#include "../../utility/serialize.h"
#include "../../core/serialization_adapters.h"
#include "../../core/ecc_native.h"
#include "proto.h"

namespace beam {
namespace proto {

/////////////////////////
// ProtocolPlus
ProtocolPlus::ProtocolPlus(uint8_t v0, uint8_t v1, uint8_t v2, size_t maxMessageTypes, IErrorHandler& errorHandler, size_t serializedFragmentsSize)
	:Protocol(v0, v1, v2, maxMessageTypes, errorHandler, serializedFragmentsSize)
{
	ResetVars();
}

void ProtocolPlus::ResetVars()
{
	m_bHandshakeSent = false;
	m_CipherIn.m_bON = false;
	m_CipherOut.m_bON = false;
	ZeroObject(m_MyNonce);
	ZeroObject(m_RemoteNonce);
	ZeroObject(m_RemoteID);
}

void ProtocolPlus::Decrypt(uint8_t* p, uint32_t nSize)
{
	if (m_CipherIn.m_bON)
		m_CipherIn.XCrypt(p, nSize);
}

void ProtocolPlus::Encrypt(SerializedMsg& sm)
{
	if (m_CipherOut.m_bON)
		for (auto i = 0; i < sm.size(); i++)
		{
			io::IOVec& iov = sm[i];
			m_CipherOut.XCrypt((uint8_t*) iov.data, (uint32_t) iov.size);
		}
}

void ProtocolPlus::InitCipher(Cipher& c)
{
	assert(!(m_MyNonce.V.m_Value == ECC::Zero));

	 // Diffie-Hellman
	ECC::Mode::Scope scope(ECC::Mode::Fast); // TODO !!!
	ECC::Point::Native ptSecret = ECC::Point::Native(m_RemoteNonce) * ECC::Scalar::Native(m_MyNonce.V);

	ECC::NoLeak<ECC::Hash::Processor> hp;
	ECC::NoLeak<ECC::Hash::Value> hvSecret;
	hp.V << ptSecret >> hvSecret.V;

	static_assert(AES_KEYLEN == sizeof(hvSecret.V), "");
	static_assert(AES_BLOCKLEN <= sizeof(hvSecret.V), "");

	c.Init(hvSecret.V.m_pData, hvSecret.V.m_pData);
	c.m_bON = true;
}

/////////////////////////
// NodeConnection
NodeConnection::NodeConnection()
	:m_Protocol(0xAA, 0xBB, 0xCC, 100, *this, 20000)
	,m_ConnectPending(false)
{
#define THE_MACRO(code, msg) m_Protocol.add_message_handler<NodeConnection, msg, &NodeConnection::OnMsgInternal>(uint8_t(code), this, 0, 2000000);
	BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO
}

NodeConnection::~NodeConnection()
{
	Reset();
}

void NodeConnection::Reset()
{
	if (m_ConnectPending)
	{
		io::Reactor::get_Current().cancel_tcp_connect(uint64_t(this));
		m_ConnectPending = false;
	}

	m_Connection = NULL;

	m_Protocol.ResetVars();
}


void NodeConnection::TestIoResult(const io::Result& res)
{
	if (!res)
		throw std::runtime_error(io::error_descr(res.error()));
}

void NodeConnection::OnConnectInternal(uint64_t tag, io::TcpStream::Ptr&& newStream, int status)
{
	NodeConnection* pThis = (NodeConnection*)tag;
	assert(pThis);
	pThis->OnConnectInternal2(std::move(newStream), status);
}

void NodeConnection::OnConnectInternal2(io::TcpStream::Ptr&& newStream, int status)
{
	assert(!m_Connection && m_ConnectPending);
	m_ConnectPending = false;

	if (newStream)
	{
		Accept(std::move(newStream));

		try {
			OnConnected();
		} catch (...) {
			OnClosed(-1);
		}
	}
	else
		OnClosed(status);
}

void NodeConnection::on_protocol_error(uint64_t, ProtocolError error)
{
	Reset();
	OnClosed(-1);
}

void NodeConnection::on_connection_error(uint64_t, io::ErrorCode errorCode)
{
	Reset();
	OnClosed(errorCode);
}

void NodeConnection::Connect(const io::Address& addr)
{
	assert(!m_Connection && !m_ConnectPending);

	io::Result res = io::Reactor::get_Current().tcp_connect(
		addr,
		uint64_t(this),
		OnConnectInternal);

	TestIoResult(res);
	m_ConnectPending = true;
}

void NodeConnection::Accept(io::TcpStream::Ptr&& newStream)
{
	assert(!m_Connection && !m_ConnectPending);

	m_Connection = std::make_unique<Connection>(
		m_Protocol,
		uint64_t(this),
        Connection::inbound,
		100,
		std::move(newStream)
		);
}

#define THE_MACRO(code, msg) \
void NodeConnection::Send(const msg& v) \
{ \
	m_SerializeCache.clear(); \
	m_Protocol.serialize(m_SerializeCache, uint8_t(code), v); \
	m_Protocol.Encrypt(m_SerializeCache); \
	io::Result res = m_Connection->write_msg(m_SerializeCache); \
	m_SerializeCache.clear(); \
\
	TestIoResult(res); \
} \
\
bool NodeConnection::OnMsgInternal(uint64_t, msg&& v) \
{ \
	try { \
		/* checkpoint */ \
        return OnMsg2(std::move(v)); \
	} catch (...) { \
		OnClosed(-1); \
		return false; \
	} \
} \

BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO

const ECC::Point* NodeConnection::get_RemoteID() const
{
	return (m_Protocol.m_CipherIn.m_bON && !(m_Protocol.m_RemoteID.m_X == ECC::Zero)) ? &m_Protocol.m_RemoteID : NULL;
}

void NodeConnection::get_MyID(ECC::Scalar::Native& sk)
{
	sk = ECC::Zero;
}

void NodeConnection::GenerateSChannelNonce(ECC::Scalar&)
{
	// unsupported
}

void NodeConnection::SecureConnect()
{
	if (!m_Protocol.m_bHandshakeSent)
	{
		if (m_Protocol.m_MyNonce.V.m_Value == ECC::Zero)
		{
			GenerateSChannelNonce(m_Protocol.m_MyNonce.V);

			if (m_Protocol.m_MyNonce.V.m_Value == ECC::Zero)
				throw std::runtime_error("SChannel not supported");
		}

		assert(!(m_Protocol.m_MyNonce.V.m_Value == ECC::Zero));
		m_Protocol.m_bHandshakeSent = true;

		SChannelInitiate msg;
		msg.m_NoncePub = ECC::Context::get().G * m_Protocol.m_MyNonce.V;
		Send(msg);
	}
}

void NodeConnection::OnMsg(SChannelInitiate&& msg)
{
	SecureConnect(); // unless already sent

	SChannelReady msgOut1;
	Send(msgOut1); // activating new cipher.

	m_Protocol.m_RemoteNonce = msg.m_NoncePub;
	m_Protocol.InitCipher(m_Protocol.m_CipherOut);

	// confirm our ID
	ECC::Hash::Value hv;
	ECC::Hash::Processor() << msg.m_NoncePub >> hv;

	ECC::Scalar::Native sk;
	get_MyID(sk);

	SChannelAuthentication msgOut2;
	msgOut2.m_MyID = ECC::Context::get().G * sk;
	msgOut2.m_Sig.Sign(hv, sk);
	Send(msgOut2);
}

void NodeConnection::OnMsg(SChannelReady&& msg)
{
	if (!m_Protocol.m_CipherOut.m_bON)
		throw std::runtime_error("peer insane");

	ECC::NoLeak<ECC::Hash::Value> hv;
	m_Protocol.InitCipher(m_Protocol.m_CipherIn);
}

void NodeConnection::OnMsg(SChannelAuthentication&& msg)
{
	if (!m_Protocol.m_CipherIn.m_bON)
		throw std::runtime_error("peer insane");

	// verify ID
	ECC::Point::Native pt = ECC::Context::get().G * m_Protocol.m_MyNonce.V;
	ECC::Hash::Value hv;
	ECC::Hash::Processor() << pt >> hv;

	if (!msg.m_Sig.IsValid(hv, msg.m_MyID))
		throw std::runtime_error("peer insane");

	m_Protocol.m_RemoteID = msg.m_MyID;
}

/////////////////////////
// NodeConnection::Server
void NodeConnection::Server::Listen(const io::Address& addr)
{
	m_pServer = io::TcpServer::create(io::Reactor::get_Current().shared_from_this(), addr, BIND_THIS_MEMFN(OnAccepted));
}



} // namespace proto
} // namespace beam
