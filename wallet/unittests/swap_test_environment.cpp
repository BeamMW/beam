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

#include "nlohmann/json.hpp"

#include "bitcoin/bitcoin.hpp"

using namespace beam;
using namespace beam::wallet;
using namespace std;
using namespace ECC;
using json = nlohmann::json;

class TestBitcoinWallet
{
public:

    struct Options
    {
        string m_rawAddress = "2NB9nqKnHgThByiSzVEVDg5cYC2HwEMBcEK";
        string m_privateKey = "cTZEjMtL96FyC43AxEvUxbs3pinad2cH8wvLeeCYNUwPURqeknkG";
        string m_refundTx = "";
        Amount m_amount = 0;
    };

public:

    TestBitcoinWallet(io::Reactor& reactor, const io::Address& addr, const Options& options)
        : m_reactor(reactor)
        , m_httpClient(reactor)
        , m_msgCreator(1000)
        , m_lastId(0)
        , m_options(options)
    {
        m_server = io::TcpServer::create(
            m_reactor,
            addr,
            BIND_THIS_MEMFN(onStreamAccepted)
        );
    }

    void addPeer(const io::Address& addr)
    {
        m_peers.push_back(addr);
    }

private:

    void onStreamAccepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
    {
        if (errorCode == 0)
        {
            LOG_DEBUG() << "Stream accepted";
            uint64_t peerId = m_lastId++;
            m_connections[peerId] = std::make_unique<HttpConnection>(
                peerId,
                BaseConnection::inbound,
                BIND_THIS_MEMFN(onRequest),
                10000,
                1024,
                std::move(newStream)
                );
        }
        else
        {
            LOG_ERROR() << "Server error " << io::error_str(errorCode);
            //g_stopEvent();
            m_reactor.stop();
        }
    }

    bool onRequest(uint64_t peerId, const HttpMsgReader::Message& msg)
    {
        const char* message = "OK";
        static const HeaderPair headers[] =
        {
            {"Server", "BitcoinHttpServer"}
        };
        io::SharedBuffer body = generateResponse(msg);
        io::SerializedMsg serialized;

        if (m_connections[peerId] && m_msgCreator.create_response(
            serialized, 200, message, headers, sizeof(headers) / sizeof(HeaderPair),
            1, "text/plain", body.size))
        {
            serialized.push_back(body);
            m_connections[peerId]->write_msg(serialized);
            m_connections[peerId]->shutdown();
        }
        else
        {
            LOG_ERROR() << "Cannot create response";
            //g_stopEvent();
        }

        m_connections.erase(peerId);

        return false;
    }

    io::SharedBuffer generateResponse(const HttpMsgReader::Message& msg)
    {
        if (msg.what != HttpMsgReader::http_message)
        {
            LOG_ERROR() << "TestBitcoinWallet, connection error: " << msg.error_str();
            return {};
        }

        size_t sz = 0;
        const void* rawReq = msg.msg->get_body(sz);
        std::string result;
        if (sz > 0 && rawReq)
        {
            std::string req(static_cast<const char*>(rawReq), sz);
            json j = json::parse(req);
            if (j["method"] == "fundrawtransaction")
            {
                std::string hexTx = j["params"][0];
                libbitcoin::data_chunk tx_data;
                libbitcoin::decode_base16(tx_data, hexTx);
                libbitcoin::chain::transaction tx;
                tx.from_data_without_inputs(tx_data);

                libbitcoin::chain::input input;

                tx.inputs().push_back(input);

                std::string hexNewTx = libbitcoin::encode_base16(tx.to_data());

                result = R"({"result":{"hex":")" + hexNewTx + R"(", "fee": 0, "changepos": 0},"error":null,"id":null})";
            }
            else if (j["method"] == "dumpprivkey")
            {
                result = R"({"result":")" + m_options.m_privateKey + R"(","error":null,"id":null})";
            }
            else if (j["method"] == "signrawtransactionwithwallet")
            {
                std::string hexTx = j["params"][0];
                result = R"({"result": {"hex": ")" + hexTx + R"(", "complete": true},"error":null,"id":null})";
            }
            else if (j["method"] == "decoderawtransaction")
            {
                std::string hexTx = j["params"][0];

                libbitcoin::data_chunk tx_data;
                libbitcoin::decode_base16(tx_data, hexTx);
                libbitcoin::chain::transaction tx = libbitcoin::chain::transaction::factory_from_data(tx_data);

                std::string txId = libbitcoin::encode_hash(tx.hash());
                result = R"({"result": {"txid": ")" + txId + R"("},"error":null,"id":null})";
            }
            else if (j["method"] == "createrawtransaction")
            {
                result = R"({"result": ")" + m_options.m_refundTx + R"(","error":null,"id":null})";
            }
            else if (j["method"] == "getrawchangeaddress")
            {
                result = R"( {"result":")" + m_options.m_rawAddress + R"(","error":null,"id":null})";
            }
            else if (j["method"] == "sendrawtransaction")
            {
                std::string hexTx = j["params"][0];

                libbitcoin::data_chunk tx_data;
                libbitcoin::decode_base16(tx_data, hexTx);
                libbitcoin::chain::transaction tx = libbitcoin::chain::transaction::factory_from_data(tx_data);

                std::string txId = libbitcoin::encode_hash(tx.hash());

                if (m_transactions.find(txId) == m_transactions.end())
                {
                    m_transactions[txId] = make_pair(hexTx, 0);
                    sendRawTransaction(req);
                }

                result = R"( {"result":")" + txId + R"(","error":null,"id":null})";
            }
            else if (j["method"] == "gettxout")
            {
                std::string txId = j["params"][0];
                std::string lockScript = "";
                int confirmations = 0;

                auto idx = m_transactions.find(txId);
                if (idx != m_transactions.end())
                {
                    confirmations = ++idx->second.second;
                    libbitcoin::data_chunk tx_data;
                    libbitcoin::decode_base16(tx_data, idx->second.first);
                    libbitcoin::chain::transaction tx = libbitcoin::chain::transaction::factory_from_data(tx_data);

                    auto script = tx.outputs()[0].script();

                    lockScript = libbitcoin::encode_base16(script.to_data(false));
                }

                result = R"( {"result":{"confirmations":)" + std::to_string(confirmations) + R"(,"value":)" + std::to_string(double(m_options.m_amount) / libbitcoin::satoshi_per_bitcoin) + R"(,"scriptPubKey":{"hex":")" + lockScript + R"("}},"error":null,"id":null})";
            }
            else if (j["method"] == "getblockcount")
            {
                result = R"( {"result":)" + std::to_string(m_blockCount++) + R"(,"error":null,"id":null})";
            }
        }
        else
        {
            LOG_ERROR() << "Request is wrong";
            //g_stopEvent();
        }

        io::SharedBuffer body;

        body.assign(result.data(), result.size());
        return body;
    }

    void sendRawTransaction(const string& msg)
    {
        for (const auto& peer : m_peers)
        {
            HttpClient::Request request;

            request.address(peer)
                .connectTimeoutMsec(2000)
                .pathAndQuery("/")
                .method("POST")
                .body(msg.c_str(), msg.size());

            request.callback([](uint64_t, const HttpMsgReader::Message&) -> bool {
                return false;
            });

            m_httpClient.send_request(request);
        }
    }

private:
    io::Reactor& m_reactor;
    io::TcpServer::Ptr m_server;
    HttpClient m_httpClient;
    std::map<uint64_t, HttpConnection::Ptr> m_connections;
    HttpMsgCreator m_msgCreator;
    uint64_t m_lastId;
    Options m_options;
    std::vector<io::Address> m_peers;
    std::vector<std::string> m_rawTransactions;
    std::map<std::string, std::pair<std::string, int>> m_transactions;
    std::map<std::string, int> m_txConfirmations;
    uint64_t m_blockCount = 100;
};

