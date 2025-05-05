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
#include <sstream>
#include <memory>

using namespace beam;
using namespace beam::io;
using namespace std;

namespace {

    void setup_test_CA(SSL_CTX* ctx, const std::string& caFile)
    {
        if (SSL_CTX_load_verify_locations(ctx, caFile.c_str(), nullptr) != 1) {
            BEAM_LOG_ERROR() << "SSL_CTX_load_verify_locations failed" << caFile;
            IO_EXCEPTION(EC_SSL_ERROR);
        }
    }

    int test_sslio(bool requestCertificate, bool rejectUnauthorized, const string& serverCertName, const string& clientCertName = "") {
        BEAM_LOG_INFO() << "Testing SSLIO " 
                   << TRACE(requestCertificate)
                   << TRACE(rejectUnauthorized)
                   << TRACE(serverCertName)
                   << TRACE(clientCertName);
        static const char a[] = "AAAAAAAAAAAAAAAAAAAA";
        static const char b[] = "BBBBBBBBBBBBBBBBBBBB";

        int nErrors = 0;
        size_t expectedSize = 0;
        size_t receivedSize = 0;

        auto on_decrypted = [&receivedSize](void* data, size_t size) -> bool {
            //BEAM_LOG_DEBUG() << "received " << size << " bytes";
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

        struct PathHelper
        {
            string certFileName;
            string certKeyFileName;
            PathHelper(const std::string& name)
            {
                std::stringstream ss;
                ss << PROJECT_SOURCE_DIR "/utility/unittest/" << name;
                string certPath = ss.str();

                certFileName = certPath + ".crt";
                certKeyFileName = certPath + ".key";
            }
        };

        PathHelper serverCert(serverCertName);

        SSLContext::Ptr serverCtx = SSLContext::create_server_ctx(
            serverCert.certFileName.c_str(), serverCert.certKeyFileName.c_str(), requestCertificate, rejectUnauthorized
        );

        PathHelper clientCert(clientCertName);
        SSLContext::Ptr clientCtx = SSLContext::create_client_context((clientCertName.empty() ? nullptr : clientCert.certFileName.c_str())
                                                                    , (clientCertName.empty() ? nullptr : clientCert.certKeyFileName.c_str()), rejectUnauthorized);

        const char* CAfile = PROJECT_SOURCE_DIR "/utility/unittest/beam_CA.crt";
        setup_test_CA(serverCtx->get(), CAfile);
        setup_test_CA(clientCtx->get(), CAfile);

        server = std::make_unique<SSLIO>(serverCtx, on_decrypted, on_encrypted_server);
        client = std::make_unique<SSLIO>(clientCtx, on_decrypted, on_encrypted_client);

        client->flush();

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
            BEAM_LOG_ERROR() << TRACE(expectedSize) << TRACE(receivedSize);
            ++nErrors;
        }
        BEAM_LOG_INFO() << TRACE(nErrors);
        return nErrors;
    }
}
#define CHECK_TRUE(s) {\
    auto r = (s);\
    retCode += r;\
}\

#define CHECK_FALSE(s) {\
    auto r = (s);\
    if (r == 0) \
        retCode += 1; \
}\

int main() {
    int logLevel = BEAM_LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = BEAM_LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    int retCode = 0;

    const std::string clientCert = "beam_client";
    const std::string serverCert = "beam_server";
    const std::string selfSignedCert = "test";

    try {
        CHECK_FALSE(test_sslio(false, true, selfSignedCert));
        CHECK_FALSE(test_sslio(true, true, selfSignedCert));
        CHECK_FALSE(test_sslio(true, true, selfSignedCert, clientCert));
        CHECK_TRUE(test_sslio(true, false, selfSignedCert));
        CHECK_TRUE(test_sslio(false, false, selfSignedCert));

// should work with installed test beam CA
        CHECK_TRUE(test_sslio(false, false, serverCert, selfSignedCert));
        // client has certificate, so it is AUTHORIZED, but certificate verification should fail
        CHECK_FALSE(test_sslio(true, false, serverCert, selfSignedCert));
        CHECK_TRUE(test_sslio(true, false, serverCert));
        CHECK_TRUE(test_sslio(false, true, serverCert));
        CHECK_FALSE(test_sslio(true, true, serverCert));
        CHECK_FALSE(test_sslio(true, true, serverCert, selfSignedCert));
        CHECK_TRUE(test_sslio(true, true, serverCert, clientCert));
    } catch (const exception& e) {
        BEAM_LOG_ERROR() << e.what();
        retCode = 255;
    } catch (...) {
        BEAM_LOG_ERROR() << "non-std exception";
        retCode = 255;
    }
    return retCode;
}


