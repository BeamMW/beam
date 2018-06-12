#include "../../utility/serialize.h"
#include "../../core/serialization_adapters.h"
#include "proto.h"

namespace beam {
namespace proto {

/////////////////////////
// NodeConnection
NodeConnection::NodeConnection()
	:m_Protocol(0xAA, 0xBB, 0xCC, 100, *this, 20000)
	, m_ConnectPending(false)
{
#define THE_MACRO(code, msg) m_Protocol.add_message_handler<NodeConnection, msg, &NodeConnection::OnMsgInternal>(uint8_t(code), this, 1, 2000000);
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

void NodeConnection::on_connection_error(uint64_t, int errorCode)
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
	return true; \
} \

BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO


/////////////////////////
// NodeConnection::Server
void NodeConnection::Server::Listen(const io::Address& addr)
{
	m_pServer = io::TcpServer::create(io::Reactor::get_Current().shared_from_this(), addr, BIND_THIS_MEMFN(OnAccepted));
}



} // namespace proto
} // namespace beam
