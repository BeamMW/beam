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

// RFC 1928                SOCKS Protocol Version 5
// TODO mempool memory allocation optimisation may be implemented
// TODO: proxy insert to wallet CLI

namespace
{
	constexpr auto test_domain = "beam.mw";
	constexpr uint16_t test_http_port = 80;

	uint32_t callback_counter = 0;

	bool on_http_resp(ErrorCode errorCode, void* data, size_t size) {
		LOG_DEBUG() << "Http server response error code: " << errorCode;
		if (size > 0 && data != 0) {
			LOG_DEBUG() << "size: " << size << " string: \n" << std::string((const char*)data, size);
		}
		return true;
	}

	void on_connected(uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
		if (newStream) {
			LOG_DEBUG() << "CONNECTED TO DESTINATION";

			newStream->enable_read(on_http_resp);
			string req("GET / HTTP/1.0\r\nHost: ");
			req.append(test_domain);
			req.append("\r\n\r\n");
			Result res = newStream->write(req.c_str(), req.size());
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

	void proxy_test()
	{
		Reactor::Ptr reactor = Reactor::create();

		Address a;
		a.resolve(test_domain);
		// a.ip(0x7F000001);
		a.port(test_http_port);

		reactor->tcp_connect(a, 1, on_connected, 10000, false);

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
	config.set<uint16_t>("io.proxy_port", 9150);
	reset_global_config(std::move(config));

	proxy_test();

	return 0;
}
