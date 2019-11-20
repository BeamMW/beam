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
// TODO mempool memory allocation optimisation may be implemented
// TODO: proxy insert to wallet CLI

namespace {

constexpr auto testDomain = "123.45.67.89";
constexpr uint16_t testPort = 80;

constexpr auto dummyProxyServerIp = "localhost";
constexpr uint16_t dummyProxyServerPort = 1080;

TcpStream::Ptr serverStream;
TcpStream::Ptr clientStream;
enum class ConnState : uint8_t {
	Initial,
	AuthChosen,
	DestRequested
} testProxyConnState;

bool onTargetReply(ErrorCode errorCode, void* data, size_t size) {
	if (errorCode != EC_OK) {
		LOG_DEBUG() << "Destination server response error. Code: " << errorCode;
	}
	assert(data && size);
	string reply = "Aloha!";
	assert(std::equal(reply.cbegin(), reply.cend(), static_cast<uint8_t*>(data)));
	return true;
}

void onConnectionEstablished(uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
	if (!newStream) {
		LOG_DEBUG() << "Error connection establish: " << error_str(status);
		assert(false);
	}
	LOG_DEBUG() << "Proxy connection established. Tag: " << tag;
	clientStream = std::move(newStream);
	clientStream->enable_read(onTargetReply);
	auto req = "Hello, hello!";
	Result res = clientStream->write(req, strlen(req));
	assert(res);
};

void onInitialState(uint8_t* data, size_t size) {
	std::array<uint8_t,3> referenceAuthReq = {0x05, 0x01, 0x00};
	assert(std::equal(referenceAuthReq.cbegin(), referenceAuthReq.cend(), data));

	std::array<uint8_t,2> referenceAuthReply = {0x05, 0x00};
	serverStream->write(referenceAuthReply.data(), referenceAuthReply.size());
	testProxyConnState = ConnState::AuthChosen;
};

void onAuthState(uint8_t* data, size_t size) {
	// 123.45.67.89 = 0x7B2D4359
	// 80 = 0x50
	std::array<uint8_t,10> referenceAuthReq = {0x05, 0x01, 0x00, 0x01, 0x7B, 0x2D, 0x43, 0x59, 0x00, 0x50};
	assert(std::equal(referenceAuthReq.cbegin(), referenceAuthReq.cend(), data));

	// here we can really connect to requested destination...
	// and implement real proxy bridge

	std::array<uint8_t,10> referenceAuthReply = {0x05, 0x00, 0x00, 0x01, 0x12, 0x34, 0x56, 0x78, 0x98, 0x76};
	serverStream->write(referenceAuthReply.data(), referenceAuthReply.size());
	testProxyConnState = ConnState::DestRequested;
};

void onRequestState(uint8_t* data, size_t size) {
	string req = "Hello, hello!";
	assert(std::equal(req.cbegin(), req.cend(), data));
	auto dummyDestinationServerRepply = "Aloha!";
	serverStream->write(dummyDestinationServerRepply, strlen(dummyDestinationServerRepply));
};

bool onInputData(ErrorCode errorCode, void* data, size_t size) {
	if (errorCode != EC_OK) {
		LOG_DEBUG() << "testProxyConnState: " <<
						static_cast<uint8_t>(testProxyConnState) <<
						";  errorCode: " << errorCode;
		assert(false);
	}
	assert(data && size);
	uint8_t* buf = static_cast<uint8_t*>(data);
	switch(testProxyConnState) {
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

	// Connection timeout testcase
	uint32_t connTO = 2000;
	auto timeStart = std::time(nullptr);
	bool timeoutCalled = false;
	reactor->tcp_connect_with_proxy(
		destAddr,
		Address(0x7f000001, 12345),	// intentionally corrupted port
		1,
		[&timeStart, &connTO, &timeoutCalled](uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
			auto timeStop = std::time(nullptr);
			LOG_DEBUG() << "Timed out";
			assert(tag == 1);
			assert(status == EC_ETIMEDOUT);
			assert(timeStop >= timeStart+(connTO/1000));
			assert(timeStop <= timeStart+(connTO/1000)+1);	// 1-second accuracy
			timeoutCalled = true;
		},
		connTO,
		false);

	// Successful connection testcase
	TcpServer::Ptr server = TcpServer::create(
		*reactor,
		proxyAddr,
		[](TcpStream::Ptr&& newStream, int errorCode) {
			LOG_DEBUG() << "Dummy proxy server accepted connection";
			serverStream = std::move(newStream);
			serverStream->enable_read(onInputData);
		}
	);
	reactor->tcp_connect_with_proxy(destAddr, proxyAddr, 2,	onConnectionEstablished, 1000, false);

	// Run test
	Timer::Ptr timer = Timer::create(*reactor);
	timer->start(
		3000,
		false,
		[&reactor] {
			LOG_DEBUG() << "Watchdog called"; reactor->stop();
			}
		);
	LOG_DEBUG() << "reactor start";
	reactor->run();
	LOG_DEBUG() << "reactor stop";
	assert(timeoutCalled);
}

}	// namespace

int main() {

	int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
	logLevel = LOG_LEVEL_VERBOSE;
#endif

	auto logger = Logger::create(logLevel, logLevel);

	proxy_test();

	return 0;
}
