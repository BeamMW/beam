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

using namespace beam;

namespace {
    int http_client_test() {
        int nErrors = 0;

        io::Reactor::Ptr reactor;

        try {
            reactor = io::Reactor::create();

            std::map<uint64_t, io::ErrorCode> expected;

            static const char DOMAIN_NAME[] = "example.com";
            io::Address a;
            a.resolve(DOMAIN_NAME);
            a.port(80);

            static const HeaderPair headers[] = {
                {"Host", DOMAIN_NAME }
            };

            HttpClient client(*reactor);

            HttpClient::Request request;
            request.address(a).connectTimeoutMsec(2000).pathAndQuery("/").headers(headers).numHeaders(1)
                .callback(
                    [&expected, &nErrors](uint64_t id, const HttpMsgReader::Message& msg) -> bool {
                        LOG_DEBUG() << "response from " << id;
                        auto it = expected.find(id);
                        if (it == expected.end()) {
                            LOG_DEBUG() << "unexpected response";
                            ++nErrors;
                        }
                        if (msg.what == HttpMsgReader::http_message) {
                            size_t sz=0;
                            /*const void* body =*/ msg.msg->get_body(sz);
                            LOG_DEBUG() << "received " << sz << " bytes";
                            //if (body) {
                            //    LOG_DEBUG() << std::string((const char*)body, sz);
                            //}
                            if (it->second != io::EC_OK || sz == 0) ++nErrors;
                        } else {
                            if (it->second != msg.connectionError) {
                                ++nErrors;
                            }
                        }
                        expected.erase(it);
                        return false;
                    }
                );

            auto res = client.send_request(request);
            if (!res) ++nErrors;
            else expected[*res] = io::EC_OK;

            uint64_t cancelID = 0;
            res = client.send_request(request);
            if (!res) ++nErrors;
            else cancelID = *res;

            request.address(io::Address::localhost().port(666));
            res = client.send_request(request);
            if (!res) ++nErrors;
            else expected[*res] = io::EC_ECONNREFUSED;

            request.address(a.port(666));
            res = client.send_request(request);
            if (!res) ++nErrors;
            else expected[*res] = io::EC_ETIMEDOUT;

            io::Timer::Ptr timer = io::Timer::create(*reactor);
            int x = 30;
            timer->start(0, false, [&]{
                client.cancel_request(cancelID);
                timer->start(100, true, [&]{
                    if (--x == 0 || expected.empty()) {
                        reactor->stop();
                    }
                });
            });
            reactor->run();

            nErrors += (int)expected.size();
        } catch (const std::exception& e) {
            LOG_ERROR() << e.what();
            nErrors = 255;
        }

        return nErrors;
    }

} //namespace

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    return http_client_test();
}

