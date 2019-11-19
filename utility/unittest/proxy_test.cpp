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

constexpr auto testDomain = "beam.mw";
constexpr uint16_t testPort = 80;

constexpr uint32_t dummyProxyServerIp = 0x7F000001;
constexpr uint16_t dummyProxyServerPort = 1080;

TcpStream::Ptr serverStream;
TcpStream::Ptr clientStream;
enum class ConnState : uint8_t {
	Initial,
	AuthChosen,
	DestRequested
} testProxyConnState;

bool onTargetReply(ErrorCode errorCode, void* data, size_t size) {
	LOG_DEBUG() << "Http server response error code: " << errorCode;
	if (size > 0 && data != 0) {
		LOG_DEBUG() << "size: " << size << " string: \n" << std::string((const char*)data, size);
	}
	return true;
}

void onConnectionEstablished(uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
	if (newStream) {
		LOG_DEBUG() << "CONNECTED TO DESTINATION";

		clientStream = std::move(newStream);

		clientStream->enable_read(onTargetReply);
		string req("GET / HTTP/1.0\r\nHost: ");
		req.append(testDomain);
		req.append("\r\n\r\n");
		Result res = clientStream->write(req.c_str(), req.size());
		if (!res) {
			LOG_DEBUG() << "Http request write result: " << error_str(res.error());
		}
		else {
			LOG_DEBUG() << "Http request write result: OK";
		}
	}
	else {
		LOG_DEBUG() << __FUNCTION__ << " ERROR: " << error_str(status);
	}
};

void onInitialState(uint8_t* data, size_t size) {
	std::array<uint8_t,3> referenceAuthReq = {0x05, 0x01, 0x00};
	assert(std::equal(referenceAuthReq.cbegin(), referenceAuthReq.cend(), data));

	std::array<uint8_t,2> referenceAuthReply = {0x05, 0x00};
	serverStream->write(referenceAuthReply.data(), referenceAuthReply.size());
	testProxyConnState = ConnState::AuthChosen;
};

void onAuthState(uint8_t* data, size_t size) {
	std::array<uint8_t,4> referenceAuthReq = {0x05, 0x01, 0x00, 0x01};
	assert(std::equal(referenceAuthReq.cbegin(), referenceAuthReq.cend(), data));

	// here we can really connect to requested destination...
	// and implement real proxy bridge

	std::array<uint8_t,10> referenceAuthReply = {0x05, 0x00, 0x00, 0x01, 0x12, 0x34, 0x56, 0x78, 0x98, 0x76};
	serverStream->write(referenceAuthReply.data(), referenceAuthReply.size());
	testProxyConnState = ConnState::DestRequested;
};

void onRequestState(uint8_t* data, size_t size) {
	auto dummyDestinationServerRepply = "Dummy reply from requested domain";
	serverStream->write(dummyDestinationServerRepply, strlen(dummyDestinationServerRepply));
};

bool onInputData(ErrorCode errorCode, void* data, size_t size) {
	if (errorCode != EC_OK) {
		LOG_DEBUG() << "testProxyConnState: " <<
						static_cast<uint8_t>(testProxyConnState) <<
						";  errorCode: " << errorCode;
	}
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

void proxy_test() {
	Reactor::Ptr reactor = Reactor::create();

	TcpServer::Ptr server = TcpServer::create(
		*reactor,
		Address(dummyProxyServerIp, dummyProxyServerPort),
		[](TcpStream::Ptr&& newStream, int errorCode) {
			LOG_DEBUG() << "TcpServer accepted connection";
			serverStream = std::move(newStream);
			serverStream->enable_read(onInputData);
		}
	);

	Address a;
	a.resolve(testDomain);
	a.port(testPort);

	reactor->tcp_connect(a, 1, onConnectionEstablished, 10000, false);

	Timer::Ptr timer = Timer::create(*reactor);
	timer->start(10000, false, [&reactor]{ 
		LOG_DEBUG() << "alarm called"; reactor->stop(); } );

	LOG_DEBUG() << "starting reactor...";
	reactor->run();
	LOG_DEBUG() << "reactor stopped";

}

}	// namespace

int main() {

	int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
	logLevel = LOG_LEVEL_VERBOSE;
#endif

	auto logger = Logger::create(logLevel, logLevel);

	Config config;
	config.set<bool>("io.proxy_socks5", true);
	config.set<string>("io.proxy_address", "127.0.0.1");
	config.set<uint16_t>("io.proxy_port", dummyProxyServerPort);
	reset_global_config(std::move(config));

	proxy_test();

	return 0;
}
