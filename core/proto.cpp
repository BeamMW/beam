#include "../../utility/serialize.h"
#include "../../core/serialization_adapters.h"
#include "../../core/ecc_native.h"
#include "proto.h"
#include "../utility/logger.h"
#include "../utility/logger_checkpoints.h"

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
	m_CipherIn.m_bON = false;
	m_CipherOut.m_bON = false;
	ZeroObject(m_MyNonce);
	ZeroObject(m_RemoteNonce);
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
	ECC::Point pt;
	pt.m_X = m_RemoteNonce;
	pt.m_Y = false;
	ECC::Point::Native ptSecret = ECC::Point::Native(pt) * ECC::Scalar::Native(m_MyNonce.V);

	ECC::NoLeak<ECC::Hash::Processor> hp;
	ECC::NoLeak<ECC::Hash::Value> hvSecret;
	hp.V << ptSecret >> hvSecret.V;

	static_assert(AES::s_KeyBytes == sizeof(hvSecret.V), "");
	c.Init(hvSecret.V.m_pData);

	hp.V << hvSecret.V >> hvSecret.V; // IV

	static_assert(sizeof(hvSecret.V) >= sizeof(c.m_Counter), "");
	memcpy(c.m_Counter.m_pData, hvSecret.V.m_pData, sizeof(c.m_Counter.m_pData));

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
	m_pAsyncFail = NULL;

	m_Protocol.ResetVars();
}


void NodeConnection::TestIoResultAsync(const io::Result& res)
{
	if (res)
		return; // ok

	if (m_pAsyncFail)
		return;

	io::ErrorCode err = res.error();

	io::AsyncEvent::Callback cb = [this, err]() {
		OnIoErr(err);
	};

	m_pAsyncFail = io::AsyncEvent::create(io::Reactor::get_Current().shared_from_this(), std::move(cb));
	m_pAsyncFail->trigger();
}

void NodeConnection::OnConnectInternal(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode status)
{
	NodeConnection* pThis = (NodeConnection*)tag;
	assert(pThis);
	pThis->OnConnectInternal2(std::move(newStream), status);
}

void NodeConnection::OnConnectInternal2(io::TcpStream::Ptr&& newStream, io::ErrorCode status)
{
	assert(!m_Connection && m_ConnectPending);
	m_ConnectPending = false;

	if (newStream)
	{
		Accept(std::move(newStream));

		try {
			OnConnected();
		}
		catch (const std::exception& e) {
			OnExc(e);
		}
	}
	else
		OnIoErr(status);
}

void NodeConnection::OnExc(const std::exception& e)
{
	DisconnectReason r;
	r.m_Type = DisconnectReason::ProcessingExc;
	r.m_szErrorMsg = e.what();
	OnDisconnect(r);
}

void NodeConnection::OnIoErr(io::ErrorCode err)
{
	DisconnectReason r;
	r.m_Type = DisconnectReason::Io;
	r.m_IoError = err;
	OnDisconnect(r);
}

void NodeConnection::on_protocol_error(uint64_t, ProtocolError error)
{
	Reset();

	DisconnectReason r;
	r.m_Type = DisconnectReason::Protocol;
	r.m_eProtoCode = error;
	OnDisconnect(r);
}

std::ostream& operator << (std::ostream& s, const NodeConnection::DisconnectReason& r)
{
	switch (r.m_Type)
	{
	case NodeConnection::DisconnectReason::Io:
		s << io::error_descr(r.m_IoError);
		break;

	case NodeConnection::DisconnectReason::Protocol:
		s << "Protocol " << r.m_eProtoCode;
		break;

	case NodeConnection::DisconnectReason::ProcessingExc:
		s << r.m_szErrorMsg;
		break;

	default:
		assert(false);
	}
	return s;
}

void NodeConnection::on_connection_error(uint64_t, io::ErrorCode errorCode)
{
	Reset();
	OnIoErr(errorCode);
}

void NodeConnection::ThrowUnexpected(const char* sz)
{
	throw std::runtime_error(sz ? sz : "proto violation");
}

void NodeConnection::Connect(const io::Address& addr)
{
	assert(!m_Connection && !m_ConnectPending);

	io::Result res = io::Reactor::get_Current().tcp_connect(
		addr,
		uint64_t(this),
		OnConnectInternal);

	TestIoResultAsync(res);
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
	if (m_pAsyncFail) \
		return; \
	m_SerializeCache.clear(); \
	m_Protocol.serialize(m_SerializeCache, uint8_t(code), v); \
	m_Protocol.Encrypt(m_SerializeCache); \
	io::Result res = m_Connection->write_msg(m_SerializeCache); \
	m_SerializeCache.clear(); \
\
	TestIoResultAsync(res); \
} \
\
bool NodeConnection::OnMsgInternal(uint64_t, msg&& v) \
{ \
	try { \
		/* checkpoint */ \
        return OnMsg2(std::move(v)); \
	} catch (const std::exception& e) { \
		OnExc(e); \
		return false; \
	} \
} \

BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO

void NodeConnection::Sk2Pk(PeerID& res, ECC::Scalar::Native& sk)
{
	ECC::Point pt = ECC::Point::Native(ECC::Context::get().G * sk);
	if (pt.m_Y)
		sk = -sk;

	res = pt.m_X;
}

void NodeConnection::GenerateSChannelNonce(ECC::Scalar::Native& sk)
{
	ECC::NoLeak<ECC::uintBig> secret;
	ECC::GenRandom(secret.V.m_pData, sizeof(secret.V.m_pData));

	ECC::Hash::Value hv;
	hv = ECC::Zero;
	sk.GenerateNonce(secret.V, hv, NULL);
}

void NodeConnection::SecureConnect()
{
	if (!(m_Protocol.m_MyNonce.V.m_Value == ECC::Zero))
		return; // already sent

	ECC::Scalar::Native nonce;
	GenerateSChannelNonce(nonce);

	if (nonce == ECC::Zero)
		ThrowUnexpected("SChannel not supported");

	SChannelInitiate msg;
	Sk2Pk(msg.m_NoncePub, nonce);
	Send(msg);

	m_Protocol.m_MyNonce.V = nonce;
}

void NodeConnection::OnMsg(SChannelInitiate&& msg)
{
	SecureConnect(); // unless already sent

	SChannelReady msgOut;
	Send(msgOut); // activating new cipher.

	m_Protocol.m_RemoteNonce = msg.m_NoncePub;
	m_Protocol.InitCipher(m_Protocol.m_CipherOut);
}

void NodeConnection::OnMsg(SChannelReady&& msg)
{
	if (!m_Protocol.m_CipherOut.m_bON)
		ThrowUnexpected();

	ECC::NoLeak<ECC::Hash::Value> hv;
	m_Protocol.InitCipher(m_Protocol.m_CipherIn);
}

void NodeConnection::ProveID(ECC::Scalar::Native& sk, uint8_t nIDType)
{
	assert(IsSecureOut());

	// confirm our ID
	ECC::Hash::Value hv;
	ECC::Hash::Processor() << m_Protocol.m_RemoteNonce >> hv;

	Authentication msgOut;
	msgOut.m_IDType = nIDType;
	Sk2Pk(msgOut.m_ID, sk);
	msgOut.m_Sig.Sign(hv, sk);

	Send(msgOut);
}

bool NodeConnection::IsSecureIn() const
{
	return m_Protocol.m_CipherIn.m_bON;
}

bool NodeConnection::IsSecureOut() const
{
	return m_Protocol.m_CipherOut.m_bON;
}

void NodeConnection::OnMsg(Authentication&& msg)
{
	if (!m_Protocol.m_CipherIn.m_bON)
		ThrowUnexpected();

	// verify ID
	ECC::Scalar::Native sk = m_Protocol.m_MyNonce.V;
	PeerID myPubNonce;
	Sk2Pk(myPubNonce, sk);

	ECC::Hash::Value hv;
	ECC::Hash::Processor() << myPubNonce >> hv;

	ECC::Point pt;
	pt.m_X = msg.m_ID;
	pt.m_Y = false;

	if (!msg.m_Sig.IsValid(hv, pt))
		ThrowUnexpected();
}

/////////////////////////
// NodeConnection::Server
void NodeConnection::Server::Listen(const io::Address& addr)
{
	m_pServer = io::TcpServer::create(io::Reactor::get_Current().shared_from_this(), addr, BIND_THIS_MEMFN(OnAccepted));
}

/////////////////////////
// PeerManager
uint32_t PeerManager::Rating::Saturate(uint32_t v)
{
	// try not to take const refernce on Max, so its value can directly be substituted (otherwise gcc link error)
	return (v < Max) ? v : Max;
}

void PeerManager::Rating::Inc(uint32_t& r, uint32_t delta)
{
	r = Saturate(r + delta);
}

void PeerManager::Rating::Dec(uint32_t& r, uint32_t delta)
{
	r = (r > delta) ? (r - delta) : 1;
}

uint32_t PeerManager::PeerInfo::AdjustedRating::get() const
{
	return Rating::Saturate(get_ParentObj().m_RawRating.m_Value + m_Increment);
}

void PeerManager::Update()
{
	uint32_t nTicks_ms = GetTimeNnz_ms();

	if (m_TicksLast_ms)
		UpdateRatingsInternal(nTicks_ms);

	m_TicksLast_ms = nTicks_ms;

	// select recommended peers
	uint32_t nSelected = 0;

	for (ActiveList::iterator it = m_Active.begin(); m_Active.end() != it; it++)
	{
		PeerInfo& pi = it->get_ParentObj();
		assert(pi.m_Active.m_Now);

		bool bTooEarlyToDisconnect = (nTicks_ms - pi.m_LastActivity_ms < m_Cfg.m_TimeoutDisconnect_ms);

		it->m_Next = bTooEarlyToDisconnect;
		if (bTooEarlyToDisconnect)
			nSelected++;
	}

	// 1st group
	uint32_t nHighest = 0;
	for (RawRatingSet::iterator it = m_Ratings.begin(); (nHighest < m_Cfg.m_DesiredHighest) && (nSelected < m_Cfg.m_DesiredTotal) && (m_Ratings.end() != it); it++, nHighest++)
		ActivatePeerInternal(it->get_ParentObj(), nTicks_ms, nSelected);

	// 2nd group
	for (AdjustedRatingSet::iterator it = m_AdjustedRatings.begin(); (nSelected < m_Cfg.m_DesiredTotal) && (m_AdjustedRatings.end() != it); it++)
		ActivatePeerInternal(it->get_ParentObj(), nTicks_ms, nSelected);

	// remove excess
	for (ActiveList::iterator it = m_Active.begin(); m_Active.end() != it; )
	{
		PeerInfo& pi = (it++)->get_ParentObj();
		assert(pi.m_Active.m_Now);

		if (!pi.m_Active.m_Next)
		{
			OnActive(pi, false);
			DeactivatePeer(pi);
		}
	}
}

void PeerManager::ActivatePeerInternal(PeerInfo& pi, uint32_t nTicks_ms, uint32_t& nSelected)
{
	if (pi.m_Active.m_Now && pi.m_Active.m_Next)
		return; // already selected

	if (pi.m_Addr.m_Value.empty())
		return; // current adddress unknown

	if (!pi.m_Active.m_Now && (nTicks_ms - pi.m_LastActivity_ms < m_Cfg.m_TimeoutReconnect_ms))
		return; // too early for reconnect

	nSelected++;

	pi.m_Active.m_Next = true;

	if (!pi.m_Active.m_Now)
	{
		OnActive(pi, true);
		ActivatePeer(pi);
	}
}

void PeerManager::UpdateRatingsInternal(uint32_t t_ms)
{
	// calc dt in seconds, resistant to rounding
	uint32_t dt_ms = t_ms - m_TicksLast_ms;
	uint32_t dt_s = dt_ms / 1000;
	if ((t_ms % 1000) < (m_TicksLast_ms % 1000))
		dt_s++;

	uint32_t rInc = dt_s * m_Cfg.m_StarvationRatioInc;
	uint32_t rDec = dt_s * m_Cfg.m_StarvationRatioDec;

	// First unban peers
	for (RawRatingSet::reverse_iterator it = m_Ratings.rbegin(); m_Ratings.rend() != it; )
	{
		PeerInfo& pi = (it++)->get_ParentObj();
		if (pi.m_RawRating.m_Value)
			break; // starting from this - not banned

		assert(!pi.m_AdjustedRating.m_Increment); // shouldn't be adjusted while banned

		uint32_t dtThis_ms = t_ms - pi.m_LastActivity_ms;
		if (dtThis_ms >= m_Cfg.m_TimeoutBan_ms)
			ModifyRatingInternal(pi, 1, true, false);
	}

	// For inactive peers: modify adjusted ratings in-place, no need to rebuild the tree. For active (presumably lesser part) rebuild is necessary
	for (AdjustedRatingSet::iterator it = m_AdjustedRatings.begin(); m_AdjustedRatings.end() != it; )
	{
		PeerInfo& pi = (it++)->get_ParentObj();

		if (pi.m_Active.m_Now)
			m_AdjustedRatings.erase(AdjustedRatingSet::s_iterator_to(pi.m_AdjustedRating));
		else
			Rating::Inc(pi.m_AdjustedRating.m_Increment, rInc);
	}

	for (ActiveList::iterator it = m_Active.begin(); m_Active.end() != it; it++)
	{
		PeerInfo& pi = it->get_ParentObj();
		assert(pi.m_Active.m_Now);
		assert(pi.m_RawRating.m_Value); // must not be banned (i.e. must be re-inserted into m_AdjustedRatings).

		uint32_t& val = pi.m_AdjustedRating.m_Increment;
		val = (val > rDec) ? (val - rDec) : 0;
		m_AdjustedRatings.insert(pi.m_AdjustedRating);
	}
}

PeerManager::PeerInfo* PeerManager::Find(const PeerID& id, bool& bCreate)
{
	PeerInfo::ID pid;
	pid.m_Key = id;

	PeerIDSet::iterator it = m_IDs.find(pid);
	if (m_IDs.end() != it)
	{
		bCreate = false;
		return &it->get_ParentObj();
	}

	if (!bCreate)
		return NULL;

	PeerInfo* ret = AllocPeer();

	ret->m_ID.m_Key = id;
	if (!(id == ECC::Zero))
		m_IDs.insert(ret->m_ID);

	ret->m_RawRating.m_Value = Rating::Initial;
	m_Ratings.insert(ret->m_RawRating);

	ret->m_AdjustedRating.m_Increment = 0;
	m_AdjustedRatings.insert(ret->m_AdjustedRating);

	ret->m_Active.m_Now = false;
	ret->m_LastSeen = 0;
	ret->m_LastActivity_ms = 0;

	LOG_INFO() << *ret << " New";

	return ret;
}

void PeerManager::OnSeen(PeerInfo& pi)
{
	pi.m_LastSeen = getTimestamp();
}

void PeerManager::ModifyRating(PeerInfo& pi, uint32_t delta, bool bAdd)
{
	ModifyRatingInternal(pi, delta, bAdd, false);
}

void PeerManager::Ban(PeerInfo& pi)
{
	ModifyRatingInternal(pi, 0, false, true);
}

void PeerManager::ModifyRatingInternal(PeerInfo& pi, uint32_t delta, bool bAdd, bool ban)
{
	uint32_t r0 = pi.m_RawRating.m_Value;

	m_Ratings.erase(RawRatingSet::s_iterator_to(pi.m_RawRating));
	if (pi.m_RawRating.m_Value)
		m_AdjustedRatings.erase(AdjustedRatingSet::s_iterator_to(pi.m_AdjustedRating));

	if (ban)
	{
		pi.m_RawRating.m_Value = 0;
		pi.m_AdjustedRating.m_Increment = 0;
	}
	else
	{
		if (bAdd)
			Rating::Inc(pi.m_RawRating.m_Value, delta);
		else
			Rating::Dec(pi.m_RawRating.m_Value, delta);

		assert(pi.m_RawRating.m_Value);
		m_AdjustedRatings.insert(pi.m_AdjustedRating);
	}

	m_Ratings.insert(pi.m_RawRating);

	LOG_INFO() << pi << " Rating " << r0 << " -> " << pi.m_RawRating.m_Value;
}

void PeerManager::RemoveAddr(PeerInfo& pi)
{
	if (!pi.m_Addr.m_Value.empty())
	{
		m_Addr.erase(AddrSet::s_iterator_to(pi.m_Addr));
		pi.m_Addr.m_Value = io::Address();
		assert(pi.m_Addr.m_Value.empty());
	}
}

void PeerManager::ModifyAddr(PeerInfo& pi, const io::Address& addr)
{
	if (addr == pi.m_Addr.m_Value)
		return;

	LOG_INFO() << pi << " Address changed to " << addr;

	RemoveAddr(pi);

	if (addr.empty())
		return;

	PeerInfo::Addr pia;
	pia.m_Value = addr;

	AddrSet::iterator it = m_Addr.find(pia);
	if (m_Addr.end() != it)
		RemoveAddr(it->get_ParentObj());

	pi.m_Addr.m_Value = addr;
	m_Addr.insert(pi.m_Addr);
	assert(!pi.m_Addr.m_Value.empty());
}

void PeerManager::OnActive(PeerInfo& pi, bool bActive)
{
	if (pi.m_Active.m_Now != bActive)
	{
		pi.m_Active.m_Now = bActive;
		pi.m_LastActivity_ms = GetTimeNnz_ms();

		if (bActive)
			m_Active.push_back(pi.m_Active);
		else
			m_Active.erase(ActiveList::s_iterator_to(pi.m_Active));
	}
}

void PeerManager::OnRemoteError(PeerInfo& pi, bool bShouldBan)
{
	if (bShouldBan)
		Ban(pi);
	else
	{
		uint32_t dt_ms = GetTimeNnz_ms() - pi.m_LastActivity_ms;
		if (dt_ms < m_Cfg.m_TimeoutDisconnect_ms)
			ModifyRating(pi, Rating::PenaltyNetworkErr, false);
	}
}

PeerManager::PeerInfo* PeerManager::OnPeer(const PeerID& id, const io::Address& addr, bool bAddrVerified)
{
	if (id == ECC::Zero)
	{
		if (!bAddrVerified)
			return NULL;

		// find by addr
		PeerInfo::Addr pia;
		pia.m_Value = addr;

		AddrSet::iterator it = m_Addr.find(pia);
		if (m_Addr.end() != it)
			return &it->get_ParentObj();
	}

	bool bCreate = true;
	PeerInfo* pRet = Find(id, bCreate);

	if (bAddrVerified || !pRet->m_Addr.m_Value.empty() || (getTimestamp() - pRet->m_LastSeen > m_Cfg.m_TimeoutAddrChange_s))
		ModifyAddr(*pRet, addr);

	return pRet;
}

void PeerManager::Delete(PeerInfo& pi)
{
	OnActive(pi, false);
	RemoveAddr(pi);
	m_Ratings.erase(RawRatingSet::s_iterator_to(pi.m_RawRating));

	if (pi.m_RawRating.m_Value)
		m_AdjustedRatings.erase(AdjustedRatingSet::s_iterator_to(pi.m_AdjustedRating));

	if (!(pi.m_ID.m_Key == ECC::Zero))
		m_IDs.erase(PeerIDSet::s_iterator_to(pi.m_ID));

	DeletePeer(pi);
}

void PeerManager::Clear()
{
	while (!m_Ratings.empty())
		Delete(m_Ratings.begin()->get_ParentObj());

	assert(m_AdjustedRatings.empty() && m_Active.empty());
}

std::ostream& operator << (std::ostream& s, const PeerManager::PeerInfo& pi)
{
	s << "PI " << pi.m_ID.m_Key << "--" << pi.m_Addr.m_Value;
	return s;
}

} // namespace proto
} // namespace beam
