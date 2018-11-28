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

#include "utility/serialize.h"
#include "core/serialization_adapters.h"
#include "core/ecc_native.h"
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
	m_Mode = Mode::Plaintext;
	m_MyNonce = Zero;
	m_RemoteNonce = Zero;
}

void ProtocolPlus::Decrypt(uint8_t* p, uint32_t nSize)
{
	if (Mode::Duplex == m_Mode)
		m_CipherIn.XCrypt(m_Enc, p, nSize);
}

uint32_t ProtocolPlus::get_MacSize()
{
	return (Mode::Duplex == m_Mode) ? sizeof(uint64_t) : 0;
}

bool ProtocolPlus::VerifyMsg(const uint8_t* p, uint32_t nSize)
{
	if (Mode::Duplex != m_Mode)
		return true;

	MacValue hmac;

	if (nSize < hmac.nBytes)
		return false; // could happen on (sort of) overflow attack?

	ECC::Hash::Mac hm = m_HMac;
	hm.Write(p, nSize - hmac.nBytes);

	get_HMac(hm, hmac);

	return !memcmp(p + nSize - hmac.nBytes, hmac.m_pData, hmac.nBytes);
}

void ProtocolPlus::get_HMac(ECC::Hash::Mac& hm, MacValue& res)
{
	ECC::Hash::Value hv;
	hm >> hv;

	//static_assert(hv.nBytes >= res.nBytes, "");
	res = hv;
}

void ProtocolPlus::Encrypt(SerializedMsg& sm, MsgSerializer& ser)
{
	MacValue hmac;

	if (Mode::Plaintext != m_Mode)
	{
		// 1. append dummy of the needed size
		hmac = Zero;
		ser & hmac;
	}

	ser.finalize(sm);

	if (Mode::Plaintext != m_Mode)
	{
		// 2. get size
		size_t n = 0;

		for (size_t i = 0; i < sm.size(); i++)
			n += sm[i].size;

		// 3. Calculate
		ECC::Hash::Mac hm = m_HMac;
		size_t n2 = n - MacValue::nBytes;

		for (size_t i = 0; ; i++)
		{
			assert(i < sm.size());
			io::IOVec& iov = sm[i];
			if (iov.size >= n2)
			{
				hm.Write(iov.data, (uint32_t) n2);
				break;
			}

			hm.Write(iov.data, (uint32_t)iov.size);
			n2 -= iov.size;
		}

		get_HMac(hm, hmac);

		// 4. Overwrite the hmac, encrypt
		n2 = n;

		for (size_t i = 0; i < sm.size(); i++)
		{
			io::IOVec& iov = sm[i];
			uint8_t* dst = (uint8_t*) iov.data;

			if (n2 <= hmac.nBytes)
				memcpy(dst, hmac.m_pData + hmac.nBytes - n2, iov.size);
			else
			{
				size_t offs = n2 - hmac.nBytes;
				if (offs < iov.size)
					memcpy(dst + offs, hmac.m_pData, iov.size - offs);
			}

			n2 -= iov.size;

			m_CipherOut.XCrypt(m_Enc, dst, (uint32_t) iov.size);
		}
	}
}

void InitCipherIV(AES::StreamCipher& c, const ECC::Hash::Value& hvSecret, const ECC::Hash::Value& hvParam)
{
	ECC::NoLeak<ECC::Hash::Value> hvIV;
	ECC::Hash::Processor() << hvSecret << hvParam >> hvIV.V;

	//static_assert(hvIV.V.nBytes >= c.m_Counter.nBytes, "");
	c.m_Counter = hvIV.V;

	c.m_nBuf = 0;
}

bool ImportPeerID(ECC::Point::Native& res, const PeerID& pid)
{
	ECC::Point pt;
	pt.m_X = pid;
	pt.m_Y = 0;

	return res.ImportNnz(pt);
}

bool InitViaDiffieHellman(const ECC::Scalar::Native& myPrivate, const PeerID& remotePublic, AES::Encoder& enc, ECC::Hash::Mac& hmac, AES::StreamCipher* pCipherOut, AES::StreamCipher* pCipherIn)
{
	// Diffie-Hellman
	ECC::Point::Native p;
	ImportPeerID(p, remotePublic);

	ECC::Point::Native ptSecret = p * myPrivate;

	ECC::NoLeak<ECC::Hash::Value> hvSecret;
	ECC::Hash::Processor() << ptSecret >> hvSecret.V;

	static_assert(AES::s_KeyBytes == ECC::Hash::Value::nBytes, "");
	enc.Init(hvSecret.V.m_pData);

	hmac.Reset(hvSecret.V.m_pData, hvSecret.V.nBytes);

	if (pCipherOut)
		InitCipherIV(*pCipherOut, hvSecret.V, remotePublic);

	if (pCipherIn)
	{
		PeerID myPublic;
		Sk2Pk(myPublic, Cast::NotConst(myPrivate)); // my private must have been already normalized. Should not be modified.
		InitCipherIV(*pCipherIn, hvSecret.V, myPublic);
	}

	return true;
}

void ProtocolPlus::InitCipher()
{
	assert(!(m_MyNonce == Zero));

	if (!InitViaDiffieHellman(m_MyNonce, m_RemoteNonce, m_Enc, m_HMac, &m_CipherOut, &m_CipherIn))
		NodeConnection::ThrowUnexpected();
}

void Sk2Pk(PeerID& res, ECC::Scalar::Native& sk)
{
	ECC::Point pt = ECC::Point::Native(ECC::Context::get().G * sk);
	if (pt.m_Y)
		sk = -sk;

	res = pt.m_X;
}

bool BbsEncrypt(ByteBuffer& res, const PeerID& publicAddr, ECC::Scalar::Native& nonce, const void* p, uint32_t n)
{
	PeerID myPublic;
	Sk2Pk(myPublic, nonce);

	AES::Encoder enc;
	AES::StreamCipher cOut;
	ECC::Hash::Mac hmac;
	if (!InitViaDiffieHellman(nonce, publicAddr, enc, hmac, &cOut, NULL))
		return false; // bad address

	hmac.Write(p, n);
	ECC::Hash::Value hvMac;
	hmac >> hvMac;

	res.resize(myPublic.nBytes + hvMac.nBytes + n);
	uint8_t* pDst = &res.at(0);

	memcpy(pDst, myPublic.m_pData, myPublic.nBytes);
	memcpy(pDst + myPublic.nBytes, hvMac.m_pData, hvMac.nBytes);
	memcpy(pDst + myPublic.nBytes + hvMac.nBytes, p, n);

	cOut.XCrypt(enc, pDst + myPublic.nBytes, hvMac.nBytes + n);

	return true;
}

bool BbsDecrypt(uint8_t*& p, uint32_t& n, const ECC::Scalar::Native& privateAddr)
{
	PeerID remotePublic;
	ECC::Hash::Value hvMac, hvMac2;

	if (n < remotePublic.nBytes + hvMac.nBytes)
		return false;

	memcpy(remotePublic.m_pData, p, remotePublic.nBytes);

	AES::Encoder enc;
	AES::StreamCipher cIn;
	ECC::Hash::Mac hmac;
	if (!InitViaDiffieHellman(privateAddr, remotePublic, enc, hmac, NULL, &cIn))
		return false; // bad address

	cIn.XCrypt(enc, p + remotePublic.nBytes, n - remotePublic.nBytes);

	memcpy(hvMac.m_pData, p + remotePublic.nBytes, hvMac.nBytes);

	p += remotePublic.nBytes + hvMac.nBytes;
	n -= (remotePublic.nBytes + hvMac.nBytes);

	hmac.Write(p, n);
	hmac >> hvMac2;

	return (hvMac == hvMac2);
}

union HighestMsgCode
{
#define THE_MACRO(code, msg) uint8_t m_pBuf_##msg[code + 1];
	BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO
};

bool NotCalled_VerifyNoDuplicatedIDs(uint32_t id)
{
	switch (id)
	{
#define THE_MACRO(code, msg) \
	case code:
		BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO
		return true;
	}
	return false;
}

/////////////////////////
// NodeConnection
NodeConnection::NodeConnection()
	:m_Protocol('B', 'm', 8, sizeof(HighestMsgCode), *this, 20000)
	,m_ConnectPending(false)
{
#define THE_MACRO(code, msg) \
	m_Protocol.add_message_handler<NodeConnection, msg##_NoInit, &NodeConnection::OnMsgInternal>(uint8_t(code), this, 0, 1024*1024*10);

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

	m_pAsyncFail = io::AsyncEvent::create(io::Reactor::get_Current(), std::move(cb));
	m_pAsyncFail->get_trigger()();
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
			SecureConnect();
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

	case NodeConnection::DisconnectReason::Bye:
		s << "Bye " << r.m_ByeReason;
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

bool NodeConnection::IsLive() const
{
	return m_Connection && !m_pAsyncFail;
}

#define THE_MACRO(code, msg) \
void NodeConnection::Send(const msg& v) \
{ \
	if (!IsLive()) \
		return; \
	m_SerializeCache.clear(); \
	MsgSerializer& ser = m_Protocol.serializeNoFinalize(m_SerializeCache, uint8_t(code), v); \
	m_Protocol.Encrypt(m_SerializeCache, ser); \
	io::Result res = m_Connection->write_msg(m_SerializeCache); \
	m_SerializeCache.clear(); \
\
	TestIoResultAsync(res); \
} \
\
bool NodeConnection::OnMsgInternal(uint64_t, msg##_NoInit&& v) \
{ \
	try { \
		/* checkpoint */ \
		TestInputMsgContext(code); \
		return OnMsg2(std::move(v)); \
	} catch (const std::exception& e) { \
		OnExc(e); \
		return false; \
	} \
} \

BeamNodeMsgsAll(THE_MACRO)
#undef THE_MACRO

void NodeConnection::TestInputMsgContext(uint8_t code)
{
	if (!IsSecureIn())
	{
		// currently we demand all the trafic encrypted. The only messages that can be sent over non-secure network is those used to establish it
		switch (code)
		{
		case SChannelInitiate::s_Code:
		case SChannelReady::s_Code:
			break;

		default:
			ThrowUnexpected("non-secure comm");
		}
	}
}

void NodeConnection::GenerateSChannelNonce(ECC::Scalar::Native& sk)
{
	sk.GenRandomNnz();
}

void NodeConnection::SecureConnect()
{
	if (!(m_Protocol.m_MyNonce == Zero))
		return; // already sent

	GenerateSChannelNonce(m_Protocol.m_MyNonce);

	if (m_Protocol.m_MyNonce == Zero)
		ThrowUnexpected("SChannel not supported");

	SChannelInitiate msg;
	Sk2Pk(msg.m_NoncePub, m_Protocol.m_MyNonce);
	Send(msg);
}

void NodeConnection::OnMsg(SChannelInitiate&& msg)
{
	if ((ProtocolPlus::Mode::Plaintext != m_Protocol.m_Mode) || (msg.m_NoncePub == Zero))
		ThrowUnexpected();

	SecureConnect(); // unless already sent

	SChannelReady msgOut(Zero);
	Send(msgOut); // activating new cipher.

	m_Protocol.m_RemoteNonce = msg.m_NoncePub;
	m_Protocol.InitCipher();

	m_Protocol.m_Mode = ProtocolPlus::Mode::Outgoing;

	OnConnectedSecure();
}

void NodeConnection::OnMsg(SChannelReady&& msg)
{
	if (ProtocolPlus::Mode::Outgoing != m_Protocol.m_Mode)
		ThrowUnexpected();

	m_Protocol.m_Mode = ProtocolPlus::Mode::Duplex;
}

void NodeConnection::ProveID(ECC::Scalar::Native& sk, uint8_t nIDType)
{
	assert(IsSecureOut());

	// confirm our ID
	ECC::Hash::Processor hp;
	HashAddNonce(hp, true);
	ECC::Hash::Value hv;
	hp >> hv;

	Authentication msgOut;
	msgOut.m_IDType = nIDType;
	Sk2Pk(msgOut.m_ID, sk);
	msgOut.m_Sig.Sign(hv, sk);

	Send(msgOut);
}

void NodeConnection::HashAddNonce(ECC::Hash::Processor& hp, bool bRemote)
{
	if (bRemote)
		hp << m_Protocol.m_RemoteNonce;
	else
	{
		ECC::Hash::Value hv;
		Sk2Pk(hv, m_Protocol.m_MyNonce);
		hp << hv;
	}
}

void NodeConnection::ProveKdfObscured(Key::IKdf& kdf, uint8_t nIDType)
{
	ECC::Hash::Processor hp;
	hp << (uint32_t) Key::Type::Identity;
	HashAddNonce(hp, true);
	HashAddNonce(hp, false);

	ECC::Hash::Value hv;
	hp >> hv;

	ECC::Scalar::Native sk;
	kdf.DeriveKey(sk, hv);

	ProveID(sk, nIDType);

}

void NodeConnection::ProvePKdfObscured(Key::IPKdf& kdf, uint8_t nIDType)
{
	struct MyKdf
		:public Key::IKdf
	{
		Key::IPKdf& m_Kdf;
		MyKdf(Key::IPKdf& kdf) :m_Kdf(kdf) {}

		virtual void DerivePKey(ECC::Point::Native&, const ECC::Hash::Value&) override
		{
			assert(false);
		}

		virtual void DerivePKey(ECC::Scalar::Native&, const ECC::Hash::Value&) override
		{
			assert(false);
		}

		virtual void DeriveKey(ECC::Scalar::Native& out, const ECC::Hash::Value& hv) override
		{
			m_Kdf.DerivePKey(out, hv);
		}
	};

	MyKdf myKdf(kdf);
	ProveKdfObscured(myKdf, nIDType);
}

bool NodeConnection::IsKdfObscured(Key::IPKdf& kdf, const PeerID& id)
{
	ECC::Hash::Processor hp;
	hp << (uint32_t)Key::Type::Identity;
	HashAddNonce(hp, false);
	HashAddNonce(hp, true);

	ECC::Hash::Value hv;
	hp >> hv;

	ECC::Point::Native pt;
	kdf.DerivePKey(pt, hv);

	return id == ECC::Point(pt).m_X;
}

bool NodeConnection::IsPKdfObscured(Key::IPKdf& kdf, const PeerID& id)
{
	struct MyPKdf
		:public Key::IPKdf
	{
		Key::IPKdf& m_Kdf;
		MyPKdf(Key::IPKdf& kdf) :m_Kdf(kdf) {}

		virtual void DerivePKey(ECC::Point::Native& out, const ECC::Hash::Value& hv) override
		{
			ECC::Scalar::Native s;
			m_Kdf.DerivePKey(s, hv);
			out = ECC::Context::get().G * s;
		}

		virtual void DerivePKey(ECC::Scalar::Native&, const ECC::Hash::Value&) override
		{
			assert(false);
		}
	};

	MyPKdf myKdf(kdf);
	return IsKdfObscured(myKdf, id);
}

bool NodeConnection::IsSecureIn() const
{
	return ProtocolPlus::Mode::Duplex == m_Protocol.m_Mode;
}

bool NodeConnection::IsSecureOut() const
{
	return ProtocolPlus::Mode::Plaintext != m_Protocol.m_Mode;
}

void NodeConnection::OnMsg(Authentication&& msg)
{
	if (!IsSecureIn())
		ThrowUnexpected();

	// verify ID
	ECC::Hash::Processor hp;
	HashAddNonce(hp, false);

	ECC::Hash::Value hv;
	hp >> hv;

	ECC::Point pt;
	pt.m_X = msg.m_ID;
	pt.m_Y = 0;

	ECC::Point::Native p;
	if (!p.Import(pt))
		ThrowUnexpected();

	if (!msg.m_Sig.IsValid(hv, p))
		ThrowUnexpected();
}

void NodeConnection::OnMsg(Bye&& msg)
{
	DisconnectReason r;
	r.m_Type = DisconnectReason::Bye;
	r.m_ByeReason = msg.m_Reason;
	OnDisconnect(r);
}

/////////////////////////
// NodeConnection::Server
void NodeConnection::Server::Listen(const io::Address& addr)
{
	m_pServer = io::TcpServer::create(io::Reactor::get_Current(), addr, BIND_THIS_MEMFN(OnAccepted));
}

} // namespace proto
} // namespace beam
