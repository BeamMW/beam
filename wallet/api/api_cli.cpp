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

#include "wallet/api/api.h"
#include "wallet/api/api_connection.h"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <map>
#include <core/block_crypt.h>

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
#include "keykeeper/local_private_key_keeper.h"

#if defined(BEAM_ATOMIC_SWAP_SUPPORT)
#include "wallet/api/i_atomic_swap_provider.h"
#include "wallet/transactions/swaps/utils.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_router.h"
#include "wallet/transactions/swaps/bridges/bitcoin/client.h"
#include "wallet/transactions/swaps/bridges/bitcoin/bridge_holder.h"
#include "wallet/transactions/swaps/bridges/bitcoin/bitcoin.h"
#include "wallet/transactions/swaps/bridges/litecoin/litecoin.h"
#include "wallet/transactions/swaps/bridges/denarius/denarius.h"
#include "wallet/transactions/swaps/bridges/qtum/qtum.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

#include "nlohmann/json.hpp"
#include "version.h"

using json = nlohmann::json;

static const unsigned LOG_ROTATION_PERIOD = 3 * 60 * 60 * 1000; // 3 hours
static const size_t PACKER_FRAGMENTS_SIZE = 4096;

using namespace beam;
using namespace beam::wallet;

namespace
{
struct TlsOptions
{
    bool use;
    std::string certPath;
    std::string keyPath;
    bool requestCertificate;
    bool rejectUnauthorized;
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

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
using BaseSwapClient = beam::bitcoin::Client;
class SwapClient : public BaseSwapClient
{
public:
    using Ptr = std::shared_ptr<SwapClient>;
    SwapClient(
        beam::bitcoin::IBridgeHolder::Ptr bridgeHolder,
        std::unique_ptr<beam::bitcoin::SettingsProvider> settingsProvider,
        io::Reactor& reactor)
        : BaseSwapClient(bridgeHolder,
                         std::move(settingsProvider),
                         reactor)
        , _timer(beam::io::Timer::create(reactor))
    {
        requestBalance();
        _timer->start(1000, true, [this] ()
        {
            requestBalance();
        });
    }
    Amount GetAvailable() const
    {
        return _balance.m_available;
    }

private:
    beam::io::Timer::Ptr _timer;
    Balance _balance;
    Status _status;
    void requestBalance()
    {
        if (GetSettings().IsActivated())
        {
            // update balance
            GetAsync()->GetBalance();
        }
    }
    void OnStatus(Status status) override
    {
        _status = status;
    }
    void OnBalance(const Balance& balance) override
    {
        _balance = balance;
    }
    void OnCanModifySettingsChanged(bool canModify) override {}
    void OnChangedSettings() override {}
    void OnConnectionError(beam::bitcoin::IBridge::ErrorType error) override {}
};
#endif // BEAM_ATOMIC_SWAP_SUPPORT

class IWalletApiServer
{
public:
    virtual void closeConnection(uint64_t id) = 0;
};

class WalletApiServer 
    : public IWalletApiServer
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
    , public IAtomicSwapProvider
    , ISwapOffersObserver
#endif // BEAM_ATOMIC_SWAP_SUPPORT
{
public:
    WalletApiServer(IWalletDB::Ptr walletDB, Wallet& wallet, io::Reactor& reactor, 
        io::Address listenTo, bool useHttp, WalletApi::ACL acl, const TlsOptions& tlsOptions, const std::vector<uint32_t>& whitelist)
        : _reactor(reactor)
        , _bindAddress(listenTo)
        , _useHttp(useHttp)
        , _tlsOptions(tlsOptions)
        , _walletDB(walletDB)
        , _wallet(wallet)
        , _acl(acl)
        , _whitelist(whitelist)
    {
        start();
    }

    ~WalletApiServer()
    {
        stop();
    }

#if defined(BEAM_ATOMIC_SWAP_SUPPORT)
    Amount getBtcAvailable() const override
    {
        return _bitcoinClient ? _bitcoinClient->GetAvailable() : 0;
    }

    Amount getLtcAvailable() const override
    {
        return _litecoinClient ? _litecoinClient->GetAvailable() : 0;
    }

    Amount getDAvailable() const override
    {
        return _denariusClient ? _denariusClient->GetAvailable() : 0;
    }

    Amount getQtumAvailable() const override
    {
        return _qtumClient ? _qtumClient->GetAvailable() : 0;
    }

    const SwapOffersBoard& getSwapOffersBoard() const override
    {
        return *_offersBulletinBoard;
    }

    using WalletDbSubscriber =
        ScopedSubscriber<wallet::IWalletDbObserver, wallet::IWalletDB>;
    using SwapOffersBoardSubscriber =
        ScopedSubscriber<wallet::ISwapOffersObserver, wallet::SwapOffersBoard>;
    void initSwapFeature(
        proto::FlyClient::INetwork& nnet, IWalletMessageEndpoint& wnet)
    {
        _broadcastRouter = std::make_shared<BroadcastRouter>(nnet, wnet);
        _offerBoardProtocolHandler =
            std::make_shared<OfferBoardProtocolHandler>(
                _walletDB->get_SbbsKdf(), _walletDB);
        _offersBulletinBoard = std::make_shared<SwapOffersBoard>(
            *_broadcastRouter, *_offerBoardProtocolHandler);
        _walletDbSubscriber = std::make_unique<WalletDbSubscriber>(
            static_cast<IWalletDbObserver*>(
                _offersBulletinBoard.get()), _walletDB);
        _swapOffersBoardSubscriber =
            std::make_unique<SwapOffersBoardSubscriber>(
                static_cast<ISwapOffersObserver*>(this), _offersBulletinBoard);

        _btcBridgeHolder = std::make_shared<
            bitcoin::BridgeHolder<bitcoin::Electrum,
                                  bitcoin::BitcoinCore017>>();

        auto bitcoinSettingsProvider =
            std::make_unique<bitcoin::SettingsProvider>(_walletDB);
        bitcoinSettingsProvider->Initialize();
        _bitcoinClient = std::make_shared<SwapClient>(
            _btcBridgeHolder,
            std::move(bitcoinSettingsProvider),
            io::Reactor::get_Current()
        );

        _ltcBridgeHolder = std::make_shared<
            bitcoin::BridgeHolder<litecoin::Electrum,
                                  litecoin::LitecoinCore017>>();
        auto litecoinSettingsProvider =
            std::make_unique<litecoin::SettingsProvider>(_walletDB);
        litecoinSettingsProvider->Initialize();
        _litecoinClient = std::make_shared<SwapClient>(
            _ltcBridgeHolder,
            std::move(litecoinSettingsProvider),
            io::Reactor::get_Current()
        );

        _dBridgeHolder = std::make_shared<
            bitcoin::BridgeHolder<denarius::Electrum,
                                  denarius::DenariusCore017>>();
        auto denariusSettingsProvider =
            std::make_unique<denarius::SettingsProvider>(_walletDB);
        denariusSettingsProvider->Initialize();
        _denariusClient = std::make_shared<SwapClient>(
            _dBridgeHolder,
            std::move(denariusSettingsProvider),
            io::Reactor::get_Current()
        );

        _qtumBridgeHolder = std::make_shared<
            bitcoin::BridgeHolder<qtum::Electrum, qtum::QtumCore017>>();
        auto qtumSettingsProvider =
            std::make_unique<qtum::SettingsProvider>(_walletDB);
        qtumSettingsProvider->Initialize();
        _qtumClient = std::make_shared<SwapClient>(
            _qtumBridgeHolder,
            std::move(qtumSettingsProvider),
            io::Reactor::get_Current()
        );
    }

    void onSwapOffersChanged(
        ChangeAction action, const std::vector<SwapOffer>& offers) override
    {

    }
private:
    std::shared_ptr<BroadcastRouter> _broadcastRouter;
    std::shared_ptr<OfferBoardProtocolHandler> _offerBoardProtocolHandler;
    SwapOffersBoard::Ptr _offersBulletinBoard;
    std::unique_ptr<WalletDbSubscriber> _walletDbSubscriber;
    std::unique_ptr<SwapOffersBoardSubscriber> _swapOffersBoardSubscriber;

    beam::bitcoin::IBridgeHolder::Ptr _btcBridgeHolder;
    beam::bitcoin::IBridgeHolder::Ptr _ltcBridgeHolder;
    beam::bitcoin::IBridgeHolder::Ptr _dBridgeHolder;
    beam::bitcoin::IBridgeHolder::Ptr _qtumBridgeHolder;

    SwapClient::Ptr _bitcoinClient;
    SwapClient::Ptr _litecoinClient;
    SwapClient::Ptr _denariusClient;
    SwapClient::Ptr _qtumClient;
#endif // BEAM_ATOMIC_SWAP_SUPPORT

protected:

    void start()
    {
        LOG_INFO() << "Start server on " << _bindAddress;

        try
        {
            _server = _tlsOptions.use
                ? io::SslServer::create(_reactor, _bindAddress, BIND_THIS_MEMFN(on_stream_accepted)
                    , _tlsOptions.certPath.c_str(), _tlsOptions.keyPath.c_str(), _tlsOptions.requestCertificate, _tlsOptions.rejectUnauthorized)
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

        struct WalletData : ApiConnection::IWalletData
        {
            WalletData(IWalletDB::Ptr walletDB, Wallet& wallet, IAtomicSwapProvider& atomicSwapProvider)
                : m_walletDB(walletDB)
                , m_wallet(wallet)
                , m_atomicSwapProvider(atomicSwapProvider)
            {}

            virtual ~WalletData() {}

            IWalletDB::Ptr getWalletDB() override
            {
                return m_walletDB;
            }

            Wallet& getWallet() override
            {
                return m_wallet;
            }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
            const IAtomicSwapProvider& getAtomicSwapProvider() const override
            {
                return m_atomicSwapProvider;
            }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

            IWalletDB::Ptr m_walletDB;
            Wallet& m_wallet;
            IAtomicSwapProvider& m_atomicSwapProvider;
        };

    template<typename T>
    std::shared_ptr<ApiConnection> createConnection(io::TcpStream::Ptr&& newStream)
    {
        if (!_walletData)
        {
            _walletData = std::make_unique<WalletData>(_walletDB, _wallet, *this);
        }

    return std::static_pointer_cast<ApiConnection>(
        std::make_shared<T>(*this
                            , std::move(newStream)
                            , *_walletData
                            , _acl));
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
    class TcpApiConnection : public ApiConnection
    {
    public:
    TcpApiConnection(IWalletApiServer& server
                    , io::TcpStream::Ptr&& newStream
                    , IWalletData& walletData
                    , WalletApi::ACL acl
        )
        : ApiConnection(walletData
                      , acl )
        , _server(server)
        , _stream(std::move(newStream))
        , _lineProtocol(BIND_THIS_MEMFN(on_raw_message), BIND_THIS_MEMFN(on_write))
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
        IWalletApiServer& _server;
        io::TcpStream::Ptr _stream;
        LineProtocol _lineProtocol;
    };

    class HttpApiConnection : public ApiConnection
    {
    public:
        HttpApiConnection(IWalletApiServer& server
                        , io::TcpStream::Ptr&& newStream
                        , IWalletData& walletData
                        , WalletApi::ACL acl
            )
            : ApiConnection(
                  walletData
                , acl)
            , _server(server)
            , _keepalive(false)
            , _msgCreator(2000)
            , _packer(PACKER_FRAGMENTS_SIZE)
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
        IWalletApiServer& _server;
        bool _keepalive;

        HttpMsgCreator _msgCreator;
        HttpMsgCreator _packer;
        io::SerializedMsg _headers;
        io::SerializedMsg _body;
    };

    io::Reactor& _reactor;
    io::TcpServer::Ptr _server;
    io::Address _bindAddress;
    bool _useHttp;
    TlsOptions _tlsOptions;

    std::unordered_map<uint64_t, std::shared_ptr<ApiConnection>> _connections;

    IWalletDB::Ptr _walletDB;
    Wallet& _wallet;
        std::unique_ptr<WalletData> _walletData;
    std::vector<uint64_t> _pendingToClose;
    WalletApi::ACL _acl;
    std::vector<uint32_t> _whitelist;
};
}  // namespace

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
                (cli::API_TLS_REQUEST_CERTIFICATE, po::value<bool>(&tlsOptions.requestCertificate)->default_value("false"), "request client's certificate for verification")
                (cli::API_TLS_REJECT_UNAUTHORIZED, po::value<bool>(&tlsOptions.rejectUnauthorized)->default_value("true"), "server will reject any connection which is not authorized with the list of supplied CAs.")
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

            walletDB = WalletDB::open(options.walletPath, pass);

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

        WalletApiServer server(walletDB, wallet, *reactor, 
            listenTo, options.useHttp, acl, tlsOptions, whitelist);

#if defined(BEAM_ATOMIC_SWAP_SUPPORT)
        RegisterSwapTxCreators(wallet, walletDB);
        server.initSwapFeature(*nnet, *wnet);
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

        // All TxCreators must be registered by this point
        wallet.ResumeAllTransactions();

        io::Reactor::get_Current().run();

        LOG_INFO() << "Done";
    }
    catch (const DatabaseException&)
    {
        LOG_ERROR() << "Wallet not opened.";
        return -1;
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
