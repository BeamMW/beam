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
#include "../utility/logger.h"

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

bool InitViaDiffieHellman(const ECC::Scalar::Native& myPrivate, const PeerID& remotePublic, AES::Encoder& enc, ECC::Hash::Mac& hmac, AES::StreamCipher* pCipherOut, AES::StreamCipher* pCipherIn)
{
    // Diffie-Hellman
    ECC::Point::Native p;
    if (!remotePublic.ExportNnz(p))
        return false;

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
        myPublic.FromSk(Cast::NotConst(myPrivate)); // my private must have been already normalized. Should not be modified.
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


bool Bbs::Encrypt(ByteBuffer& res, const PeerID& publicAddr, ECC::Scalar::Native& nonce, const void* p, uint32_t n)
{
    PeerID myPublic;
    myPublic.FromSk(nonce);

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

bool Bbs::Decrypt(uint8_t*& p, uint32_t& n, const ECC::Scalar::Native& privateAddr)
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

void Bbs::get_HashPartial(ECC::Hash::Processor& hp, const BbsMsg& msg)
{
	hp
		<< "bbs.msg"
		<< msg.m_Channel
		<< Blob(msg.m_Message);
}

void Bbs::get_Hash(ECC::Hash::Value& hv, const BbsMsg& msg)
{
	ECC::Hash::Processor hp;
	get_HashPartial(hp, msg);

	hp
		<< msg.m_TimePosted
		<< msg.m_Nonce
		>> hv;
}

bool Bbs::IsHashValid(const ECC::Hash::Value& hv)
{
	uint32_t nHigh;
	hv.ExportWord<0>(nHigh);

	return nHigh < (1 << 10); // upper 22 bits should be zero, probability ~ 1 / 4mln
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
    :m_Protocol('B', 'm', 10, sizeof(HighestMsgCode), *this, 20000)
    ,m_ConnectPending(false)
	,m_RulesCfgSent(false)
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

	m_RulesCfgSent = false;
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

void NodeConnection::TestNotDrown()
{
	if (!m_pAsyncFail && m_UnsentHiMark && (get_Unsent() > m_UnsentHiMark))
	{
		io::AsyncEvent::Callback cb = [this]()
		{
			DisconnectReason r;
			r.m_Type = DisconnectReason::Drown;
			OnDisconnect(r);
		};

		m_pAsyncFail = io::AsyncEvent::create(io::Reactor::get_Current(), std::move(cb));
		m_pAsyncFail->get_trigger()();
	}
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
        catch (const NodeProcessingException& e) {
            OnProcessingExc(e);
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
    r.m_ExceptionDetails.m_ExceptionType = NodeProcessingException::Type::Base;
    r.m_ExceptionDetails.m_szErrorMsg = e.what();
    OnDisconnect(r);
}

void NodeConnection::OnProcessingExc(const NodeProcessingException& exception)
{
    DisconnectReason r;
    r.m_Type = DisconnectReason::ProcessingExc;
    r.m_ExceptionDetails.m_ExceptionType = exception.type();
    r.m_ExceptionDetails.m_szErrorMsg = exception.what();
    OnDisconnect(r);
}

void NodeConnection::OnIoErr(io::ErrorCode err)
{
    DisconnectReason r;
    r.m_Type = DisconnectReason::Io;
    r.m_IoError = err;
    OnDisconnect(r);
}

size_t NodeConnection::get_Unsent() const
{
	return m_Connection ? m_Connection->get_Unsent() : 0;
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
        s << "Protocol " << static_cast<uint32_t>(r.m_eProtoCode);
        break;

    case NodeConnection::DisconnectReason::ProcessingExc:
        s << r.m_ExceptionDetails.m_szErrorMsg;
        break;

    case NodeConnection::DisconnectReason::Bye:
        s << "Bye " << r.m_ByeReason;
        break;

	case NodeConnection::DisconnectReason::Drown:
		s << "Drown";
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

void NodeConnection::ThrowUnexpected(const char* sz, NodeProcessingException::Type type)
{
    throw NodeProcessingException(sz ? sz : "proto violation", type);
}

void NodeConnection::Connect(const io::Address& addr, const boost::optional<io::Address> proxyAddr)
{
    assert(!m_Connection && !m_ConnectPending);

    io::Result res;
    if (proxyAddr)
    {
        res = io::Reactor::get_Current().tcp_connect_with_proxy(
            addr,
            proxyAddr.value(),
            uint64_t(this),
            OnConnectInternal);
    }
    else
    {
        res = io::Reactor::get_Current().tcp_connect(
            addr,
            uint64_t(this),
            OnConnectInternal);
    }

    TestIoResultAsync(res);
    m_ConnectPending = true;
}

void NodeConnection::Accept(io::TcpStream::Ptr&& newStream)
{
    assert(!m_Connection && !m_ConnectPending);

    newStream->enable_keepalive(Rules::get().DA.Target_s); // it should be comparable to the block rate

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
    TestNotDrown(); \
} \
\
bool NodeConnection::OnMsgInternal(uint64_t, msg##_NoInit&& v) \
{ \
    try { \
        /* checkpoint */ \
        TestInputMsgContext(code); \
        return OnMsg2(std::move(v)); \
    } catch (const NodeProcessingException& e) { \
        OnProcessingExc(e); \
        return false; \
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
    msg.m_NoncePub.FromSk(m_Protocol.m_MyNonce);
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

	Send(proto::GetTime(Zero)); // in the next proto - better to send the time right away, instead of asking for it

	OnConnectedSecure();
}

void LoginFlags::Extension::set(uint32_t& nFlags, uint32_t nExt)
{
    assert(!(nFlags & Msk));

    if (nExt < nBitsLegacy)
        nExt = (1 << nExt) - 1;
    else
        nExt = ((nExt - nBitsLegacy + 1) << nBitsLegacy) - 1;

    nFlags |= nExt << nShift;
}

uint32_t LoginFlags::Extension::get(uint32_t nFlags)
{
    uint32_t val = (Msk & nFlags) >> nShift;
    
    const uint32_t nLegacyVal = (1 << nBitsLegacy) - 1;
    if (nLegacyVal == (val & nLegacyVal))
        return ((val - nLegacyVal) >> nBitsLegacy) + nBitsLegacy;

    // find 1st zero bit
    uint32_t iBit = 0;
    for (; iBit < nBitsLegacy - 1; iBit++)
        if (!(1 & (val >> iBit)))
            break;

    return iBit;
}

void NodeConnection::SendLogin()
{
	Login msg;
    LoginFlags::Extension::set(msg.m_Flags, LoginFlags::Extension::Maximum);
	SetupLogin(msg);

	const Rules& r = Rules::get();

	if (!m_RulesCfgSent)
	{
		m_RulesCfgSent = true;

		Height hMin = get_MinPeerFork();
		size_t iFork = _countof(r.pForks);
		while (iFork)
		{
			if (r.pForks[--iFork].m_Height <= hMin)
				break;
		}

		msg.m_Cfgs.reserve(_countof(r.pForks) - iFork);

		for (; iFork < _countof(r.pForks); iFork++)
		{
			const HeightHash& x = r.pForks[iFork];
			if (MaxHeight == x.m_Height)
				break;
			msg.m_Cfgs.push_back(r.pForks[iFork].m_Hash);
		}
	}

	Send(msg);
}

void NodeConnection::SetupLogin(Login&)
{
}

Height NodeConnection::get_MinPeerFork()
{
	return Rules::HeightGenesis - 1;
}

void NodeConnection::OnLogin(Login&&)
{
}

void NodeConnection::OnMsg(Login&& msg)
{
	if (msg.m_Cfgs.empty()) // Peers send cfgs only on 1st login
	{
		OnLoginInternal(std::move(msg));
		return;
	}

	const Rules& r = Rules::get();
	for (size_t i = msg.m_Cfgs.size(); ; )
	{
		if (!i--)
			break; // no compatible cfg found

		const HeightHash* pFork = r.FindFork(msg.m_Cfgs[i]);
		if (pFork)
		{
			if (&r.get_LastFork() != pFork)
			{
                size_t nMyFork = pFork - r.pForks;

                Height hScheme = pFork[1].m_Height;

                LOG_WARNING() << "Peer " << m_Connection->peer_address() << " incompatible with HF " << (nMyFork + 1) << ", Height=" << hScheme;

                Height hMinScheme = get_MinPeerFork();
                if (hMinScheme >= hScheme)
                    ThrowUnexpected("Legacy", NodeProcessingException::Type::Incompatible);
			}

            if (i + 1 != msg.m_Cfgs.size())
            {
                LOG_WARNING() << "Peer " << m_Connection->peer_address() << " has unknown fork: " << msg.m_Cfgs[i + 1];
            }

			OnLoginInternal(std::move(msg));
			return;
		}
	}

	std::ostringstream os;
	os << "Incompatible peer cfgs: ";

	for (size_t i = 0; ; )
	{
		os << msg.m_Cfgs[i];
		if (++i == msg.m_Cfgs.size())
			break;

		os << ", ";
	}

	ThrowUnexpected(os.str().c_str(), NodeProcessingException::Type::Incompatible);
}

void NodeConnection::OnLoginInternal(Login&& msg)
{
    uint32_t nExt = LoginFlags::Extension::get(msg.m_Flags);
    if (LoginFlags::Extension::Maximum != nExt)
    {
        bool bNewer = (nExt > LoginFlags::Extension::Maximum);
        LOG_WARNING() << "Peer " << m_Connection->peer_address() << " uses " << (bNewer ? "newer" : "older") << " ext: " << nExt;

        if (nExt < LoginFlags::Extension::Minimum)
            ThrowUnexpected("Legacy", NodeProcessingException::Type::Incompatible);
    }

	OnLogin(std::move(msg));
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
    msgOut.m_ID.FromSk(sk);
    msgOut.m_Sig.Sign(hv, sk);

    Send(msgOut);
}

void NodeConnection::HashAddNonce(ECC::Hash::Processor& hp, bool bRemote)
{
    if (bRemote)
        hp << m_Protocol.m_RemoteNonce;
    else
    {
        PeerID pid;
        pid.FromSk(m_Protocol.m_MyNonce);
        hp << pid;
    }
}

void NodeConnection::ProveKdfObscured(Key::IKdf& kdf, uint8_t nIDType)
{
    ECC::Hash::Processor hp;
    hp << (uint32_t) Key::Type::ProtoID;
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
    hp << (uint32_t) Key::Type::ProtoID;
    HashAddNonce(hp, false);
    HashAddNonce(hp, true);

    ECC::Hash::Value hv;
    hp >> hv;

    ECC::Point::Native pt;
    kdf.DerivePKeyG(pt, hv);

    return id == ECC::Point(pt).m_X;
}

bool NodeConnection::IsPKdfObscured(Key::IPKdf& kdf, const PeerID& id)
{
    struct MyPKdf
        :public Key::IPKdf
    {
        Key::IPKdf& m_Kdf;
        MyPKdf(Key::IPKdf& kdf) :m_Kdf(kdf) {}

        virtual void DerivePKeyG(ECC::Point::Native& out, const ECC::Hash::Value& hv) override
        {
            ECC::Scalar::Native s;
            m_Kdf.DerivePKey(s, hv);
            out = ECC::Context::get().G * s;
        }

        virtual void DerivePKeyJ(ECC::Point::Native& out, const ECC::Hash::Value& hv) override
        {
            assert(false);
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

    ECC::Point::Native p;
    if (!msg.m_ID.ExportNnz(p))
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

void NodeConnection::OnMsg(Ping&& msg)
{
	Send(Pong(Zero));
}

void NodeConnection::OnMsg(GetTime&& msg)
{
	proto::Time msgOut;
	msgOut.m_Value = getTimestamp();
	Send(msgOut);
}

void NodeConnection::OnMsg(Time&& msg)
{
	uint32_t dtMax_s = Rules::get().DA.MaxAhead_s * 3 / 4; // time diff should be no more than 3/4 of the max allowed time diff in blocks
	Timestamp ts = getTimestamp();

	if ((ts + dtMax_s < msg.m_Value) ||
		(ts > msg.m_Value + dtMax_s))
	{
		std::ostringstream os;
		os << "Time diff too large. Local=" << ts << ", Remote=" << msg.m_Value;

		ThrowUnexpected(os.str().c_str(), NodeProcessingException::Type::TimeOutOfSync);
	}
}

void NodeConnection::OnMsg(ShieldedList0&& msg)
{
    proto::ShieldedList msg2;
    msg2.m_Items = std::move(msg.m_Items);
    Cast::Down<INodeMsgHandler>(*this).OnMsg(std::move(msg2));
}

/////////////////////////
// NodeConnection::Server
void NodeConnection::Server::Listen(const io::Address& addr)
{
    m_pServer = io::TcpServer::create(io::Reactor::get_Current(), addr, BIND_THIS_MEMFN(OnAccepted));
}

/////////////////////////
// Event
void Event::IParserBase::ProceedOnce(Deserializer& der)
{
    Type::Enum eType = Type::Load(der);

    switch (eType)
    {
#define THE_MACRO(id, name) \
    case Type::name: \
        { \
            name evt; \
            der & evt; \
            OnEventType(evt); \
        } \
        break;

        BeamEventsAll(THE_MACRO)
#undef THE_MACRO

    default:
        throw std::runtime_error("Unk proto/Event");
    }
}

Event::Type::Enum Event::Type::Load(Deserializer& der)
{
    Type::Enum eType;
    auto bytesLeft = der.bytes_left();
    try
    {
        der& eType;
    }
    catch (const std::exception&)
    {
        // workaround for the case whe enum was serialized as unsigned integer #1622
        // TODO: remove after fork
        while (der.bytes_left() < bytesLeft)
            der.ungetch();

        enum Enum2 : int32_t {
#define THE_MACRO(id, name) name = id,
            BeamEventsAll(THE_MACRO)
#undef THE_MACRO
        };
        Enum2 eType2;
        der& eType2;
        eType = static_cast<Type::Enum>(eType2);
    }
    return eType;
}

void Event::IParser::OnEventType(Utxo0& evt0)
{
    Utxo evt;

#define THE_MACRO(type, name) evt.m_##name = std::move(evt0.m_##name);
    BeamEvent_Utxo0(THE_MACRO)
#undef THE_MACRO

    ZeroObject(evt.m_User);
    Cast::Down<IParserBase>(*this).OnEventType(evt);
}

void Event::IParserBase::ProceedOnce(const Blob& blob)
{
    Deserializer der;
    der.reset(blob.p, blob.n);
    ProceedOnce(der);
}

uint32_t Event::IGroupParser::Proceed(const Blob& blob)
{
    uint32_t nCount = 0;
    Deserializer der;

    for (der.reset(blob.p, blob.n); der.bytes_left(); nCount++)
    {
        der & m_Height;
        ProceedOnce(der);
    }

    return nCount;
}

void Event::Utxo::Dump(std::ostringstream& os) const
{
    char ch = (Flags::Add & m_Flags) ? '+' : '-';
    os << ch << "Utxo " << m_Cid << ", Maturity=" << m_Maturity;
}

void Event::Utxo0::Dump(std::ostringstream& os) const
{
    char ch = (Flags::Add & m_Flags) ? '+' : '-';
    os << ch << "Utxo " << m_Cid << ", Maturity=" << m_Maturity;
}

void Event::Shielded::Dump(std::ostringstream& os) const
{
    char ch = (Flags::Add & m_Flags) ? '+' : '-';
    os << ch << "Shielded Value=" << m_CoinID.m_Value << ", TxoID=" << m_TxoID << ", Sender=" << m_CoinID.m_User.m_Sender;
}

void Event::AssetCtl::Dump(std::ostringstream& os) const
{
    if (Flags::Add & m_Flags)
        os << '+';
    if (Flags::Delete & m_Flags)
        os << '-';

    os << "Asset MetaHash=" << m_Info.m_Metadata.m_Hash << ", ID=" << m_Info.m_ID << ", Value=" << AmountBig::get_Lo(m_Info.m_Value) << ", LockHeight=" << m_Info.m_LockHeight;

    if (m_EmissionChange)
        os << ", Emit " << m_EmissionChange;
}

} // namespace proto
} // namespace beam
