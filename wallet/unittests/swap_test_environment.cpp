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

    uint64_t getBlockCount()
    {
        return m_blockCount; 
    }

private:

    void onStreamAccepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
    {
        if (errorCode == 0)
        {
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
            else if (j["method"] == "getblockhash")
            {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
                result = R"( {"result":"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f","error":null,"id":"verify"})";
#else
                result = R"( {"result":"0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206","error":null,"id":"verify"})";
#endif
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
                .connectTimeoutMsec(200000)
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

class TestElectrumWallet
{
public:
    TestElectrumWallet(io::Reactor& reactor, const std::string& addr)
        : m_reactor(reactor)
    {
        io::Address address;
        address.resolve(addr.c_str());
        m_server = io::SslServer::create(
            m_reactor,
            address,
            BIND_THIS_MEMFN(onStreamAccepted),
            PROJECT_SOURCE_DIR "/utility/unittest/test.crt", PROJECT_SOURCE_DIR "/utility/unittest/test.key"
        );

    }

private:

    void onStreamAccepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
    {
        if (errorCode == 0)
        {
            auto peer = newStream->peer_address();

            newStream->enable_keepalive(2);
            m_connections[peer.u64()] = std::move(newStream);
            m_connections[peer.u64()]->enable_read([this, peerId = peer.u64()](io::ErrorCode errorCode, void* data, size_t size) -> bool
            {
                if (errorCode != 0)
                {
                    m_connections.erase(peerId);
                }
                else if (size > 0 && data)
                {
                    std::string result = "";
                    std::string strResponse = std::string(static_cast<const char*>(data), size);

                    try
                    {
                        json request = json::parse(strResponse);
                        if (request["method"] == "server.features")
                        {
#if defined(BEAM_MAINNET) || defined(SWAP_MAINNET)
                            result = R"({"jsonrpc": "2.0", "result": {"genesis_hash": "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"}, "id": "verify"})";
#else
                            result = R"({"jsonrpc": "2.0", "result": {"genesis_hash": "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"}, "id": "verify"})";
#endif
                        }
                        else if (request["method"] == "blockchain.headers.subscribe")
                        {
                            result = R"({"jsonrpc": "2.0", "result": {"hex": "00000020f067b25ee650df3118827383cc128eb00ff88ad521dd3a17c43ebeef56ce0f50d3e5df9a08d80ffff34f3b64b04eb303679290b0b908e5ae6ddd08f38aee29237ca0805dffff7f2001000000", "height": )" + std::to_string(m_blockCount++) + R"(}, "id": "test"})";
                        }
                        else if(request["method"] == "blockchain.scripthash.listunspent")
                        {
                            if (m_listUnspent.count(request["params"][0]))
                            {
                                result = m_listUnspent.at(request["params"][0]);
                            }
                            else
                            {
                                result = R"({"jsonrpc": "2.0", "result": [], "id": "teste"})";
                            }
                        }
                        else if (request["method"] == "blockchain.transaction.broadcast")
                        {
                            std::string hexTx = request["params"][0];

                            libbitcoin::data_chunk tx_data;
                            libbitcoin::decode_base16(tx_data, hexTx);
                            libbitcoin::chain::transaction tx = libbitcoin::chain::transaction::factory_from_data(tx_data);

                            std::string txId = libbitcoin::encode_hash(tx.hash());

                            if (m_transactions.find(txId) == m_transactions.end())
                            {
                                m_transactions[txId] = make_pair(hexTx, 0);
                            }
                            result = R"({"jsonrpc": "2.0", "result": ")" + txId + R"(", "id": "test"})";
                        }
                        else if (request["method"] == "blockchain.transaction.get")
                        {
                            std::string txId = request["params"][0];
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

                            auto response = json::parse(R"({"jsonrpc": "2.0", "result": {"txid": "b77ada485262ccb2615903db0c6379187646c97113e8defee215aa610d66cc01", "hash": "b77ada485262ccb2615903db0c6379187646c97113e8defee215aa610d66cc01", "version": 2, "size": 224, "vsize": 224, "weight": 896, "locktime": 0, "vin": [{"txid": "b5d4225286fec7801fca9fae2f5819a938bc6fec707e5c270935ea20a9ec94ed", "vout": 1, "scriptSig": {"asm": "3045022100f92a598ddc276a0d3270a5527dbf34adff80c2030150c9a98a588073e93cebcb02206c559fb8569aff9b6008805c43d0ba497d53825b61cc2d48eaea4107f9185072[ALL] 03656b45ecae3cfe909ce78b8ace1890ad8dde8e62e3d0aebf86e73673b57c6464", "hex": "483045022100f92a598ddc276a0d3270a5527dbf34adff80c2030150c9a98a588073e93cebcb02206c559fb8569aff9b6008805c43d0ba497d53825b61cc2d48eaea4107f9185072012103656b45ecae3cfe909ce78b8ace1890ad8dde8e62e3d0aebf86e73673b57c6464"}, "sequence": 0}], "vout": [{"value": 0.002, "n": 0, "scriptPubKey": {"asm": "OP_HASH160 ff495beff01c6a334ae47294e738a724e4155b29 OP_EQUAL", "hex": "a914ff495beff01c6a334ae47294e738a724e4155b2987", "reqSigs": 1, "type": "scripthash", "addresses": ["2NGX4BHLHv5YPBShdZyYcKiMrm5BJs6Uy4e"]}}, {"value": 9.9513022, "n": 1, "scriptPubKey": {"asm": "OP_DUP OP_HASH160 45db9fa908ff3e35ab6db85ab8b189e5da54cd7d OP_EQUALVERIFY OP_CHECKSIG", "hex": "76a91445db9fa908ff3e35ab6db85ab8b189e5da54cd7d88ac", "reqSigs": 1, "type": "pubkeyhash", "addresses": ["mmtL21a47sRdc4V1WWCXTvPBBMrWUoBWoy"]}}], "hex": "0200000001ed94eca920ea3509275c7e70ec6fbc38a919582fae9fca1f80c7fe865222d4b5010000006b483045022100f92a598ddc276a0d3270a5527dbf34adff80c2030150c9a98a588073e93cebcb02206c559fb8569aff9b6008805c43d0ba497d53825b61cc2d48eaea4107f9185072012103656b45ecae3cfe909ce78b8ace1890ad8dde8e62e3d0aebf86e73673b57c64640000000002400d03000000000017a914ff495beff01c6a334ae47294e738a724e4155b29876c7b503b000000001976a91445db9fa908ff3e35ab6db85ab8b189e5da54cd7d88ac00000000"}, "id": "test"})");

                            response["result"]["confirmations"] = confirmations;
                            response["result"]["vout"][0]["scriptPubKey"]["hex"] = lockScript;
                            result = response.dump();
                        }
                    }
                    catch (const std::exception& /*ex*/)
                    {
                        result = R"({"jsonrpc": "2.0", "error": [], "id": "teste"})";
                    }

                    m_connections[peerId]->write(result.data(), result.size());
                }
                else
                {
                }

                return false;
            });
        }
        else
        {
            LOG_ERROR() << "Server error " << io::error_str(errorCode);
            m_reactor.stop();
        }
    }

private:
    io::Reactor& m_reactor;
    io::SslServer::Ptr m_server;
    std::map<uint64_t, io::TcpStream::Ptr> m_connections;
    uint64_t m_blockCount = 100;

    const std::map<std::string, std::string> m_listUnspent = {
        {"896063c12a01098375c8a379d820562922397caff3d7f61728092c67d34d9c65", R"({"jsonrpc": "2.0", "result": [{"tx_hash": "774a3898ed92a322c718e347ea8d033a0cb8f5371bc9ed19e7002c5e3a63f917", "tx_pos": 0, "height": 4336, "value": 167600}], "id": "test"})"},
        {"5314877c15accc80d74a09026b1f9b8c0b745c1694276605c40a894e43e55f97", R"({"jsonrpc": "2.0", "result": [{"tx_hash": "b5d4225286fec7801fca9fae2f5819a938bc6fec707e5c270935ea20a9ec94ed", "tx_pos": 1, "height": 78716, "value": 995359500}], "id": "teste"})"}
    };

    std::map<std::string, std::pair<std::string, int>> m_transactions;
};