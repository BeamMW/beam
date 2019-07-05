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

#include "utility/io/reactor.h"
#include "utility/io/timer.h"
#include "utility/io/tcpstream.h"
#include "utility/config.h"
#include "utility/helpers.h"
#include "utility/logger.h"

#include "bitcoin/bitcoin.hpp"

#include <iostream>

using namespace beam;
using namespace beam::io;

namespace
{
    uint64_t tag_ok = 100;
    std::vector<TcpStream::Ptr> streams;

    bool on_recv(ErrorCode what, void* data, size_t size) {
        if (data && size) {
            LOG_DEBUG() << "RECEIVED " << size << " bytes";
            LOG_DEBUG() << "\n" << std::string((const char*)data, size);
        }
        else {
            LOG_DEBUG() << __FUNCTION__ << " ERROR: " << error_str(what);
        }
        io::Reactor::get_Current().stop();
        return true;
    };

    void on_connected(uint64_t tag, std::unique_ptr<TcpStream>&& newStream, ErrorCode status)
    {
        LOG_INFO() << "on_connected";
        if (newStream) {
            assert(status == EC_OK);
            newStream->enable_read(on_recv);

            // legacy address mkgTKdapn48BM8BaMTDSnd1miT1AZSjV7P
            std::string hexPubKey = "76a91438a49a6f46ab5f3c7ab7f79e7cd142ac3b57bccf88ac";
            //libbitcoin::data_chunk t(hexPubKey.begin(), hexPubKey.end());
            libbitcoin::data_chunk t;
            libbitcoin::decode_base16(t, hexPubKey);
            libbitcoin::data_chunk secretHash = libbitcoin::sha256_hash_chunk(t);
            std::string hash = libbitcoin::encode_base16(secretHash);
            std::string reverseHash = "";
            for (auto idx = hash.rbegin(); idx != hash.rend(); ++idx)
            {
                auto tmp = idx;
                
                ++idx;
                if (tmp != hash.rend())
                {
                    reverseHash += *idx;
                    reverseHash += *tmp;
                }
            }
            
            LOG_INFO() << reverseHash;
            std::string request = R"({"method":"blockchain.scripthash.get_balance","params":[")" + reverseHash +R"("], "id": "teste"})";
            request += "\n";
            
            LOG_INFO() << request;
            Result res = newStream->write(request.data(), request.size());
            if (!res) {
                LOG_ERROR() << error_str(res.error());
            }

            LOG_INFO() << "after write";
            streams.emplace_back(move(newStream));
        }
        else {
            LOG_DEBUG() << __FUNCTION__ << " ERROR: " << error_str(status);
            io::Reactor::get_Current().stop();
        }
    }
}

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);

    try {
        io::Reactor::Ptr reactor = io::Reactor::create();
        io::Reactor::Scope scope(*reactor);
        
        Address address;
        /*address.resolve("testnet.hsmiths.com");
        address.port(53012);*/

        /*address.resolve("testnet.qtornado.com");
        address.port(51002);*/
        address.resolve("testnet1.bauerj.eu");
        address.port(50002);

        reactor->tcp_connect(address, tag_ok, on_connected, 2000, true);
        reactor->run();
    }
    catch (const std::exception& e) {
        LOG_ERROR() << e.what();
    }

    return 0;
}