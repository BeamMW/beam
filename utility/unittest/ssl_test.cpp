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

#include "utility/io/sslio.h"
#include "utility/logger.h"

using namespace beam;
using namespace beam::io;
using namespace std;

namespace {
    int test_sslio() {
        static const char a[] = "AAAAAAAAAAAAAAAAAAAA";
        static const char b[] = "BBBBBBBBBBBBBBBBBBBB";

        int nErrors = 0;
        size_t expectedSize = 0;
        size_t receivedSize = 0;

        auto on_decrypted = [&receivedSize](void* data, size_t size) -> bool {
            LOG_DEBUG() << "received " << size << " bytes";
            receivedSize += size;
            return true;
        };

        std::unique_ptr<SSLIO> server;
        std::unique_ptr<SSLIO> client;

        auto on_encrypted_server = [&client, &nErrors](const io::SharedBuffer& data, bool flush) -> Result {
            if (!client) {
                ++nErrors;
                return make_unexpected(EC_EINVAL);
            }
            client->on_encrypted_data_from_stream(data.data, data.size);
            return Ok();
        };

        auto on_encrypted_client = [&server, &nErrors](const io::SharedBuffer& data, bool flush) -> Result {
            if (!server) {
                ++nErrors;
                return make_unexpected(EC_EINVAL);
            }
            server->on_encrypted_data_from_stream(data.data, data.size);
            return Ok();
        };

        SSLContext::Ptr serverCtx = SSLContext::create_server_ctx(
            PROJECT_SOURCE_DIR "/utility/unittest/test.crt", PROJECT_SOURCE_DIR "/utility/unittest/test.key"
        );
        SSLContext::Ptr clientCtx = SSLContext::create_client_context();

        server = std::make_unique<SSLIO>(serverCtx, on_decrypted, on_encrypted_server);
        client = std::make_unique<SSLIO>(clientCtx, on_decrypted, on_encrypted_client);
        SharedBuffer aBuf(a, strlen(a));
        SharedBuffer bBuf(b, strlen(b));
        for (int i=0; i<4321; ++i) {
            client->enqueue(aBuf);
            expectedSize += aBuf.size;
            client->enqueue(bBuf);
            expectedSize += bBuf.size;
        }
        client->flush();

        if (expectedSize != receivedSize) {
            LOG_ERROR() << TRACE(expectedSize) << TRACE(receivedSize);
            ++nErrors;
        }
        return nErrors;
    }
}

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    int retCode = 0;
    try {
        retCode += test_sslio();
    } catch (const exception& e) {
        LOG_ERROR() << e.what();
        retCode = 255;
    } catch (...) {
        LOG_ERROR() << "non-std exception";
        retCode = 255;
    }
    return retCode;
}


