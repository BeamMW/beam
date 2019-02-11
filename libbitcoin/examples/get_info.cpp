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

#include "http/http_client.h"
#include "utility/io/timer.h"
#include "utility/helpers.h"
#include "utility/logger.h"

#include "bitcoin/bitcoin.hpp"

#include "nlohmann/json.hpp"

#include <iostream>

using namespace beam;
using json = nlohmann::json;

namespace 
{
const uint16_t PORT = 18443;
}

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);

    try {
        io::Reactor::Ptr reactor = io::Reactor::create();
        HttpClient client(*reactor);

        {
            std::string userWithPass("test:123");
            libbitcoin::data_chunk t(userWithPass.begin(), userWithPass.end());
            std::string auth("Basic " + libbitcoin::encode_base64(t));
            std::string_view authView(auth);
            static const HeaderPair headers[] = {
                //{"Host", "127.0.0.1" },
                //{"Connection", "close"},
                {"Authorization", authView.data()}
            };
            const char *data = "{\"method\":\"getblockchaininfo\",\"params\":[]}\n";

            HttpClient::Request request;
            io::Address a(io::Address::localhost(), PORT);

            request.address(a);
            request.connectTimeoutMsec(2000);
            request.pathAndQuery("/");
            request.headers(headers);
            request.numHeaders(1);
            request.method("POST");
            request.body(data, strlen(data));

            request.callback([&reactor](uint64_t id, const HttpMsgReader::Message& msg) -> bool {
                LOG_INFO() << "response from " << id;
                size_t sz = 0;
                const void* body = msg.msg->get_body(sz);
                json j = json::parse(std::string(static_cast<const char*>(body), sz));

                LOG_INFO() << j;
                LOG_INFO() << j["result"]["headers"];
                reactor->stop();
                return false;
            });

            client.send_request(request);
        }

        reactor->run();
    }
    catch (const std::exception& e) {
        LOG_ERROR() << e.what();
    }

    return 0;
}