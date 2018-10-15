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

#pragma once

#include "p2p/http_client.h"

namespace beam
{
    class BitcoinRPC
    {
    public:
        using OnResponse = std::function<void(const std::string&)>;

        BitcoinRPC() = delete;
        BitcoinRPC(io::Reactor& reactor, const std::string& userName, const std::string& pass, const io::Address& address);

        void getBlockchainInfo(OnResponse callback);
        void dumpPrivKey(const std::string& btcAddress, OnResponse callback);
        void fundRawTransaction(const std::string& rawTx, OnResponse callback);
        void signRawTransaction(OnResponse callback);
        void sendRawTransaction(const std::string& rawTx, OnResponse callback);
        void getNetworkInfo(OnResponse callback);
        void getWalletInfo(OnResponse callback);
        void estimateFee(int blocks, OnResponse callback);
        void getRawChangeAddress(OnResponse callback);
        void createRawTransaction(OnResponse callback);
        void getRawTransaction(const std::string& txid, OnResponse callback);
        void getBalance(OnResponse callback);

    private:

        void sendRequest(const std::string& method, const std::string& params, OnResponse callback);

    private:
        HttpClient m_httpClient;
        //std::string m_userName;
        //std::string m_pass;
        io::Address m_address;
        std::string m_authorization;
        //const HeaderPair m_headers[];
    };
}