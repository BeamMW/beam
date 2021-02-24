// Copyright 2021 The Beam Team
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

// #include "wallet/client/wallet_client.h"
#include <emscripten/bind.h>
#include <string>
#include <thread>
#include <sstream>
#include <chrono>
#include <vector>
#include <mutex>
#include "wallet/core/wallet_db.h"
//#include "wallet/client/wallet_client.h"

#include "utility/io/reactor.h"
//#ifndef LOG_VERBOSE_ENABLED
//#define LOG_VERBOSE_ENABLED 0
//#endif
#include "utility/logger.h"
#include <future>
#include <iostream>

#include <stdio.h>

#include <stdlib.h>

#include <openssl/bio.h> /* BasicInput/Output streams */

#include <openssl/err.h> /* errors */

#include <openssl/ssl.h> /* core library */

#define BuffSize 1024

using namespace beam;
using namespace beam::io;
using namespace std;

using namespace emscripten;
//using namespace beam;
using namespace beam::wallet;

class WasmWalletClient //: public WalletClient
{
public:
    WasmWalletClient(const std::string& s)
        : m_Seed(s)
    {}

    std::string TestThreads()
    {
        std::vector<std::thread> threads;

        for (int i = 0; i < 5; ++i)
        {
            threads.emplace_back([this]() { ThreadFunc(); });
        }

        for (auto& t : threads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }

        return "TestThreads " + std::to_string(std::thread::hardware_concurrency()) + m_Seed;
    }

    void ThreadFunc()
    {
        std::stringstream ss;
        ss << "\nThread #" << std::this_thread::get_id();
        {
            std::unique_lock lock(m_Mutex);
            m_Seed += ss.str();
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::string TestWalletDB()
    {
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = 10283UL;
        puts("TestWalletDB...");
        auto walletDB = WalletDB::init("file:////c//Data//wallet.db", std::string("123"), seed);
        if (walletDB)
        {
            puts("setting new state...");
            beam::Block::SystemState::ID id = { };
            id.m_Height = 134;
            walletDB->setSystemStateID(id);
            return std::to_string(walletDB->getCurrentHeight());
        }
        else
        {
            puts("failed to open");
            return "";
        }
        //return "";
    }

    void report_and_exit(const char* msg) {
        perror(msg);
        ERR_print_errors_fp(stderr);
        exit(-1);
    }

    void init_ssl() {
        SSL_load_error_strings();
        SSL_library_init();
    }

    void cleanup(SSL_CTX* ctx, BIO* bio) {
        SSL_CTX_free(ctx);
        BIO_free_all(bio);
    }

    void secure_connect(const char* hostname) {
        char name[BuffSize];
        char request[BuffSize];
        char response[BuffSize];

        const SSL_METHOD* method = TLSv1_2_client_method();
        if (NULL == method) report_and_exit("TLSv1_2_client_method...");

        SSL_CTX* ctx = SSL_CTX_new(method);
        if (NULL == ctx) report_and_exit("SSL_CTX_new...");

        BIO* bio = BIO_new_ssl_connect(ctx);
        if (NULL == bio) report_and_exit("BIO_new_ssl_connect...");

        SSL* ssl = NULL;

        /* link bio channel, SSL session, and server endpoint */

        sprintf(name, "%s:%s", hostname, "https");
        BIO_get_ssl(bio, &ssl); /* session */
        SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY); /* robustness */
        BIO_set_conn_hostname(bio, name); /* prepare to connect */

        /* try to connect */
        if (BIO_do_connect(bio) <= 0) {
            cleanup(ctx, bio);
            report_and_exit("BIO_do_connect...");
        }

        /* verify truststore, check cert */
        if (!SSL_CTX_load_verify_locations(ctx,
            "/etc/ssl/certs/ca-certificates.crt", /* truststore */
            "/etc/ssl/certs/")) /* more truststore */
            report_and_exit("SSL_CTX_load_verify_locations...");

        long verify_flag = SSL_get_verify_result(ssl);
        if (verify_flag != X509_V_OK)
            fprintf(stderr,
                "##### Certificate verification error (%i) but continuing...\n",
                (int)verify_flag);

        /* now fetch the homepage as sample data */
        sprintf(request,
            "GET / HTTP/1.1\x0D\x0AHost: %s\x0D\x0A\x43onnection: Close\x0D\x0A\x0D\x0A",
            hostname);
        BIO_puts(bio, request);

        /* read HTTP response from server and print to stdout */
        while (1) {
            memset(response, '\0', sizeof(response));
            int n = BIO_read(bio, response, BuffSize);
            if (n <= 0) break; /* 0 is end-of-stream, < 0 is an error */
            puts(response);
        }

        cleanup(ctx, bio);
    }

    int TestOpenSSL() {
        init_ssl();

        const char* hostname = "www.google.com:443";
        fprintf(stderr, "Trying an HTTPS connection to %s...\n", hostname);
        secure_connect(hostname);

        return 0;
    }

    void TestReactor()
    {
    
            Reactor::Ptr reactor = Reactor::create();
          
            auto f = std::async(
                std::launch::async,
                [reactor]() {
                this_thread::sleep_for(chrono::microseconds(300000));
                //usleep(300000);
                LOG_DEBUG() << "stopping reactor from foreign thread...";
                reactor->stop();
            }
            );
          
            LOG_DEBUG() << "starting reactor...";;
            reactor->run();
            LOG_DEBUG() << "reactor stopped";
          
            f.get();
    }

private:
    
   // WalletClient m_Client;
    std::string m_Seed;
    std::mutex m_Mutex;
};

//struct WasmClientWrapper
//{
//public:
//    WasmClientWrapper(const std::string& phrase)
//    {
//        _client = make_unique<WalletModel>(walletDB, "127.0.0.1:10005", reactor);
//    }
//
//private:
//    std::unique_ptr<WasmClient> _client;
//
//};
// Binding code
EMSCRIPTEN_BINDINGS() 
{
    class_<WasmWalletClient>("WasmWalletClient")
        .constructor<const std::string&>()
        .function("testThreads",                &WasmWalletClient::TestThreads)
        .function("testOpenSSL",                &WasmWalletClient::TestOpenSSL)
        .function("testReactor",                &WasmWalletClient::TestReactor)
        .function("testWalletDB",               &WasmWalletClient::TestWalletDB)
 
        ;
}