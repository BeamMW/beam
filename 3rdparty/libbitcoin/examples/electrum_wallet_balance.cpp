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
#include "utility/io/timer.h"
#include "utility/io/tcpstream.h"
#include "utility/config.h"
#include "utility/helpers.h"
#include "utility/logger.h"

#include "bitcoin/bitcoin.hpp"

#include <iostream>

using namespace beam;
using namespace beam::io;
using namespace libbitcoin;
using namespace libbitcoin::wallet;

namespace
{
    uint64_t tag_ok = 100;
    TcpStream::Ptr tcpStream = nullptr;

    std::vector<std::string> scriptHashes;
    size_t ind = 0;

    void getBalance(const std::string& scriptHash)
    {
        std::string request = R"({"method":"blockchain.scripthash.listunspent","params":[")" + scriptHash + R"("], "id": "teste"})";
        request += "\n";
        Result res = tcpStream->write(request.data(), request.size());
        if (!res) {
            LOG_ERROR() << error_str(res.error());
        }
    }

    bool on_recv(ErrorCode what, void* data, size_t size) {
        if (data && size) {
            LOG_DEBUG() << "RECEIVED " << size << " bytes";
            LOG_DEBUG() << "index = " << ind << "\n" << std::string((const char*)data, size);

            if (ind < scriptHashes.size())
            {
                getBalance(scriptHashes[ind++]);
                return true;
            }
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
            
            tcpStream = std::move(newStream);
            tcpStream->enable_read(on_recv);

            getBalance(scriptHashes[ind++]);
        }
        else {
            LOG_DEBUG() << __FUNCTION__ << " ERROR: " << error_str(status);
            io::Reactor::get_Current().stop();
        }
    }

    std::string generateScriptHash(const ec_public& publicKey)
    {
        payment_address addr = publicKey.to_payment_address(ec_private::testnet);
        auto script = chain::script(chain::script::to_pay_key_hash_pattern(addr.hash()));
        libbitcoin::data_chunk secretHash = libbitcoin::sha256_hash_chunk(script.to_data(false));
        libbitcoin::data_chunk reverseSecretHash(secretHash.rbegin(), secretHash.rend());
        return libbitcoin::encode_base16(reverseSecretHash);
    }

    std::vector<std::string> getScriptHashes()
    {
        word_list my_word_list{ "child", "happy", "moment", "weird", "ten", "token", "stuff", "surface", "success", "desk", "embark", "observe" };

        auto hd_seed = electrum::decode_mnemonic(my_word_list);
        data_chunk seed_chunk(to_chunk(hd_seed));
        hd_private masterPrivateKey(seed_chunk, hd_public::testnet);

        auto receivingPrivateKey = masterPrivateKey.derive_private(0);
        auto changePrivateKey = masterPrivateKey.derive_private(1);

        std::vector<std::string> result;

        for (uint32_t i = 0; i < 21; i++)
        {
            result.push_back(generateScriptHash(receivingPrivateKey.to_public().derive_public(i).point()));
        }

        for (uint32_t i = 0; i < 6; i++)
        {
            result.push_back(generateScriptHash(changePrivateKey.to_public().derive_public(i).point()));
        }

        return result;
    }
}

int main() {
    int logLevel = LOG_LEVEL_DEBUG;
    auto logger = Logger::create(logLevel, logLevel);

    try {
        io::Reactor::Ptr reactor = io::Reactor::create();
        io::Reactor::Scope scope(*reactor);

        scriptHashes = getScriptHashes();

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