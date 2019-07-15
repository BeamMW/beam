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
#include <boost/algorithm/string/trim.hpp>
#include <map>

#include "utility/cli/options.h"
#include "utility/helpers.h"
#include "utility/io/timer.h"
#include "utility/io/tcpserver.h"
#include "utility/io/sslserver.h"
#include "utility/io/json_serializer.h"
#include "utility/string_helpers.h"
#include "utility/log_rotation.h"

#include "http/http_connection.h"
#include "http/http_msg_creator.h"

#include "p2p/line_protocol.h"

#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"
#include "wallet/bitcoin/options.h"
#include "wallet/litecoin/options.h"
#include "wallet/qtum/options.h"

#include "nlohmann/json.hpp"
#include "version.h"

using json = nlohmann::json;

static const unsigned LOG_ROTATION_PERIOD = 3 * 60 * 60 * 1000; // 3 hours
static const size_t PACKER_FRAGMENTS_SIZE = 4096;

using namespace beam;
using namespace beam::wallet;

namespace
{
    const char* MinimumFeeError = "Failed to initiate the send operation. The minimum fee is 100 GROTH.";

    struct TlsOptions
    {
        bool use;
        std::string certPath;
        std::string keyPath;
    };

    WalletApi::ACL loadACL(const std::string& path)
    {
        std::ifstream file(path);
        std::string line;
        WalletApi::ACL::value_type keys;
        int curLine = 1;

        while (std::getline(file, line)) 
        {
            boost::algorithm::trim(line);

            auto key = string_helpers::split(line, ':');
            bool parsed = false;

            static const char* READ_ACCESS = "read";
            static const char* WRITE_ACCESS = "write";

            if (key.size() == 2)
            {
                boost::algorithm::trim(key[0]);
                boost::algorithm::trim(key[1]);

                parsed = !key[0].empty() && (key[1] == READ_ACCESS || key[1] == WRITE_ACCESS);
            }

            if (!parsed)
            {
                LOG_ERROR() << "ACL parsing error, line " << curLine;
                return boost::none;
            }

            keys.insert({ key[0], key[1] == WRITE_ACCESS });
            curLine++;
        }

        if (keys.empty())
        {
            LOG_WARNING() << "ACL file is empty";
        }
        else
        {
            LOG_INFO() << "ACL file successfully loaded";
        }

        return WalletApi::ACL(keys);
    }

    class IWalletApiServer
    {
    public:
        virtual void closeConnection(uint64_t id) = 0;
    };

    class WalletApiServer : public IWalletApiServer
    {
    public:
        WalletApiServer(IWalletDB::Ptr walletDB, Wallet& wallet, IWalletMessageEndpoint& wnet, io::Reactor& reactor, 
            io::Address listenTo, bool useHttp, WalletApi::ACL acl, const TlsOptions& tlsOptions, const std::vector<uint32_t>& whitelist)
            : _reactor(reactor)
            , _bindAddress(listenTo)
            , _useHttp(useHttp)
            , _tlsOptions(tlsOptions)
            , _walletDB(walletDB)
            , _wallet(wallet)
            , _wnet(wnet)
            , _acl(acl)
            , _whitelist(whitelist)
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
                _server = _tlsOptions.use
                    ? io::SslServer::create(_reactor, _bindAddress, BIND_THIS_MEMFN(on_stream_accepted), _tlsOptions.certPath.c_str(), _tlsOptions.keyPath.c_str())
                    : io::TcpServer::create(_reactor, _bindAddress, BIND_THIS_MEMFN(on_stream_accepted));

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
            return std::static_pointer_cast<ApiConnection>(std::make_shared<T>(*this, _walletDB, _wallet, _wnet, std::move(newStream), _acl));
        }

        void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
        {
            if (errorCode == 0) 
            {          
                auto peer = newStream->peer_address();

                if (!_whitelist.empty())
                {
                    if (std::find(_whitelist.begin(), _whitelist.end(), peer.ip()) == _whitelist.end())
                    {
                        LOG_WARNING() << peer.str() << " not in IP whitelist, closing";
                        return;
                    }
                }

                LOG_DEBUG() << "+peer " << peer;

                checkConnections();

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
            ApiConnection(IWalletDB::Ptr walletDB, Wallet& wallet, IWalletMessageEndpoint& wnet, WalletApi::ACL acl)
                : _walletDB(walletDB)
                , _wallet(wallet)
                , _api(*this, acl)
                , _wnet(wnet)
            {
                _walletDB->subscribe(this);
            }

            virtual ~ApiConnection()
            {
                _walletDB->unsubscribe(this);
            }

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

            void FillAddressData(const AddressData& data, WalletAddress& address)
            {
                if (data.comment)
                {
                    address.setLabel(*data.comment);
                }

                if (data.expiration)
                {
                    switch (*data.expiration)
                    {
                    case EditAddress::OneDay:
                        address.makeActive(24 * 60 * 60);
                        break;
                    case EditAddress::Expired:
                        address.makeExpired();
                        break;
                    case EditAddress::Never:
                        address.makeEternal();
                        break;
                    }
                }
            }

            void onMessage(int id, const CreateAddress& data) override 
            {
                LOG_DEBUG() << "CreateAddress(id = " << id << ")";

                WalletAddress address = storage::createAddress(*_walletDB);
                FillAddressData(data, address);

                _walletDB->saveAddress(address);

                doResponse(id, CreateAddress::Response{ address.m_walletID });
            }

            void onMessage(int id, const DeleteAddress& data) override
            {
                LOG_DEBUG() << "DeleteAddress(id = " << id << " address = " << std::to_string(data.address) << ")";

                auto addr = _walletDB->getAddress(data.address);

                if (addr)
                {
                    _walletDB->deleteAddress(data.address);

                    doResponse(id, DeleteAddress::Response{});
                }
                else
                {
                    doError(id, INVALID_ADDRESS, "Provided address doesn't exist.");
                }
            }

            void onMessage(int id, const EditAddress& data) override
            {
                LOG_DEBUG() << "EditAddress(id = " << id << " address = " << std::to_string(data.address) << ")";

                auto addr = _walletDB->getAddress(data.address);

                if (addr)
                {
                    if (addr->m_OwnID)
                    {
                        FillAddressData(data, *addr);
                        _walletDB->saveAddress(*addr);

                        doResponse(id, EditAddress::Response{});
                    }
                    else
                    {
                        doError(id, INVALID_ADDRESS, "You can edit only own address.");
                    }
                }
                else
                {
                    doError(id, INVALID_ADDRESS, "Provided address doesn't exist.");
                }
            }

            void onMessage(int id, const AddrList& data) override
            {
                LOG_DEBUG() << "AddrList(id = " << id << ")";

                doResponse(id, AddrList::Response{ _walletDB->getAddresses(data.own) });
            }

            void onMessage(int id, const ValidateAddress& data) override
            {
                LOG_DEBUG() << "ValidateAddress( address = " << std::to_string(data.address) << ")";

                auto addr = _walletDB->getAddress(data.address);
                bool isMine = addr ? addr->m_OwnID != 0 : false;
                doResponse(id, ValidateAddress::Response{ data.address.IsValid() && (isMine ? !addr->isExpired() : true), isMine});
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
                        WalletAddress senderAddress = storage::createAddress(*_walletDB);
                        _walletDB->saveAddress(senderAddress);

                        from = senderAddress.m_walletID;     
                    }

                    ByteBuffer message(data.comment.begin(), data.comment.end());

                    CoinIDList coins;

                    if (data.session)
                    {
                        coins = _walletDB->getLocked(*data.session);

                        if (coins.empty())
                        {
                            doError(id, INTERNAL_JSON_RPC_ERROR, "Requested session is empty.");
                            return;
                        }
                    }
                    else
                    {
                        coins = data.coins ? *data.coins : CoinIDList();
                    }

                    if (data.fee < MinimumFee)
                    {
                        doError(id, INTERNAL_JSON_RPC_ERROR, MinimumFeeError);
                        return;
                    }

                    auto txId = _wallet.transfer_money(from, data.address, data.value, data.fee, coins, true, kDefaultTxLifetime, kDefaultTxResponseTime, std::move(message), true);

                    doResponse(id, Send::Response{ txId });
                }
                catch(...)
                {
                    doError(id, INTERNAL_JSON_RPC_ERROR, "Transaction could not be created. Please look at logs.");
                }
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

                    storage::getTxParameter(*_walletDB, tx->m_txId, TxParameterID::KernelProofHeight, result.kernelProofHeight);

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
                     WalletAddress senderAddress = storage::createAddress(*_walletDB);
                    _walletDB->saveAddress(senderAddress);

                    if (data.fee < MinimumFee)
                    {
                        doError(id, INTERNAL_JSON_RPC_ERROR, MinimumFeeError);
                        return;
                    }

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

            void onMessage(int id, const TxDelete& data) override
            {
                LOG_DEBUG() << "TxDelete(txId = " << to_hex(data.txId.data(), data.txId.size()) << ")";

                auto tx = _walletDB->getTx(data.txId);

                if (tx)
                {
                    if (tx->canDelete())
                    {
                        _walletDB->deleteTx(data.txId);

                        if (_walletDB->getTx(data.txId))
                        {
                            doError(id, INTERNAL_JSON_RPC_ERROR, "Transaction not deleted.");
                        }
                        else
                        {
                            doResponse(id, TxDelete::Response{true});
                        }
                    }
                    else
                    {
                        doError(id, INTERNAL_JSON_RPC_ERROR, "Transaction can't be deleted.");
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

                storage::Totals totals(*_walletDB);

                response.available = totals.Avail;
                response.receiving = totals.Incoming;
                response.sending = totals.Outgoing;
                response.maturing = totals.Maturing;

                doResponse(id, response);
            }

            void onMessage(int id, const Lock& data) override
            {
                LOG_DEBUG() << "Lock(id = " << id << ")";

                Lock::Response response;

                response.result = _walletDB->lock(data.coins, data.session);

                doResponse(id, response);
            }

            void onMessage(int id, const Unlock& data) override
            {
                LOG_DEBUG() << "Unlock(id = " << id << " session = " << data.session << ")";

                Unlock::Response response;

                response.result = _walletDB->unlock(data.session);

                doResponse(id, response);
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

                        storage::getTxParameter(*_walletDB, tx.m_txId, TxParameterID::KernelProofHeight, item.kernelProofHeight);
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
            IWalletMessageEndpoint& _wnet;
        };

        class TcpApiConnection : public ApiConnection
        {
        public:
            TcpApiConnection(IWalletApiServer& server, IWalletDB::Ptr walletDB, Wallet& wallet, IWalletMessageEndpoint& wnet, io::TcpStream::Ptr&& newStream, WalletApi::ACL acl)
                : ApiConnection(walletDB, wallet, wnet, acl)
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
                LOG_INFO() << "got " << std::string((char*)data, size);

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
            HttpApiConnection(IWalletApiServer& server, IWalletDB::Ptr walletDB, Wallet& wallet, IWalletMessageEndpoint& wnet, io::TcpStream::Ptr&& newStream, WalletApi::ACL acl)
                : ApiConnection(walletDB, wallet, wnet, acl)
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
        TlsOptions _tlsOptions;

        std::map<uint64_t, std::shared_ptr<ApiConnection>> _connections;

        IWalletDB::Ptr _walletDB;
        Wallet& _wallet;
        IWalletMessageEndpoint& _wnet;
        std::vector<uint64_t> _pendingToClose;
        WalletApi::ACL _acl;
        std::vector<uint32_t> _whitelist;
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
            Nonnegative<uint32_t> pollPeriod_ms;

            bool useAcl;
            std::string aclPath;
            std::string whitelist;

            uint32_t logCleanupPeriod;

        } options;

        TlsOptions tlsOptions;

        io::Address node_addr;
        IWalletDB::Ptr walletDB;
        io::Reactor::Ptr reactor = io::Reactor::create();
        WalletApi::ACL acl;
        std::vector<uint32_t> whitelist;

        {
            po::options_description desc("Wallet API general options");
            desc.add_options()
                (cli::HELP_FULL, "list of all options")
                (cli::PORT_FULL, po::value(&options.port)->default_value(10000), "port to start server on")
                (cli::NODE_ADDR_FULL, po::value<std::string>(&options.nodeURI), "address of node")
                (cli::WALLET_STORAGE, po::value<std::string>(&options.walletPath)->default_value("wallet.db"), "path to wallet file")
                (cli::PASS, po::value<std::string>(), "password for the wallet")
                (cli::API_USE_HTTP, po::value<bool>(&options.useHttp)->default_value(false), "use JSON RPC over HTTP")
                (cli::IP_WHITELIST, po::value<std::string>(&options.whitelist)->default_value(""), "IP whitelist")
                (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>(&options.logCleanupPeriod)->default_value(5), "old logfiles cleanup period(days)")
                (cli::NODE_POLL_PERIOD, po::value<Nonnegative<uint32_t>>(&options.pollPeriod_ms)->default_value(Nonnegative<uint32_t>(0)), "Node poll period in milliseconds. Set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks if it is less then it will be rounded up to block rate value.")
            ;

            po::options_description authDesc("User authorization options");
            authDesc.add_options()
                (cli::API_USE_ACL, po::value<bool>(&options.useAcl)->default_value(false), "use Access Control List (ACL)")
                (cli::API_ACL_PATH, po::value<std::string>(&options.aclPath)->default_value("wallet_api.acl"), "path to ACL file")
            ;

            po::options_description tlsDesc("TLS protocol options");
            tlsDesc.add_options()
                (cli::API_USE_TLS, po::value<bool>(&tlsOptions.use)->default_value(false), "use TLS protocol")
                (cli::API_TLS_CERT, po::value<std::string>(&tlsOptions.certPath)->default_value("wallet_api.crt"), "path to TLS certificate")
                (cli::API_TLS_KEY, po::value<std::string>(&tlsOptions.keyPath)->default_value("wallet_api.key"), "path to TLS private key")
            ;

            desc.add(authDesc);
            desc.add(tlsDesc);
            desc.add(createRulesOptionsDescription());

            po::variables_map vm;

            po::store(po::command_line_parser(argc, argv)
                .options(desc)
                .style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing)
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

            getRulesOptions(vm);

            Rules::get().UpdateChecksum();
            LOG_INFO() << "Beam Wallet API " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
            LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();
            
            if (options.useAcl)
            {
                if (!(boost::filesystem::exists(options.aclPath) && (acl = loadACL(options.aclPath))))
                {
                    LOG_ERROR() << "ACL file not loaded, path is: " << options.aclPath;
                    return -1;
                }
            }

            if (tlsOptions.use)
            {
                if (tlsOptions.certPath.empty() || !boost::filesystem::exists(tlsOptions.certPath))
                {
                    LOG_ERROR() << "TLS certificate not found, path is: " << tlsOptions.certPath;
                    return -1;
                }

                if (tlsOptions.keyPath.empty() || !boost::filesystem::exists(tlsOptions.keyPath))
                {
                    LOG_ERROR() << "TLS private key not found, path is: " << tlsOptions.keyPath;
                    return -1;
                }
            }

            if (!options.whitelist.empty())
            {
                const auto& items = string_helpers::split(options.whitelist, ',');

                for (const auto& item : items)
                {
                    io::Address addr;

                    if (addr.resolve(item.c_str()))
                    {
                        whitelist.push_back(addr.ip());
                    }
                    else
                    {
                        LOG_ERROR() << "IP address not added to whitelist: " << item;
                        return -1;
                    }
                }
            }

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

            walletDB = WalletDB::open(options.walletPath, pass, reactor);
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

        LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD, options.logCleanupPeriod);

        Wallet wallet{ walletDB };

        auto nnet = std::make_shared<proto::FlyClient::NetworkStd>(wallet);
        nnet->m_Cfg.m_PollPeriod_ms = options.pollPeriod_ms.value;
        
        if (nnet->m_Cfg.m_PollPeriod_ms)
        {
            LOG_INFO() << "Node poll period = " << nnet->m_Cfg.m_PollPeriod_ms << " ms";
            uint32_t timeout_ms = std::max(Rules::get().DA.Target_s * 1000, nnet->m_Cfg.m_PollPeriod_ms);
            if (timeout_ms != nnet->m_Cfg.m_PollPeriod_ms)
            {
                LOG_INFO() << "Node poll period has been automatically rounded up to block rate: " << timeout_ms << " ms";
            }
        }
        uint32_t responceTime_s = Rules::get().DA.Target_s * wallet::kDefaultTxResponseTime;
        if (nnet->m_Cfg.m_PollPeriod_ms >= responceTime_s * 1000)
        {
            LOG_WARNING() << "The \"--node_poll_period\" parameter set to more than " << uint32_t(responceTime_s / 3600) << " hours may cause transaction problems.";
        }
        nnet->m_Cfg.m_vNodes.push_back(node_addr);
        nnet->Connect();

        auto wnet = std::make_shared<WalletNetworkViaBbs>(wallet, nnet, walletDB);
		wallet.AddMessageEndpoint(wnet);
        wallet.SetNodeEndpoint(nnet);

        WalletApiServer server(walletDB, wallet, *wnet, *reactor, 
            listenTo, options.useHttp, acl, tlsOptions, whitelist);

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
