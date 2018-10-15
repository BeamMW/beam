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

#include "bitcoin_rpc.h"
#include "bitcoin/bitcoin.hpp"

namespace beam
{
    BitcoinRPC::BitcoinRPC(io::Reactor& reactor, const std::string& userName, const std::string& pass, const io::Address& address)
        : m_httpClient(reactor)
        , m_userName(userName)
        , m_pass(pass)
        , m_address(address)
        //, m_headers{ {"Host", "127.0.0.1" }, {"Connection", "close"}, {"Authorization", "basi"} }
    {

    }

    void BitcoinRPC::getBlockchainInfo(OnResponse callback)
    {
        /*std::string userWithPass("test:123");
        libbitcoin::data_chunk t(userWithPass.begin(), userWithPass.end());
        std::string auth("Basic " + libbitcoin::encode_base64(t));

        const HeaderPair headers[] = {
            {"Host", "127.0.0.1" },
            {"Connection", "close"},
            {"Authorization", auth.data()}
        };*/
    }
}