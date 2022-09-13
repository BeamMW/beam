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

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <map>

#ifndef LOG_VERBOSE_ENABLED
#define LOG_VERBOSE_ENABLED 1
#endif
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "wallet/api/i_wallet_api.h"
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
#include "wallet/core/wallet_db.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/simple_transaction.h"
#include "wallet/core/node_network.h"
#include "keykeeper/local_private_key_keeper.h"
#include "wallet/transactions/assets/assets_reg_creators.h"
#include "wallet/transactions/lelantus/lelantus_reg_creators.h"

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "api_cli_swap.h"
#endif

#ifdef BEAM_IPFS_SUPPORT
#include "wallet/ipfs/ipfs.h"
#include "wallet/ipfs/ipfs_async.h"
#endif

#include "wallet/transactions/lelantus/lelantus_reg_creators.h"
#include "wallet/core/contracts/i_shaders_manager.h"
#include "nlohmann/json.hpp"
#include "version.h"

using json = nlohmann::json;
using namespace beam;
using namespace beam::wallet;

namespace
{
    const unsigned LOG_ROTATION_PERIOD = 3 * 60 * 60 * 1000; // 3 hours
    const size_t PACKER_FRAGMENTS_SIZE = 4096;
    constexpr size_t LINE_FRAGMENT_SIZE = 4096;

    struct TlsOptions
    {
        bool use;
        std::string certPath;
        std::string keyPath;
        bool requestCertificate;
        bool rejectUnauthorized;
    };

    struct ConnectionOptions
    {
        bool useHttp;
        size_t maxLineSize;
        TlsOptions tls;
    };

    ApiACL loadACL(const std::string& path)
    {
        std::ifstream file(path);
        std::string line;
        ApiACL::value_type keys;
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

        return ApiACL(keys);
    }

    class IWalletApiServer
    {
    public:
        virtual void closeConnection(uint64_t id) = 0;
    };

    class IServerConnection
    {
    public:
        typedef std::shared_ptr<IServerConnection> Ptr;
        virtual ~IServerConnection() = default;
    };

    class WalletApiServer
        : public IWalletApiServer
    {
    public:
        WalletApiServer(const std::string& apiVersion, IWalletDB::Ptr walletDB,
                        Wallet::Ptr wallet,
                        NodeNetwork::Ptr nnet,
                        io::Reactor& reactor,
                        io::Address listenTo,
                        const ConnectionOptions& connectionOptions,
                        ApiACL acl,
                        const std::vector<uint32_t>& whitelist)

            : _apiVersion(apiVersion)
            , _reactor(reactor)
            , _bindAddress(listenTo)
            , _connectionOptions(connectionOptions)
            , _walletDB(walletDB)
            , _wallet(wallet)
            , _network(nnet)
            , _acl(acl)
            , _whitelist(whitelist)
        {
            start();
        }

        ~WalletApiServer()
        {
            stop();
        }

        #ifdef BEAM_IPFS_SUPPORT
        struct IPFSHandler: beam::wallet::IPFSService::Handler
        {
            explicit IPFSHandler(io::Reactor::Ptr reactor, std::function<void (std::string)> onerr)
            {
                _toServerThread = PostToReactorThread::create(std::move(reactor));
                _onError = std::move(onerr);
            }

            void AnyThread_pushToClient(std::function<void()>&& action) override
            {
                _toServerThread->post(std::move(action));
            }

            void AnyThread_onStatus(const std::string& error, uint32_t peercnt) override
            {
                if (error.empty())
                {
                    LOG_INFO() << "IPFS peers count: " << peercnt;
                    return;
                }

                _toServerThread->post([onerr = _onError, error](){
                    onerr(error);
                });
            }

        private:
            wallet::PostToReactorThread::Ptr _toServerThread;
            std::function<void (std::string)> _onError;
        };

        bool startIPFS(asio_ipfs::config config, io::Reactor::Ptr reactor)
        {
            try
            {
                _ipfsHandler = std::make_shared<IPFSHandler>(reactor, [reactor, this] (const std::string& error) {
                    _ipfsError = error;
                    reactor->stop();
                });

                _ipfs = beam::wallet::IPFSService::AnyThread_create(_ipfsHandler);
                _ipfs->ServiceThread_start(std::move(config));
                return true;
            }
            catch(std::runtime_error& err)
            {
                LOG_ERROR() << err.what();
                return false;
            }
        }

        void stopIPFS()
        {
            _ipfs->ServiceThread_stop();
            _ipfs.reset();
            _ipfsHandler.reset();
        }

        [[nodiscard]] const std::string& getIPFSError() const
        {
            return _ipfsError;
        }

        private:
            beam::wallet::IPFSService::Ptr _ipfs;
            std::shared_ptr<IPFSHandler> _ipfsHandler;
            std::string _ipfsError;
        public:
        #endif

        #ifdef BEAM_ATOMIC_SWAP_SUPPORT
        void initSwapFeature(proto::FlyClient::INetwork::Ptr nnet, IWalletMessageEndpoint& wnet)
        {
            _swapsProvider = std::make_shared<ApiCliSwap>(_walletDB);
            _swapsProvider->initSwapFeature(nnet, wnet);
        }
        #endif

    protected:
        void start()
        {
            LOG_INFO() << "Start server on " << _bindAddress;

            try
            {
                const auto& tls = _connectionOptions.tls;
                _server = tls.use
                    ? io::SslServer::create(_reactor, _bindAddress, BIND_THIS_MEMFN(on_stream_accepted)
                        , tls.certPath.c_str(), tls.keyPath.c_str(), tls.requestCertificate, tls.rejectUnauthorized)
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

        template<typename T>
        IServerConnection::Ptr createConnection(io::TcpStream::Ptr&& newStream)
        {
            if (!_walletData)
            {
                _walletData = std::make_unique<ApiInitData>();
                _walletData->walletDB    = _walletDB;
                _walletData->wallet      = _wallet;
                _walletData->acl         = _acl;
                _walletData->contracts   = IShadersManager::CreateInstance(_wallet, _walletDB, _network, "", "", 0);
                _walletData->nodeNetwork = _network;

                #ifdef BEAM_ATOMIC_SWAP_SUPPORT
                _walletData->swaps = _swapsProvider;
                #endif

                #ifdef BEAM_IPFS_SUPPORT
                _walletData->ipfs = _ipfs;
                #endif
            }

            return std::make_shared<T>(_apiVersion, *this, std::move(newStream), *_walletData, _connectionOptions);
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

                _connections[peer.u64()] = _connectionOptions.useHttp
                    ? createConnection<HttpApiConnection>(std::move(newStream))
                    : createConnection<TcpApiConnection>(std::move(newStream));
            }

            LOG_DEBUG() << "on_stream_accepted";
        }

    private:
        class TcpApiConnection
            : public IServerConnection
            , public IWalletApiHandler
        {
        public:
            TcpApiConnection(const std::string& apiVersion, IWalletApiServer& server, io::TcpStream::Ptr&& newStream, ApiInitData& walletData, const ConnectionOptions& options)
                : _server(server)
                , _stream(std::move(newStream))
                , _lineProtocol(BIND_THIS_MEMFN(on_raw_message), BIND_THIS_MEMFN(on_write), LINE_FRAGMENT_SIZE, options.maxLineSize)
            {
                _walletApi = IWalletApi::CreateInstance(apiVersion, *this, walletData);
                _stream->enable_keepalive(2);
                _stream->enable_read(BIND_THIS_MEMFN(on_stream_data));
            }

            void sendAPIResponse(const json& result) override
            {
                serialize_json_msg(_lineProtocol, result);
            }

            void on_write(io::SharedBuffer&& msg)
            {
                _stream->write(msg);
            }

            bool on_raw_message(void* data, size_t size)
            {
                _walletApi->executeAPIRequest(static_cast<const char*>(data), size);
                return size > 0;
            }

            bool on_stream_data(io::ErrorCode errorCode, void* data, size_t size)
            {
                if (errorCode != 0)
                {
                    LOG_INFO() << "peer disconnected, code=" << io::error_str(errorCode);
                    closeConnection();
                    return false;
                }

                if (!_lineProtocol.new_data_from_stream(data, size))
                {
                    LOG_INFO() << "stream corrupted";
                    closeConnection();
                    return false;
                }

                return true;
            }

            void closeConnection()
            {
                _stream->shutdown();
                _server.closeConnection(_stream->peer_address().u64());
            }

        private:
            IWalletApi::Ptr _walletApi;
            IWalletApiServer& _server;
            io::TcpStream::Ptr _stream;
            LineProtocol _lineProtocol;
        };

        class HttpApiConnection
            : public IServerConnection
            , public IWalletApiHandler
        {
        public:
            HttpApiConnection(const std::string& apiVersion, IWalletApiServer& server, io::TcpStream::Ptr&& newStream, ApiInitData& walletData, const ConnectionOptions&)
                : _server(server)
                , _sendResponseCalled(false)
                , _msgCreator(2000)
                , _packer(PACKER_FRAGMENTS_SIZE)
            {
                _walletApi = IWalletApi::CreateInstance(apiVersion, *this, walletData);

                newStream->enable_keepalive(1);
                auto peerId = newStream->peer_address().u64();

                _connection = std::make_unique<HttpConnection>(
                    peerId,
                    BaseConnection::inbound,
                    BIND_THIS_MEMFN(on_request),
                    16 * 1024 * 1024,
                    1024,
                    std::move(newStream)
                    );
            }

            void sendAPIResponse(const json& result) override
            {
                _sendResponseCalled = true;
                serialize_json_msg(_body, _packer, result);
                send(_connection, 200, "OK");
            }

        private:
            bool on_request(uint64_t id, const HttpMsgReader::Message& msg)
            {
                assert(_connection->id() == id);
                if (!_connection->is_connected())
                {
                    return false;
                }

                if ((msg.what != HttpMsgReader::http_message || !msg.msg) && msg.what != HttpMsgReader::message_too_long)
                {
                    LOG_DEBUG() << "-peer " << io::Address::from_u64(id) << " : " << msg.error_str();
                    closeConnection();
                    return false;
                }

                if (msg.what == HttpMsgReader::message_too_long)
                {
                    return send(_connection, 413, "Payload Too Large");
                }

                if (msg.msg->get_path() != "/api/wallet")
                {
                    return send(_connection, 404, "Not Found");
                }

                _body.clear();

                size_t size = 0;
                auto data = msg.msg->get_body(size);

                if (size == 0)
                {
                    return send(_connection, 400, "Bad Request");
                }

                _sendResponseCalled = false;
                const auto asyncResult = _walletApi->executeAPIRequest(reinterpret_cast<const char*>(data), size);

                if (asyncResult == ApiSyncMode::DoneSync)
                {
                    if(_sendResponseCalled)
                    {
                        return _connection->is_connected();
                    }

                    // all sync functions should already have sent response
                    std::stringstream ss;
                    ss << "API sync method has not called SendAPIResponse, request: " << data;
                    LOG_ERROR() << ss.str();

                    closeConnection();
                    throw std::runtime_error(ss.str());
                }

                // async function may or may have not sent response if sent
                // and failed to send, we will be disconnected otherwise keep
                // alive and let an opportunity to send response later
                return _connection->is_connected();
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

                if (ok && code == 200)
                {
                    return true;
                }

                closeConnection();
                return false;
            }

            void closeConnection ()
            {
                _connection->shutdown();
                _server.closeConnection(_connection->id());
            }

            HttpConnection::Ptr _connection;
            IWalletApiServer&   _server;
            bool                _sendResponseCalled;
            HttpMsgCreator      _msgCreator;
            HttpMsgCreator      _packer;
            io::SerializedMsg   _headers;
            io::SerializedMsg   _body;
            IWalletApi::Ptr     _walletApi;
        };

        std::string        _apiVersion;
        io::Reactor&       _reactor;
        io::TcpServer::Ptr _server;
        io::Address        _bindAddress;
        ConnectionOptions  _connectionOptions;

        std::unordered_map<uint64_t, IServerConnection::Ptr> _connections;

        IWalletDB::Ptr _walletDB;
        Wallet::Ptr _wallet;
        NodeNetwork::Ptr _network;

        #ifdef BEAM_ATOMIC_SWAP_SUPPORT
        std::shared_ptr<ApiCliSwap> _swapsProvider;
        #endif // BEAM_ATOMIC_SWAP_SUPPORT

        std::unique_ptr<ApiInitData> _walletData;
        std::vector<uint64_t> _pendingToClose;
        ApiACL _acl;
        std::vector<uint32_t> _whitelist;
    };
}

int main(int argc, char* argv[])
{
    namespace po = boost::program_options;
    using namespace beam;

    struct
    {
        uint16_t port;
        std::string walletPath;
        std::string nodeURI;
        Nonnegative<uint32_t> pollPeriod_ms;

        bool useAcl;
        std::string aclPath;
        std::string whitelist;
        std::string apiVersion;

        uint32_t logCleanupPeriod;
        bool enableLelantus = false;
        bool enableBodyRequests = false;
    } options;
    ConnectionOptions connectionOptions;

    po::options_description desc("Wallet API general options");
    {
        desc.add_options()
            (cli::HELP_FULL,       "list of all options")
            (cli::VERSION_FULL,    "print project version")
            (cli::PORT_FULL,        po::value(&options.port)->default_value(10000), "port to start server on")
            (cli::NODE_ADDR_FULL,   po::value<std::string>(&options.nodeURI), "address of node")
            (cli::WALLET_STORAGE,   po::value<std::string>(&options.walletPath)->default_value("wallet.db"), "path to wallet file")
            (cli::PASS,             po::value<std::string>(), "password for the wallet")
            (cli::API_USE_HTTP,     po::value<bool>(&connectionOptions.useHttp)->default_value(false), "use JSON RPC over HTTP")
            (cli::IP_WHITELIST,     po::value<std::string>(&options.whitelist)->default_value(""), "IP whitelist")
            (cli::NODE_POLL_PERIOD, po::value<Nonnegative<uint32_t>>(&options.pollPeriod_ms)->default_value(Nonnegative<uint32_t>(0)), "Node poll period in milliseconds. Set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks if it is less then it will be rounded up to block rate value.")
            (cli::WITH_ASSETS,      po::bool_switch()->default_value(false), "enable confidential assets transactions")
            (cli::ENABLE_LELANTUS,  po::bool_switch()->default_value(false), "enable Lelantus MW transactions")
            (cli::API_VERSION,      po::value<std::string>(&options.apiVersion)->default_value("current"), "API version")
            (cli::CONFIG_FILE_PATH, po::value<std::string>()->default_value("wallet-api.cfg"), "path to the config file")
            (cli::LOG_LEVEL,        po::value<std::string>(), "set log level [error|warning|info(default)|debug|verbose]")
            (cli::FILE_LOG_LEVEL,   po::value<std::string>(), "set file log level [error|warning|info(default)|debug|verbose]")
            (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>()->default_value(5), "old logfiles cleanup period(days)")
            (cli::API_TCP_MAX_LINE, po::value<size_t>(&connectionOptions.maxLineSize)->default_value(65536), "max line size in TCP mode")
            (cli::REQUEST_BODIES,   po::value<bool>(&options.enableBodyRequests)->default_value(false), "request and parse block bodies on the wallet side")
        ;

        po::options_description authDesc("User authorization options");
        authDesc.add_options()
                (cli::API_USE_ACL, po::value<bool>(&options.useAcl)->default_value(false), "use Access Control List (ACL)")
                (cli::API_ACL_PATH, po::value<std::string>(&options.aclPath)->default_value("wallet_api.acl"), "path to ACL file")
        ;

        po::options_description tlsDesc("TLS protocol options");
        tlsDesc.add_options()
                (cli::API_USE_TLS, po::value<bool>(&connectionOptions.tls.use)->default_value(false), "use TLS protocol")
                (cli::API_TLS_CERT, po::value<std::string>(&connectionOptions.tls.certPath)->default_value("wallet_api.crt"), "path to TLS certificate")
                (cli::API_TLS_KEY, po::value<std::string>(&connectionOptions.tls.keyPath)->default_value("wallet_api.key"), "path to TLS private key")
                (cli::API_TLS_REQUEST_CERTIFICATE, po::value<bool>(&connectionOptions.tls.requestCertificate)->default_value("false"), "request client's certificate for verification")
                (cli::API_TLS_REJECT_UNAUTHORIZED, po::value<bool>(&connectionOptions.tls.rejectUnauthorized)->default_value("true"), "server will reject any connection which is not authorized with the list of supplied CAs.")
        ;

        desc.add(authDesc);
        desc.add(tlsDesc);

        #ifdef BEAM_IPFS_SUPPORT
        auto ipfsDefs = asio_ipfs::config(asio_ipfs::config::Mode::Server);
        desc.add(createIPFSOptionsDesrition(false, ipfsDefs));
        #endif

        desc.add(createRulesOptionsDescription());
    }

    try
    {
        po::variables_map vm;
        try
        {
            po::store(po::command_line_parser(argc, argv)
                              .options(desc)
                              .style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing)
                              .run(), vm);
        }
        catch (const std::exception& e)
        {
            std::cerr << "EXCEPTION: " << e.what() << std::endl;
        }

        if (vm.count(cli::HELP))
        {
            std::cout << desc << std::endl;
            return 0;
        }

        if (vm.count(cli::VERSION))
        {
            std::cout << PROJECT_VERSION << std::endl;
            return 0;
        }

        auto cfgCommon = ReadCfgFromFileCommon(vm, desc);
        auto cfgApi = ReadCfgFromFile(vm, desc);
        vm.notify();

        int logLevel = getLogLevel(cli::LOG_LEVEL, vm, LOG_LEVEL_DEBUG);
        int fileLogLevel = getLogLevel(cli::FILE_LOG_LEVEL, vm, LOG_LEVEL_DEBUG);

        const auto path = boost::filesystem::system_complete("./logs");
        auto logger = beam::Logger::create(logLevel, logLevel, fileLogLevel, "api_", path.string());

        // Since ReadCfg outputs file name to std::cout print also to logs
        if (cfgCommon)
        {
            LOG_INFO() << "Wallet API common config read from: " << *cfgCommon;
        }

        if (cfgApi)
        {
            LOG_INFO() << "Wallet API config read from: " << *cfgApi;
        }

        io::Address node_addr;
        IWalletDB::Ptr walletDB;
        ApiACL acl;
        std::vector<uint32_t> whitelist;

        {
            if (!IWalletApi::ValidateAPIVersion(options.apiVersion))
            {
                std::cout << "Unsupported API version requested: " << options.apiVersion << std::endl;
                return 1;
            }

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

            if (connectionOptions.tls.use)
            {
                const auto& certPath = connectionOptions.tls.certPath;
                if (certPath.empty() || !boost::filesystem::exists(certPath))
                {
                    LOG_ERROR() << "TLS certificate not found, path is: " << certPath;
                    return -1;
                }

                const auto& keyPath = connectionOptions.tls.keyPath;
                if (keyPath.empty() || !boost::filesystem::exists(keyPath))
                {
                    LOG_ERROR() << "TLS private key not found, path is: " << keyPath;
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

            walletDB = WalletDB::open(options.walletPath, pass);
            LOG_INFO() << "wallet successfully opened...";

            // this should be exactly CLI flag value to print correct error messages
            // Rules::CA.Enabled would be checked as well but later
            wallet::g_AssetsEnabled = vm[cli::WITH_ASSETS].as<bool>();
            options.enableLelantus = vm[cli::ENABLE_LELANTUS].as<bool>();
        }

        io::Reactor::Ptr reactor = io::Reactor::create();
        io::Address listenTo = io::Address().port(options.port);
        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);

        LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD, options.logCleanupPeriod);
        auto wallet = std::make_shared<Wallet>(walletDB);
        wallet->EnableBodyRequests(options.enableBodyRequests);

        auto nnet = std::make_shared<NodeNetwork>(*wallet);
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

        uint32_t responseTime_s = Rules::get().DA.Target_s * wallet::kDefaultTxResponseTime;
        if (nnet->m_Cfg.m_PollPeriod_ms >= responseTime_s * 1000)
        {
            LOG_WARNING() << "The \"--node_poll_period\" parameter set to more than "
                          << uint32_t(responseTime_s / 3600)
                          << " hours may cause transaction problems.";
        }

        nnet->m_Cfg.m_vNodes.push_back(node_addr);
        nnet->Connect();

        auto wnet = std::make_shared<WalletNetworkViaBbs>(*wallet, nnet, walletDB);
		wallet->AddMessageEndpoint(wnet);
        wallet->SetNodeEndpoint(nnet);

        WalletApiServer server(options.apiVersion, walletDB, wallet, nnet, *reactor, listenTo, connectionOptions, acl, whitelist);

        #ifdef BEAM_ATOMIC_SWAP_SUPPORT
        RegisterSwapTxCreators(wallet, walletDB);
        server.initSwapFeature(nnet, *wnet);
        #endif

        #ifdef BEAM_IPFS_SUPPORT
        auto ipfsOpts = getIPFSConfig(vm, asio_ipfs::config(asio_ipfs::config::Mode::Server));
        if (ipfsOpts)
        {
            if(!server.startIPFS(*ipfsOpts, reactor))
            {
                return -1;
            }
        }
        else
        {
            LOG_INFO() << "IPFS is not enabled. IPFS node will be not started.";
        }
        #endif

        if (Rules::get().CA.Enabled && wallet::g_AssetsEnabled)
        {
            RegisterAllAssetCreators(*wallet);
        }

        if (options.enableLelantus)
        {
            lelantus::RegisterCreators(*wallet, walletDB);
        }

        // All TxCreators must be registered by this point
        wallet->ResumeAllTransactions();
        io::Reactor::get_Current().run();

        #ifdef BEAM_IPFS_SUPPORT
        if (ipfsOpts)
        {
            server.stopIPFS();
            if(auto err = server.getIPFSError(); !err.empty())
            {
                throw std::runtime_error(std::string("IPFS failed: ") + err);
            }
        }
        #endif
        LOG_INFO() << "Done";
    }
    // DO NOT USE LOG_ below. Logger is dead here
    catch (const DatabaseException& e)
    {
        std::cerr << "Wallet not opened. " << e.what();
        return -1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "EXCEPTION: " << e.what();
        return -1;
    }
    catch (...)
    {
        std::cerr << "NON_STD EXCEPTION";
        return -1;
    }

    return 0;
}
