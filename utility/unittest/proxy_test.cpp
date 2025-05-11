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

}	// namespace

int main() {

	const int logLevel = BEAM_LOG_LEVEL_VERBOSE;
	auto logger = Logger::create(logLevel, logLevel);

	proxy_test();

	return 0;
}
