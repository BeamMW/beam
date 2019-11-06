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
// proxy connection object
// pass object handlers to callbacks
// insert to wallet

namespace
{
	constexpr auto test_domain = "beam.mw";
	constexpr uint16_t test_proxy_port = 9150;
	constexpr uint16_t test_http_port = 80;

	std::vector<TcpStream::Ptr> streams;
	
	bool on_auth_req(ErrorCode what, void* data, size_t size) {
		if (data && size) {
			std::string response = beam::to_hex(data, size);
			LOG_DEBUG() << "RECEIVED " << size << " bytes: " << response;

			static const uint8_t authRequest[3] = {
				0x05,	// The VER field is set to X'05' for this version of the protocol.
				0x01,	// The NMETHODS field contains the number of method identifier octets
						// that appear in the METHODS field.
				0x00	// METHODS field. The values currently defined for METHOD are:
						// X'00' NO AUTHENTICATION REQUIRED
						// X'01' GSSAPI
						// X'02' USERNAME / PASSWORD
						// X'03' to X'7F' IANA ASSIGNED
						// X'80' to X'FE' RESERVED FOR PRIVATE METHODS
						// X'FF' NO ACCEPTABLE METHODS
			};
			Result res = streams.back()->write(authRequest, sizeof authRequest);
			if (!res) {
				LOG_ERROR() << error_str(res.error());
			}
		}
		else {
			LOG_DEBUG() << __FUNCTION__ << " ERROR: " << error_str(what);
		}
		return true;
	};

	void on_connected(uint64_t tag, unique_ptr<TcpStream>&& newStream, ErrorCode status) {
		if (newStream) {
			newStream->enable_read(on_auth_req);
			static const uint8_t authRequest[3] = {
				0x05,	// The VER field is set to X'05' for this version of the protocol.
				0x01,	// The NMETHODS field contains the number of method identifier octets
						// that appear in the METHODS field.
				0x00	// METHODS field. The values currently defined for METHOD are:
						// X'00' NO AUTHENTICATION REQUIRED
						// X'01' GSSAPI
						// X'02' USERNAME / PASSWORD
						// X'03' to X'7F' IANA ASSIGNED
						// X'80' to X'FE' RESERVED FOR PRIVATE METHODS
						// X'FF' NO ACCEPTABLE METHODS
			};
			Result res = newStream->write(authRequest, sizeof authRequest);
			if (!res) {
				LOG_ERROR() << error_str(res.error());
			}
			streams.emplace_back(move(newStream));
		}
		else {
			LOG_DEBUG() << __FUNCTION__ << " ERROR: " << error_str(status);
		}
	};

	void proxy_test()
	{
		Reactor::Ptr reactor = Reactor::create();

		Address a(Address::localhost(), test_proxy_port);

		reactor->tcp_connect(a, 1, on_connected, 10000, false);

		Timer::Ptr timer = Timer::create(*reactor);
		timer->start(2000, false, [&reactor]{ 
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
	config.set<Config::Int>("io.connect_timer_resolution", 1);
	reset_global_config(std::move(config));

	proxy_test();

	return 0;
}
