// Copyright 2019 The Beam Team
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

#include "utility/io/reactor.h"
#include "utility/io/tcpstream.h"
#include "utility/io/tcpserver.h"
#include "utility/io/proxy_connector.h"
#include "utility/io/timer.h"
#include "utility/config.h"
#include "utility/logger.h"
#include "utility/helpers.h"
#include <iostream>
#include <iomanip>
#include <assert.h>
#include <vector>


using namespace beam;
using namespace beam::io;
using namespace std;

// RFC 1928	SOCKS Protocol Version 5

namespace {

constexpr auto testDomain = "123.45.67.89";
constexpr uint16_t testPort = 80;
constexpr auto dummyProxyServerIp = "localhost";
constexpr uint16_t dummyProxyServerPort = 1080;

class DummyProxyServerConnection {
public:
	DummyProxyServerConnection() :
		m_state(ConnState::Initial) {};

	bool isInitialized() { 
		if (m_streamPtr) return true;
		else return false;
	};

	void setStream(TcpStream::Ptr&& newStream) {
		m_streamPtr = std::move(newStream);
		m_streamPtr->enable_read([this](ErrorCode errorCode, void* data, size_t size) {
			return onInputData(errorCode, data, size);
			});
	};

	bool onInputData(ErrorCode errorCode, void* data, size_t size) {
		if (errorCode != EC_OK) {
			BEAM_LOG_DEBUG() << "m_state: " <<
							static_cast<uint8_t>(m_state) <<
							";  errorCode: " << errorCode;
			assert(false);
		}
		assert(data && size);
		uint8_t* buf = static_cast<uint8_t*>(data);
		switch(m_state) {
			case ConnState::Initial:
				onInitialState(buf, size);
				break;
			case ConnState::AuthChosen:
				onAuthState(buf, size);
				break;
			case ConnState::DestRequested:
				onRequestState(buf, size);
				break;
			default:
				return false;
		}
		return true;
	};

private:
	enum class ConnState : uint8_t {
		Initial,
		AuthChosen,
		DestRequested
	};

	void onInitialState(uint8_t* data, size_t size) {
		std::array<uint8_t,3> referenceAuthReq = {0x05, 0x01, 0x00};
		if (!std::equal(referenceAuthReq.cbegin(), referenceAuthReq.cend(), data))
			assert(false);

		std::array<uint8_t,2> referenceAuthReply = {0x05, 0x00};
		m_streamPtr->write(referenceAuthReply.data(), referenceAuthReply.size());
		m_state = ConnState::AuthChosen;
	};

	void onAuthState(uint8_t* data, size_t size) {
		// 123.45.67.89 = 0x7B2D4359
		// 80 = 0x50
		std::array<uint8_t,10> referenceAuthReq = {0x05, 0x01, 0x00, 0x01, 0x7B, 0x2D, 0x43, 0x59, 0x00, 0x50};
		if (!std::equal(referenceAuthReq.cbegin(), referenceAuthReq.cend(), data))
			assert(false);

		// here we can really connect to requested destination...
		// and implement real proxy bridge

		std::array<uint8_t,10> referenceAuthReply = {0x05, 0x00, 0x00, 0x01, 0x12, 0x34, 0x56, 0x78, 0x98, 0x76};
		m_streamPtr->write(referenceAuthReply.data(), referenceAuthReply.size());
		m_state = ConnState::DestRequested;
	};

	void onRequestState(uint8_t* data, size_t size) {
		string req = "Hello, hello!";
		assert(std::equal(req.cbegin(), req.cend(), data));
		auto dummyDestinationServerRepply = "Aloha!";
		m_streamPtr->write(dummyDestinationServerRepply, strlen(dummyDestinationServerRepply));
	};

	TcpStream::Ptr m_streamPtr;
	ConnState m_state;
};

/**
 * @createOwnServer use this to create own dummy proxy server or connect to any other real proxy
 */
void proxy_test() {
	Reactor::Ptr reactor = Reactor::create();

	Address destAddr, proxyAddr;
	destAddr.resolve(testDomain);
	destAddr.port(testPort);
	proxyAddr.resolve(dummyProxyServerIp);
	proxyAddr.port(dummyProxyServerPort);

	// indicate testcase execution
	bool connTimeoutCalled = false;
	bool successfulCalled = false;
	bool failedCalled = false;
	bool repplyTimeoutCalled = false;

	// Connection timeout testcase
	{
		int connTO = 2000;
		auto timeStart = std::time(nullptr);
		reactor->tcp_connect_with_proxy(
			destAddr,
			Address(0x7f000001, 12345),	// intentionally corrupted port
			1,
			[&timeStart, &connTO, &connTimeoutCalled](uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
				BEAM_LOG_DEBUG() << "Proxy connection timeout: " << error_str(status) << ". Tag: " << tag;
				assert(tag == 1);
				assert(status != EC_OK);
				if (status == EC_ECONNREFUSED) {} // in some cases
				if (status == EC_ETIMEDOUT) {
					auto timeStop = std::time(nullptr);
					if (timeStop < timeStart+(connTO/1000))
						assert(false);
					if (timeStop > timeStart+(connTO/1000)+1)	// 1-second accuracy
						assert(false);
				}
				connTimeoutCalled = true;
			},
			connTO,
			false);
	}

	// Create proxy server
	DummyProxyServerConnection serverConnection;
	TcpStream::Ptr failStream;
	TcpStream::Ptr timeoutStream;
	TcpServer::Ptr server = TcpServer::create(
		*reactor,
		proxyAddr,
		[&serverConnection, &failStream, &timeoutStream](TcpStream::Ptr&& newStream, int errorCode) {
			// First connection will be handled successfully
			if (!serverConnection.isInitialized()) {
				BEAM_LOG_DEBUG() << "Dummy proxy server accepted connection to handle successful scenario";
				serverConnection.setStream(std::move(newStream));
				return;
			}
			// Second will be intentionally failed
			if (!failStream) {
				BEAM_LOG_DEBUG() << "Dummy proxy server accepted connection to fail";
				failStream = std::move(newStream);
				failStream->enable_read([&failStream](ErrorCode errorCode, void* data, size_t size) -> bool {
					auto failReply = "abra-cadabra";
					failStream->write(failReply, strlen(failReply));
					return true;
				});
				return;
			}
			// Third will be timedout
			if (!timeoutStream) {
				BEAM_LOG_DEBUG() << "Dummy proxy server accepted connection to check timeout";
				timeoutStream = std::move(newStream);
				timeoutStream->enable_read([](ErrorCode errorCode, void* data, size_t size) -> bool {
					// ignoring request
					return true;
				});
				return;
			}
		}
	);
	// Successful connection testcase
	TcpStream::Ptr clientStream;
	reactor->tcp_connect_with_proxy(
		destAddr,
		proxyAddr,
		2,
		[&successfulCalled, &clientStream](uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
			assert(tag == 2);
			if (!newStream) {
				BEAM_LOG_DEBUG() << "Error connection establish: " << error_str(status);
				assert(false);
			}
			BEAM_LOG_DEBUG() << "Proxy connection established. Tag: " << tag;
			clientStream = std::move(newStream);
			clientStream->enable_read([](ErrorCode errorCode, void* data, size_t size){
				if (errorCode != EC_OK) {
					BEAM_LOG_DEBUG() << "Destination server response error: " << error_str(errorCode);
					assert(false);
				}
				assert(data && size);
				string reply = "Aloha!";
				assert(std::equal(reply.cbegin(), reply.cend(), static_cast<uint8_t*>(data)));
				BEAM_LOG_DEBUG() << "Proxy data transfer successful.";
				return true;
			});
			auto req = "Hello, hello!";
			Result res = clientStream->write(req, strlen(req));
			if (!res) assert(false);
			successfulCalled = true;
		},
		1000,
		false);

	// Connection error in socks protocol
	reactor->tcp_connect_with_proxy(
		destAddr,
		proxyAddr,
		3,
		[&failedCalled](uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
			assert(tag == 3);
			assert(status == EC_PROXY_AUTH_ERROR);
			BEAM_LOG_DEBUG() << "Proxy connection error: " << error_str(status) << ". Tag: " << tag;
			failedCalled = true;
		},
		1000,
		false);

	// Proxy reply timeout testcase
	reactor->tcp_connect_with_proxy(
		destAddr,
		proxyAddr,
		4,
		[&repplyTimeoutCalled](uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
			assert(tag == 4);
			assert(status == EC_ETIMEDOUT);
			BEAM_LOG_DEBUG() << "Proxy connection timeout: " << error_str(status) << ". Tag: " << tag;
			repplyTimeoutCalled = true;
		},
		1000,
		false);

	// Run test
	Timer::Ptr timer = Timer::create(*reactor);
	timer->start(
		3000,
		false,
		[&reactor] {
			BEAM_LOG_DEBUG() << "Test watchdog called"; reactor->stop();
			}
		);
	BEAM_LOG_DEBUG() << "reactor start";
	reactor->run();
	BEAM_LOG_DEBUG() << "reactor stop";
	assert(connTimeoutCalled & successfulCalled & failedCalled & repplyTimeoutCalled);
}

// Explicit checks: assert() is compiled out in Release builds
int g_failures = 0;

#define PROXY_CHECK(x) \
	do { \
		if (!(x)) { \
			BEAM_LOG_ERROR() << "CHECK failed: " #x " (line " << __LINE__ << ")"; \
			++g_failures; \
		} \
	} while (false)

std::vector<uint8_t> domain_request(const std::string& host, uint16_t port) {
	std::vector<uint8_t> req = {0x05, 0x01, 0x00, 0x03, static_cast<uint8_t>(host.size())};
	req.insert(req.end(), host.begin(), host.end());
	req.push_back(static_cast<uint8_t>(port >> 8));
	req.push_back(static_cast<uint8_t>(port & 0xFF));
	return req;
}

/**
 * Host names are sent to the proxy (socks5 ATYP=DOMAINNAME) instead of being resolved locally.
 */
void proxy_domain_name_request_test() {
	using Cmd = Socks5_Protocol::Command;

	// encoding
	PROXY_CHECK(Socks5_Protocol::makeRequest(std::string("node.invalid"), 8100, Cmd::CONNECT) == domain_request("node.invalid", 8100));
	{
		std::vector<uint8_t> ipReq = Socks5_Protocol::makeRequest(std::string("123.45.67.89"), 80, Cmd::CONNECT);
		std::vector<uint8_t> reference = {0x05, 0x01, 0x00, 0x01, 0x7B, 0x2D, 0x43, 0x59, 0x00, 0x50};
		PROXY_CHECK(ipReq == reference);
	}
	{
		std::string maxHost(Socks5_Protocol::MaxDomainNameLength, 'a');
		PROXY_CHECK(Socks5_Protocol::makeRequest(maxHost, 1, Cmd::CONNECT) == domain_request(maxHost, 1));
		PROXY_CHECK(Socks5_Protocol::makeRequest(maxHost + "a", 1, Cmd::CONNECT).empty());
		PROXY_CHECK(Socks5_Protocol::makeRequest(std::string(), 1, Cmd::CONNECT).empty());
	}

	// "host:port" parsing, no DNS lookup
	{
		std::string host;
		uint16_t port = 0;
		PROXY_CHECK(parse_host_port("eu-nodes.mainnet.beam.mw:8100", host, port) && host == "eu-nodes.mainnet.beam.mw" && port == 8100);
		PROXY_CHECK(parse_host_port("node.invalid:65535", host, port) && host == "node.invalid" && port == 65535);
		PROXY_CHECK(!parse_host_port("node.invalid", host, port));
		PROXY_CHECK(!parse_host_port("node.invalid:", host, port));
		PROXY_CHECK(!parse_host_port(":8100", host, port));
		PROXY_CHECK(!parse_host_port("node.invalid:0", host, port));
		PROXY_CHECK(!parse_host_port("node.invalid:65536", host, port));
		PROXY_CHECK(!parse_host_port("node.invalid:81a", host, port));
	}

	Reactor::Ptr reactor = Reactor::create();
	const Address proxyAddr = Address::localhost().port(dummyProxyServerPort + 1);

	// invalid host names are rejected before connecting
	auto noCallback = [](uint64_t, unique_ptr<TcpStream>&&, ErrorCode) {};
	PROXY_CHECK(!reactor->tcp_connect_with_proxy(std::string(), 8100, proxyAddr, 10, noCallback, 1000));
	PROXY_CHECK(!reactor->tcp_connect_with_proxy(std::string(256, 'a'), 8100, proxyAddr, 11, noCallback, 1000));
	PROXY_CHECK(!reactor->tcp_connect_with_proxy(std::string("node.invalid"), 0, proxyAddr, 12, noCallback, 1000));

	// mini socks5 server, records the CONNECT requests it receives
	struct Session {
		TcpStream::Ptr stream;
		std::vector<uint8_t> buf;
		bool authDone = false;
	};
	std::vector<std::unique_ptr<Session>> sessions;
	std::vector<std::vector<uint8_t>> requests;

	TcpServer::Ptr server = TcpServer::create(
		*reactor,
		proxyAddr,
		[&sessions, &requests](TcpStream::Ptr&& newStream, ErrorCode errorCode) {
			if (errorCode != EC_OK) return;
			sessions.push_back(std::make_unique<Session>());
			Session* sess = sessions.back().get();
			sess->stream = std::move(newStream);
			sess->stream->enable_read([sess, &requests](ErrorCode errorCode, void* data, size_t size) {
				if (errorCode != EC_OK || !data) return false;
				const uint8_t* p = static_cast<const uint8_t*>(data);
				sess->buf.insert(sess->buf.end(), p, p + size);
				if (!sess->authDone) {
					if (sess->buf.size() < 3) return true;
					sess->buf.erase(sess->buf.begin(), sess->buf.begin() + 3);
					sess->authDone = true;
					uint8_t authReply[] = {0x05, 0x00};
					sess->stream->write(authReply, sizeof(authReply));
				}
				if (sess->buf.size() < 5) return true;
				size_t expected = sess->buf[3] == 0x03 ? 7 + sess->buf[4] : 10;
				if (sess->buf.size() < expected) return true;
				requests.push_back(sess->buf);
				sess->buf.clear();
				uint8_t connReply[] = {0x05, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
				sess->stream->write(connReply, sizeof(connReply));
				return true;
			});
		});

	int connected = 0;
	auto onConnected = [&connected, &reactor](uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
		BEAM_LOG_DEBUG() << "Proxy connection tag " << tag << ": " << error_str(status);
		PROXY_CHECK(status == EC_OK && newStream);
		if (++connected == 2) reactor->stop();
	};
	// "node.invalid" can never be resolved locally, only the proxy may resolve it
	PROXY_CHECK(reactor->tcp_connect_with_proxy(std::string("node.invalid"), 8100, proxyAddr, 20, onConnected, 2000));
	PROXY_CHECK(reactor->tcp_connect_with_proxy(std::string("123.45.67.89"), 80, proxyAddr, 21, onConnected, 2000));

	Timer::Ptr timer = Timer::create(*reactor);
	timer->start(3000, false, [&reactor] { BEAM_LOG_ERROR() << "proxy_domain_name_request_test watchdog"; reactor->stop(); });
	reactor->run();

	PROXY_CHECK(connected == 2);
	PROXY_CHECK(requests.size() == 2);
	if (requests.size() == 2) {
		std::vector<uint8_t> ipReference = {0x05, 0x01, 0x00, 0x01, 0x7B, 0x2D, 0x43, 0x59, 0x00, 0x50};
		const auto domainReference = domain_request("node.invalid", 8100);
		// the order of the requests depends on the connection order
		PROXY_CHECK((requests[0] == domainReference && requests[1] == ipReference) ||
					(requests[1] == domainReference && requests[0] == ipReference));
	}
	reactor->cancel_tcp_connect(20);
	reactor->cancel_tcp_connect(21);
}

}	// namespace

int main() {

	const int logLevel = BEAM_LOG_LEVEL_VERBOSE;
	auto logger = Logger::create(logLevel, logLevel);

	proxy_test();
	proxy_domain_name_request_test();

	if (g_failures) {
		BEAM_LOG_ERROR() << "proxy_test: " << g_failures << " check(s) failed";
		return 1;
	}
	return 0;
}
