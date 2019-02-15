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

#define LOG_VERBOSE_ENABLED 1
#include "utility/logger.h"

#include "api.h"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <map>

#include "utility/helpers.h"
#include "utility/io/timer.h"
#include "utility/io/tcpserver.h"
#include "utility/io/json_serializer.h"
#include "utility/io/coarsetimer.h"
#include "utility/options.h"

#include "http/http_connection.h"
#include "http/http_msg_creator.h"

#include "p2p/line_protocol.h"

#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"

#include "nlohmann/json.hpp"
#include "version.h"

using json = nlohmann::json;

static const unsigned LOG_ROTATION_PERIOD = 3 * 60 * 60 * 1000; // 3 hours
static const size_t PACKER_FRAGMENTS_SIZE = 4096;
static const uint64_t CLOSE_CONNECTION_TIMER = 1;

namespace beam
{
    class IWalletApiServer
    {
    public:
        virtual void closeConnection(uint64_t id) = 0;
    };

    class WalletApiServer : public IWalletApiServer
    {
    public:
        WalletApiServer(IWalletDB::Ptr walletDB, Wallet& wallet, WalletNetworkViaBbs& wnet, io::Reactor& reactor, io::Address listenTo, bool useHttp)
            : _reactor(reactor)
            , _bindAddress(listenTo)
            , _useHttp(useHttp)
            , _walletDB(walletDB)
            , _wallet(wallet)
            , _wnet(wnet)
            , _timers(_reactor, 100)
        {
            start();
        }

        ~WalletApiServer()
        {
            stop();
        }

    protected:

        void start()
        {
            LOG_INFO() << "Start server on " << _bindAddress;

            try
            {
                _server = io::TcpServer::create(
                    _reactor,
                    _bindAddress,
                    BIND_THIS_MEMFN(on_stream_accepted)
                );
            }
            catch (const std::exception& e)
            {
                LOG_ERROR() << "cannot start server: " << e.what();
            }
        }

        void stop()
        {

        }

        void closeConnection(uint64_t id) override
        {
            _pendingToClose.push_back(id);

            _timers.set_timer(1, 0, BIND_THIS_MEMFN(checkConnections));
        }

    private:

        void checkConnections()
        {
            // clean closed connections
            {
                for (auto id : _pendingToClose)
                {
                    _connections.erase(id);
                }

                _pendingToClose.clear();
            }
        }

        class ApiConnection;

        template<typename T>
        std::shared_ptr<ApiConnection> createConnection(io::TcpStream::Ptr&& newStream)
        {
            return std::static_pointer_cast<ApiConnection>(std::make_shared<T>(*this, _walletDB, _wallet, _wnet, std::move(newStream)));
        }

        void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
        {
            if (errorCode == 0) 
            {          
                auto peer = newStream->peer_address();
                LOG_DEBUG() << "+peer " << peer;

                _connections[peer.u64()] = _useHttp
                    ? createConnection<HttpApiConnection>(std::move(newStream))
                    : createConnection<TcpApiConnection>(std::move(newStream));
            }

            LOG_DEBUG() << "on_stream_accepted";
        }

    private:
        class ApiConnection : IWalletApiHandler, IWalletDbObserver
        {
        public:
            ApiConnection(IWalletDB::Ptr walletDB, Wallet& wallet, WalletNetworkViaBbs& wnet)
                : _walletDB(walletDB)
                , _wallet(wallet)
                , _api(*this)
                , _wnet(wnet)
            {
                _walletDB->subscribe(this);
            }

            virtual ~ApiConnection()
            {
                _walletDB->unsubscribe(this);
            }

            void onCoinsChanged() override {}
            void onTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items) override {}

            void onSystemStateChanged() override 
            {
                
            }

            void onAddressChanged() override {}

            virtual void serializeMsg(const json& msg) = 0;

            template<typename T>
            void doResponse(int id, const T& response)
            {
                json msg;
                _api.getResponse(id, response, msg);
                serializeMsg(msg);
            }

            void doError(int id, int code, const std::string& info)
            {
                json msg
                {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"error",
                        {
                            {"code", code},
                            {"message", info},
                        }
                    }
                };

                serializeMsg(msg);
            }

            void onInvalidJsonRpc(const json& msg) override
            {
                LOG_DEBUG() << "onInvalidJsonRpc: " << msg;

                serializeMsg(msg);
            }

            void onMessage(int id, const CreateAddress& data) override 
            {
                LOG_DEBUG() << "CreateAddress(id = " << id << " metadata = " << data.metadata << " lifetime = " << data.lifetime << ")";

                WalletAddress address = wallet::createAddress(*_walletDB);
                address.m_duration = data.lifetime * 60 * 60;

                _walletDB->saveAddress(address);

                _wnet.AddOwnAddress(address);

                doResponse(id, CreateAddress::Response{ address.m_walletID });
            }

            void onMessage(int id, const ValidateAddress& data) override
            {
                LOG_DEBUG() << "ValidateAddress( address = " << std::to_string(data.address) << ")";

                auto addr = _walletDB->getAddress(data.address);
                bool isMine = addr ? addr->m_OwnID != 0 : false;
                doResponse(id, ValidateAddress::Response{ data.address.IsValid() && (isMine ? addr->isExpired() : true), isMine});
            }

            void onMessage(int id, const Send& data) override
            {
                LOG_DEBUG() << "Send(id = " << id << " amount = " << data.value << " fee = " << data.fee <<  " address = " << std::to_string(data.address) << ")";

                try
                {
                    WalletID from(Zero);

                    if(data.from)
                    {
                        if(!data.from->IsValid())
                        {
                            doError(id, INTERNAL_JSON_RPC_ERROR, "Invalid sender address.");
                            return;
                        }

                        auto addr = _walletDB->getAddress(*data.from);
                        bool isMine = addr ? addr->m_OwnID != 0 : false;

                        if(!isMine)
                        {
                            doError(id, INTERNAL_JSON_RPC_ERROR, "It's not your own address.");
                            return;
                        }

                        from = *data.from;
                    }
                    else
                    {
                        WalletAddress senderAddress = wallet::createAddress(*_walletDB);
                        _walletDB->saveAddress(senderAddress);

                        _wnet.AddOwnAddress(senderAddress);

                        from = senderAddress.m_walletID;     
                    }

                    ByteBuffer message(data.comment.begin(), data.comment.end());

                    auto txId = _wallet.transfer_money(from, data.address, data.value, data.fee, true, 120, std::move(message));
                    doResponse(id, Send::Response{ txId });
                }
                catch(...)
                {
                    doError(id, INTERNAL_JSON_RPC_ERROR, "Transaction could not be created. Please look at logs.");
                }
            }

            void onMessage(int id, const Replace& data) override
            {
                methodNotImplementedYet(id);
            }

            void onMessage(int id, const Status& data) override
            {
                LOG_DEBUG() << "Status(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

                auto tx = _walletDB->getTx(data.txId);

                if (tx)
                {
                    Block::SystemState::ID stateID = {};
                    _walletDB->getSystemStateID(stateID);

                    Status::Response result;
                    result.tx = *tx;
                    result.kernelProofHeight = 0;
                    result.systemHeight = stateID.m_Height;
                    result.confirmations = 0;

                    wallet::getTxParameter(*_walletDB, tx->m_txId, wallet::TxParameterID::KernelProofHeight, result.kernelProofHeight);

                    doResponse(id, result);
                }
                else
                {
                    doError(id, INVALID_PARAMS_JSON_RPC, "Unknown transaction ID.");
                }
            }

            void onMessage(int id, const Split& data) override
            {
                LOG_DEBUG() << "Split(id = " << id << " coins = [";
                for (auto& coin : data.coins) LOG_DEBUG() << coin << ",";
                LOG_DEBUG() << "], fee = " << data.fee;
                try
                {
                     WalletAddress senderAddress = wallet::createAddress(*_walletDB);
                    _walletDB->saveAddress(senderAddress);
                    _wnet.AddOwnAddress(senderAddress);

                    auto txId = _wallet.split_coins(senderAddress.m_walletID, data.coins, data.fee);
                    doResponse(id, Send::Response{ txId });
                }
                catch(...)
                {
                    doError(id, INTERNAL_JSON_RPC_ERROR, "Transaction could not be created. Please look at logs.");
                }
            }

            void onMessage(int id, const TxCancel& data) override
            {
                LOG_DEBUG() << "TxCancel(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

                auto tx = _walletDB->getTx(data.txId);

                if (tx)
                {
                    if (tx->canCancel())
                    {
                        _wallet.cancel_tx(tx->m_txId);
                        TxCancel::Response result{ true };
                        doResponse(id, result);
                    }
                    else
                    {
                        doError(id, INVALID_TX_STATUS, "Transaction could not be cancelled. Invalid transaction status.");
                    }
                }
                else
                {
                    doError(id, INVALID_PARAMS_JSON_RPC, "Unknown transaction ID.");
                }
            }

            template<typename T>
            static void doPagination(size_t skip, size_t count, std::vector<T>& res)
            {
                if (count > 0)
                {
                    size_t start = skip;
                    size_t end = start + count;
                    size_t size = res.size();

                    if (start < size)
                    {
                        if (end > size) end = size;

                        res = std::vector<T>(res.begin() + start, res.begin() + end);
                    }
                    else res = {};
                }
            }

            void onMessage(int id, const GetUtxo& data) override 
            {
                LOG_DEBUG() << "GetUtxo(id = " << id << ")";

                GetUtxo::Response response;
                _walletDB->visit([&response](const Coin& c)->bool
                {
                    response.utxos.push_back(c);
                    return true;
                });

                doPagination(data.skip, data.count, response.utxos);

                doResponse(id, response);
            }

            void onMessage(int id, const WalletStatus& data) override
            {
                LOG_DEBUG() << "WalletStatus(id = " << id << ")";

                WalletStatus::Response response;

                {
                    Block::SystemState::ID stateID = {};
                    _walletDB->getSystemStateID(stateID);

                    response.currentHeight = stateID.m_Height;
                    response.currentStateHash = stateID.m_Hash;
                }

                {
                    Block::SystemState::Full state;
                    _walletDB->get_History().get_Tip(state);
                    response.prevStateHash = state.m_Prev;
                    response.difficulty = state.m_PoW.m_Difficulty.ToFloat();
                }

				wallet::Totals totals(*_walletDB);

                response.available = totals.Avail;
                response.receiving = totals.Incoming;
                response.sending = totals.Outgoing;
                response.maturing = totals.Maturing;

                response.locked = 0; // same as Outgoing?

                doResponse(id, response);
            }

            void onMessage(int id, const Lock& data) override
            {
                methodNotImplementedYet(id);
            }

            void onMessage(int id, const Unlock& data) override
            {
                methodNotImplementedYet(id);
            }

            void onMessage(int id, const TxList& data) override
            {
                LOG_DEBUG() << "List(filter.status = " << (data.filter.status ? std::to_string((uint32_t)*data.filter.status) : "nul") << ")";

                TxList::Response res;

                {
                    auto txList = _walletDB->getTxHistory();

                    Block::SystemState::ID stateID = {};
                    _walletDB->getSystemStateID(stateID);

                    for (const auto& tx : txList)
                    {
                        Status::Response item;
                        item.tx = tx;
                        item.kernelProofHeight = 0;
                        item.systemHeight = stateID.m_Height;
                        item.confirmations = 0;

                        wallet::getTxParameter(*_walletDB, tx.m_txId, wallet::TxParameterID::KernelProofHeight, item.kernelProofHeight);
                        res.resultList.push_back(item);
                    }
                }

                using Result = decltype(res.resultList);

                // filter transactions by status if provided
                if (data.filter.status)
                {
                    Result filteredList;

                    for (const auto& it : res.resultList)
                        if (it.tx.m_status == *data.filter.status)
                            filteredList.push_back(it);

                    res.resultList = filteredList;
                }

                // filter transactions by height if provided
                if (data.filter.height)
                {
                    Result filteredList;

                    for (const auto& it : res.resultList)
                        if (it.kernelProofHeight == *data.filter.height)
                            filteredList.push_back(it);

                    res.resultList = filteredList;
                }

                doPagination(data.skip, data.count, res.resultList);

                doResponse(id, res);
            }
        private:
            void methodNotImplementedYet(int id)
            {
                doError(id, NOTFOUND_JSON_RPC, "Method not implemented yet.");
            }

        protected:
            IWalletDB::Ptr _walletDB;
            Wallet& _wallet;
            WalletApi _api;
            WalletNetworkViaBbs& _wnet;
        };

        class TcpApiConnection : public ApiConnection
        {
        public:
            TcpApiConnection(IWalletApiServer& server, IWalletDB::Ptr walletDB, Wallet& wallet, WalletNetworkViaBbs& wnet, io::TcpStream::Ptr&& newStream)
                : ApiConnection(walletDB, wallet, wnet)
                , _stream(std::move(newStream))
                , _lineProtocol(BIND_THIS_MEMFN(on_raw_message), BIND_THIS_MEMFN(on_write))
                , _server(server)
            {
                _stream->enable_keepalive(2);
                _stream->enable_read(BIND_THIS_MEMFN(on_stream_data));
            }

            virtual ~TcpApiConnection()
            {

            }

            void serializeMsg(const json& msg) override
            {
                serialize_json_msg(_lineProtocol, msg);
            }

            void on_write(io::SharedBuffer&& msg)
            {
                _stream->write(msg);
            }

            bool on_raw_message(void* data, size_t size)
            {
                const char* const_data = static_cast<const char*>(data);
                std::string str_data(const_data);
              //  std::string((char*)data,size);
                LOG_INFO() << "got " << str_data;
                try
                {

                    json json_data = json::parse(str_data);
                    if(!json_data.is_object())
                    {
                        return true;
                    }
                }
                catch(...)
                {
                    return true;
                }

                return _api.parse(static_cast<const char*>(data), size);
            }

            bool on_stream_data(io::ErrorCode errorCode, void* data, size_t size)
            {
                if (errorCode != 0)
                {
                    LOG_INFO() << "peer disconnected, code=" << io::error_str(errorCode);
                    _server.closeConnection(_stream->peer_address().u64());
                    return false;
                }

                if (!_lineProtocol.new_data_from_stream(data, size))
                {
                    LOG_INFO() << "stream corrupted";
                    _server.closeConnection(_stream->peer_address().u64());
                    return false;
                }
                _server.closeConnection(_stream->peer_address().u64());

                return true;
            }

        private:
            io::TcpStream::Ptr _stream;
            LineProtocol _lineProtocol;
            IWalletApiServer& _server;
        };

        class HttpApiConnection : public ApiConnection
        {
        public:
            HttpApiConnection(IWalletApiServer& server, IWalletDB::Ptr walletDB, Wallet& wallet, WalletNetworkViaBbs& wnet, io::TcpStream::Ptr&& newStream)
                : ApiConnection(walletDB, wallet, wnet)
                , _keepalive(false)
                , _msgCreator(2000)
                , _packer(PACKER_FRAGMENTS_SIZE)
                , _server(server)
            {
                newStream->enable_keepalive(1);
                auto peer = newStream->peer_address();

                _connection = std::make_unique<HttpConnection>(
                    peer.u64(),
                    BaseConnection::inbound,
                    BIND_THIS_MEMFN(on_request),
                    10000,
                    1024,
                    std::move(newStream)
                    );
            }

            virtual ~HttpApiConnection() {}

            void serializeMsg(const json& msg) override
            {
                serialize_json_msg(_body, _packer, msg);                
                _keepalive = send(_connection, 200, "OK");
            }

        private:

            bool on_request(uint64_t id, const HttpMsgReader::Message& msg)
            {
                if (msg.what != HttpMsgReader::http_message || !msg.msg)
                {
                    LOG_DEBUG() << "-peer " << io::Address::from_u64(id) << " : " << msg.error_str();
                    _connection->shutdown();
                    _server.closeConnection(id);
                    return false;
                }

                if (msg.msg->get_path() != "/api/wallet")
                {
                    _keepalive = send(_connection, 404, "Not Found");
                }
                else
                {
                    _body.clear();

                    size_t size = 0;
                    auto data = msg.msg->get_body(size);

                    LOG_INFO() << "got " << std::string((char*)data, size);

                    _api.parse((char*)data, size);
                }

                if (!_keepalive)
                {
                    _connection->shutdown();
                    _server.closeConnection(id);
                }

                return _keepalive;
            }

            bool send(const HttpConnection::Ptr& conn, int code, const char* message)
            {
                assert(conn);

                size_t bodySize = 0;
                for (const auto& f : _body) { bodySize += f.size; }

                bool ok = _msgCreator.create_response(
                    _headers,
                    code,
                    message,
                    0,
                    0,
                    1,
                    "application/json",
                    bodySize
                );

                if (ok) {
                    auto result = conn->write_msg(_headers);
                    if (result && bodySize > 0) {
                        result = conn->write_msg(_body);
                    }
                    if (!result) ok = false;
                }
                else {
                    LOG_ERROR() << "cannot create response";
                }

                _headers.clear();
                _body.clear();
                return (ok && code == 200);
            }

            HttpConnection::Ptr _connection;
            bool _keepalive;

            HttpMsgCreator _msgCreator;
            HttpMsgCreator _packer;
            io::SerializedMsg _headers;
            io::SerializedMsg _body;
            IWalletApiServer& _server;
        };

        io::Reactor& _reactor;
        io::TcpServer::Ptr _server;
        io::Address _bindAddress;
        bool _useHttp;

        std::map<uint64_t, std::shared_ptr<ApiConnection>> _connections;

        IWalletDB::Ptr _walletDB;
        Wallet& _wallet;
        WalletNetworkViaBbs& _wnet;
        io::MultipleTimers _timers;
        std::vector<uint64_t> _pendingToClose;
    };
}

int main(int argc, char* argv[])
{
    using namespace beam;
    namespace po = boost::program_options;

    const auto path = boost::filesystem::system_complete("./logs");
    auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, "api_", path.string());

    try
    {
        struct
        {
            uint16_t port;
            std::string walletPath;
            std::string nodeURI;
            bool useHttp;
        } options;

        io::Address node_addr;
        IWalletDB::Ptr walletDB;
        io::Reactor::Ptr reactor = io::Reactor::create();

        {
            po::options_description desc("Wallet API options");
            desc.add_options()
                (cli::HELP_FULL, "list of all options")
                (cli::PORT_FULL, po::value(&options.port)->default_value(10000), "port to start server on")
                (cli::NODE_ADDR_FULL, po::value<std::string>(&options.nodeURI), "address of node")
                (cli::WALLET_STORAGE, po::value<std::string>(&options.walletPath)->default_value("wallet.db"), "path to wallet file")
                (cli::PASS, po::value<std::string>(), "password for the wallet")
                (cli::API_USE_HTTP, po::value<bool>(&options.useHttp)->default_value(false), "use JSON RPC over HTTP")
            ;

            po::variables_map vm;

            po::store(po::command_line_parser(argc, argv)
                .options(desc)
                .run(), vm);

            if (vm.count(cli::HELP))
            {
                std::cout << desc << std::endl;
                return 0;
            }

            {
                std::ifstream cfg("wallet-api.cfg");

                if (cfg)
                {                    
                    po::store(po::parse_config_file(cfg, desc), vm);
                }
            }

            vm.notify();

            Rules::get().UpdateChecksum();
            LOG_INFO() << "Beam Wallet API " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
            LOG_INFO() << "Rules signature: " << Rules::get().Checksum;

            if (vm.count(cli::NODE_ADDR) == 0)
            {
                LOG_ERROR() << "node address should be specified";
                return -1;
            }

            if (!node_addr.resolve(options.nodeURI.c_str()))
            {
                LOG_ERROR() << "unable to resolve node address: " << options.nodeURI;
                return -1;
            }

            if (!WalletDB::isInitialized(options.walletPath))
            {
                LOG_ERROR() << "Wallet not found, path is: " << options.walletPath;
                return -1;
            }

            SecString pass;
            if (!beam::read_wallet_pass(pass, vm))
            {
                LOG_ERROR() << "Please, provide password for the wallet.";
                return -1;
            }

            walletDB = WalletDB::open(options.walletPath, pass);
            if (!walletDB)
            {
                LOG_ERROR() << "Wallet not opened.";
                return -1;
            }

            LOG_INFO() << "wallet sucessfully opened...";
        }

        io::Address listenTo = io::Address().port(options.port);
        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);

        io::Timer::Ptr logRotateTimer = io::Timer::create(*reactor);
        logRotateTimer->start(LOG_ROTATION_PERIOD, true, []() 
            {
                Logger::get()->rotate();
            });

        Wallet wallet{ walletDB };

        proto::FlyClient::NetworkStd nnet(wallet);
        nnet.m_Cfg.m_vNodes.push_back(node_addr);
        nnet.Connect();

        WalletNetworkViaBbs wnet(wallet, nnet, walletDB);

        wallet.set_Network(nnet, wnet);

        WalletApiServer server(walletDB, wallet, wnet, *reactor, listenTo, options.useHttp);

        io::Reactor::get_Current().run();

        LOG_INFO() << "Done";
    }
    catch (const std::exception& e)
    {
        LOG_ERROR() << "EXCEPTION: " << e.what();
    }
    catch (...)
    {
        LOG_ERROR() << "NON_STD EXCEPTION";
    }

    return 0;
}
