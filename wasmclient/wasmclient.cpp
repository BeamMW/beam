// Copyright 2021 The Beam Team
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
#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/threading.h>
#include <emscripten/val.h>
#include <boost/algorithm/string.hpp>
#include <queue>
#include <exception>
#include <filesystem>
#include <thread>
#include <cstdint>
#include <memory>

#include "common.h"
#include "wallet/client/wallet_client.h"
#include "wallet/api/i_wallet_api.h"
#include "wallet/core/wallet_db.h"
#include "wallet/api/i_wallet_api.h"
#include "wallet/client/apps_api/apps_utils.h"
#include "wallet/transactions/lelantus/lelantus_reg_creators.h"
#include "wallet/transactions/lelantus/push_transaction.h"
#include "mnemonic/mnemonic.h"
#include "utility/string_helpers.h"
#include "p2p/line_protocol.h"
#include "http/http_connection.h"
#include "http/http_msg_creator.h"
#include "wasm_beamapi.h"
#include "utility/io/tcpserver.h"
#include "utility/io/sslserver.h"
#include "utility/io/json_serializer.h"
namespace fs = std::filesystem;
using namespace beam;
using namespace beam::io;
using namespace std;
using namespace emscripten;
using namespace ECC;
using namespace beam::wallet;

#define Assert(x) ((void)((x) || (__assert_fail(#x, __FILE__, __LINE__, __func__),0)))

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

        virtual ~WalletApiServer()
        {
            stop();
        }

#ifdef BEAM_IPFS_SUPPORT
        struct IPFSHandler : beam::wallet::IPFSService::Handler
        {
            explicit IPFSHandler(io::Reactor::Ptr reactor, std::function<void(std::string)> onerr)
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

                _toServerThread->post([onerr = _onError, error]() {
                    onerr(error);
                    });
            }

        private:
            wallet::PostToReactorThread::Ptr _toServerThread;
            std::function<void(std::string)> _onError;
        };

        bool startIPFS(asio_ipfs::config config, io::Reactor::Ptr reactor)
        {
            try
            {
                _ipfsHandler = std::make_shared<IPFSHandler>(reactor, [reactor, this](const std::string& error) {
                    _ipfsError = error;
                    reactor->stop();
                    });

                _ipfs = beam::wallet::IPFSService::AnyThread_create(_ipfsHandler);
                _ipfs->ServiceThread_start(std::move(config));
                return true;
            }
            catch (std::runtime_error& err)
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
                //const auto& tls = _connectionOptions.tls;
                _server = //tls.use
                    //? io::SslServer::create(_reactor, _bindAddress, BIND_THIS_MEMFN(on_stream_accepted)
                    //    , tls.certPath.c_str(), tls.keyPath.c_str(), tls.requestCertificate, tls.rejectUnauthorized)
                    //: 
                    io::TcpServer::create(_reactor, _bindAddress, BIND_THIS_MEMFN(on_stream_accepted));

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
                _walletData->walletDB = _walletDB;
                _walletData->wallet = _wallet;
                _walletData->acl = _acl;
                _walletData->contracts = IShadersManager::CreateInstance(_wallet, _walletDB, _network, "", "");
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
                    if (_sendResponseCalled)
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

            void closeConnection()
            {
                _connection->shutdown();
                _server.closeConnection(_connection->id());
            }

            HttpConnection::Ptr _connection;
            IWalletApiServer& _server;
            bool                _sendResponseCalled;
            HttpMsgCreator      _msgCreator;
            HttpMsgCreator      _packer;
            io::SerializedMsg   _headers;
            io::SerializedMsg   _body;
            IWalletApi::Ptr     _walletApi;
        };

        std::string        _apiVersion;
        io::Reactor& _reactor;
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

namespace
{
    void GetWalletSeed(NoLeak<uintBig>& walletSeed, const std::string& s)
    {
        SecString seed;
        WordList phrase;

        auto tempPhrase = s;
        boost::algorithm::trim_if(tempPhrase, [](char ch) { return ch == ';'; });
        phrase = string_helpers::split(tempPhrase, ';');

        auto buf = decodeMnemonic(phrase);
        seed.assign(buf.data(), buf.size());

        walletSeed.V = seed.hash().V;
    }

    void GenerateDefaultAddress(IWalletDB::Ptr db)
    {
        db->generateAndSaveDefaultAddress();
    }

    IWalletDB::Ptr CreateDatabase(const std::string& s, const std::string& dbName, const std::string& pass)
    {
        ECC::NoLeak<ECC::uintBig> seed;
        GetWalletSeed(seed, s);
        auto r = io::Reactor::create();
        io::Reactor::Scope scope(*r);
        auto db = WalletDB::init(dbName, SecString(pass), seed);
        GenerateDefaultAddress(db);
        EM_ASM
        (
            FS.syncfs(false, function()
            {
                console.log("wallet created!");
            });
        );
        return db;
    }

    void HandleException(const std::exception& ex)
    {
        LOG_ERROR() << ex.what();
        EM_ASM
        (
            {
                throw new Error("Exception: " + Module.UTF8ToString($0));
            }, ex.what()
        );
    }
}

class WalletClient2
    : public WalletClient
    , public std::enable_shared_from_this<WalletClient2>
{
public:
    using Callback = std::function<void()>;
    using WeakPtr = std::weak_ptr<WalletClient2>;
    using Ptr = std::shared_ptr<WalletClient2>;
    using WalletClient::postFunctionToClientContext;

    struct ICallbackHandler
    {
        virtual void OnSyncProgress(int done, int total) {}
        virtual void OnResult(const json&) {}
        virtual void OnApproveSend(const std::string&, const string&, WasmAppApi::WeakPtr api) {}
        virtual void OnApproveContractInfo(const std::string& request, const std::string& info, const std::string& amounts, WasmAppApi::WeakPtr api) {}
        virtual void OnImportRecoveryProgress(uint64_t done, uint64_t total) {}
        virtual void OnWalletError(ErrorType error) {}
    };

    using WalletClient::WalletClient;

    virtual ~WalletClient2()
    {
        stopReactor(true);
    }

    void onBeforeMainLoop() override
    {
        ConnectionOptions connectionOptions;
        connectionOptions.tls.use = false;
        connectionOptions.maxLineSize = 128 * 1024 * 1024;
        connectionOptions.useHttp = true;
        m_apiServer.reset( new WalletApiServer("current", getWalletDB(), getWallet(), getNodeNetwork(), io::Reactor::get_Current(), io::Address().port(10000), connectionOptions, ApiACL{}, {}));
    }

    void onAfterMainLoop() override
    {
        m_apiServer.reset();
    }

    void SetHandler(ICallbackHandler* cb)
    {
        m_CbHandler = cb;
    }

    void SendResult(const json& result)
    {
        postFunctionToClientContext([this, sp = shared_from_this(), result]()
        {
            if (m_CbHandler)
            {
                m_CbHandler->OnResult(result);
            }
        });
    }

    void ClientThread_ApproveSend(const std::string& request, const std::string& info, WasmAppApi::Ptr api)
    {
        AssertMainThread();
        if (m_CbHandler)
        {
            m_CbHandler->OnApproveSend(request, info, api);
        }
    }

    void ClientThread_ApproveContractInfo(const std::string& request, const std::string& info, const std::string& amounts, WasmAppApi::Ptr api)
    {
        AssertMainThread();
        if (m_CbHandler)
        {
            m_CbHandler->OnApproveContractInfo(request, info, amounts, api);
        }
    }

    void Stop(Callback&& handler)
    {
        m_StoppedHandler = std::move(handler);
        stopReactor(true);
    }

    IWalletDB::Ptr GetWalletDB()
    {
        return getWalletDB();
    }

    void RegisterApi(WasmAppApi::Ptr api)
    {
        m_Apis.emplace_back(api);
    }

    void UnregisterApi(WasmAppApi::Ptr api)
    {
        m_Apis.erase(std::remove(m_Apis.begin(), m_Apis.end(), api), m_Apis.end());
    }

private:
    void onSyncProgressUpdated(int done, int total) override
    {
        postFunctionToClientContext([this, done, total]()
        {
            if (m_CbHandler)
            {
                m_CbHandler->OnSyncProgress(done, total);
            }
        });
    }

    void onPostFunctionToClientContext(MessageFunction&& func) override
    {
        {
            std::unique_lock lock(m_Mutex);
            m_Messages.push(std::move(func));
        }
        auto thisWeakPtr = std::make_unique<WeakPtr>(shared_from_this());
        emscripten_async_run_in_main_runtime_thread(
            EM_FUNC_SIG_VI,
            &WalletClient2::ProcessMessageOnMainThread,
            reinterpret_cast<int>(thisWeakPtr.release()));
    }

    void onStopped() override
    {
        WalletClient2::WeakPtr wp = shared_from_this();
        Assert(wp.use_count() == 1);
        postFunctionToClientContext([sp = shared_from_this(), wp]() mutable
        {
            Assert(wp.use_count() == 2);
            if (sp->m_StoppedHandler)
            {
                auto h = std::move(sp->m_StoppedHandler);
                sp.reset(); // handler may hold WalletClient too, but can destroy it, we don't want to prevent this
                Assert(wp.use_count() == 1);
                h();
            }
        });
    }

    void onImportRecoveryProgress(uint64_t done, uint64_t total) override
    {
        postFunctionToClientContext([this, done, total]()
        {
            if (m_CbHandler)
            {
                m_CbHandler->OnImportRecoveryProgress(done, total);
            }
        });
    }

    void onWalletError(ErrorType error) override
    {
        postFunctionToClientContext([this, error]()
        {
            if (m_CbHandler)
            {
                m_CbHandler->OnWalletError(error);
            }
        });
    }

    static void ProcessMessageOnMainThread(int pThis)
    {
        std::unique_ptr<WeakPtr> wp(reinterpret_cast<WeakPtr*>(pThis));
        assert(wp->use_count() == 1);
        while (true)
        {
            MessageFunction func;
            if (auto sp = wp->lock())
            {
                assert(wp->use_count() == 2);
                {
                    std::unique_lock lock(sp->m_Mutex);
                    if (sp->m_Messages.empty())
                    {
                        return;
                    }
                    func = std::move(sp->m_Messages.front());
                    sp->m_Messages.pop();
                }
            }
            else
            {
                return;
            }
            func();
        }
    }

private:
    std::mutex m_Mutex;
    std::queue<MessageFunction> m_Messages;
    ICallbackHandler* m_CbHandler = nullptr;
    Callback m_StoppedHandler;
    IWalletApi::Ptr m_WalletApi;
    std::vector<WasmAppApi::Ptr> m_Apis;
    std::unique_ptr<WalletApiServer> m_apiServer;
};

class WasmAppApiProxy
{
public:
    WasmAppApiProxy(WalletClient2::Ptr wc, WasmAppApi::Ptr api)
        : m_wclient{ wc }
        , m_wapi{ api }
    {

    }

    ~WasmAppApiProxy()
    {
        if (auto sp = m_wclient.lock())
        {
            if (auto sp2 = m_wapi.lock())
            {
                sp->UnregisterApi(sp2);
            }
        }
    }

    // This is visible to jscript
    void CallWalletAPI(const std::string& request)
    {
        if (auto sp = m_wapi.lock())
        {
            sp->CallWalletAPI(request);
        }
        else
        {
            LOG_ERROR() << "Failed to call API, wallet is not running";
        }
    }

    // This is visible to jscript
    void SetResultHandler(emscripten::val handler)
    {
        if (auto sp = m_wapi.lock())
        {
            sp->SetResultHandler(std::move(handler));
        }
        else
        {
            LOG_ERROR() << "Failed to set result callback, wallet is not running";
        }
    }

private:
    WalletClient2::WeakPtr m_wclient;
    WasmAppApi::WeakPtr m_wapi;
};

class AppAPICallback
{
public:
    AppAPICallback(WasmAppApi::WeakPtr sp)
        : m_Api(sp)
    {}

    void SendApproved(const std::string& request)
    {
        AssertMainThread();
        if (auto sp = m_Api.lock())
            sp->AnyThread_callWalletApiDirectly(request);
    }

    void SendRejected(const std::string& request)
    {
        AssertMainThread();
        if (auto sp = m_Api.lock())
            sp-> AnyThread_sendApiError(request, beam::wallet::ApiError::UserRejected, std::string());
    }

    void ContractInfoApproved(const std::string& request)
    {
        AssertMainThread();
        if (auto sp = m_Api.lock())
            sp->AnyThread_callWalletApiDirectly(request);
    }

    void ContractInfoRejected(const std::string& request)
    {
        AssertMainThread();
        if (auto sp = m_Api.lock())
            sp->AnyThread_sendApiError(request, beam::wallet::ApiError::UserRejected, std::string());
    }

private:
    WasmAppApi::WeakPtr m_Api;
};

class WasmWalletClient
    : public IWalletApiHandler
    , private WalletClient2::ICallbackHandler
{
public:
    WasmWalletClient(const std::string& node)
        : WasmWalletClient("headless.db", "anything", node)
    {
        SetHeadless(true);
    }

    WasmWalletClient(const std::string& dbName, const std::string& pass, const std::string& node)
        : m_Logger(beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG))
        , m_Reactor(io::Reactor::create())
        , m_DbPath(dbName)
        , m_Pass(pass)
        , m_Node(node)
    {
        wallet::g_AssetsEnabled = true;
    }


    void sendAPIResponse(const json& result) override
    {
        m_Client->SendResult(result);
    }

    uint32_t AddCallback(val&& callback)
    {
        for (uint32_t i = 0; i < m_Callbacks.size(); ++i)
        {
            auto& cb = m_Callbacks[i];
            if (cb.isNull())
            {
                cb = std::move(callback);
                return i;
            }
        }
        m_Callbacks.push_back(std::move(callback));
        return static_cast<uint32_t>(m_Callbacks.size() - 1);
    }

    int Subscribe(val callback)
    {
        for (uint32_t i = 0; i < m_Callbacks.size(); ++i)
        {
            auto& cb = m_Callbacks[i];
            if (cb.isNull())
            {
                cb = std::move(callback);
                return i;
            }
        }
        m_Callbacks.push_back(std::move(callback));
        return static_cast<uint32_t>(m_Callbacks.size() - 1);
    }

    void Unsubscribe(int key)
    {
        if (key == m_Callbacks.size() - 1)
        {
            m_Callbacks.pop_back();
        }
        else if (key < m_Callbacks.size() - 1)
        {
            m_Callbacks[key] = val::null();
        }
    }

    void SetSyncHandler(val handler)
    {
        AssertMainThread();
        m_SyncHandler = std::make_unique<val>(std::move(handler));
    }

    void SetApproveSendHandler(val handler)
    {
        AssertMainThread();
        m_ApproveSendHandler = std::make_unique<val>(std::move(handler));
    }

    void SetApproveContractInfoHandler(val handler)
    {
        AssertMainThread();
        m_ApproveContractInfoHandler = std::make_unique<val>(std::move(handler));
    }

    void ExecuteAPIRequest(const std::string& request)
    {
        AssertMainThread();
        if (!m_Client)
        {
            LOG_ERROR() << "Client is not running";
            return;
        }
        m_Client->getAsync()->makeIWTCall([this, request]()
        {
            if (!m_WalletApi)
            {
                ApiInitData initData;
                initData.walletDB = m_Client->GetWalletDB();
                initData.wallet = m_Client->getWallet();
                m_WalletApi = IWalletApi::CreateInstance(ApiVerCurrent, *this, initData);
            }

            m_WalletApi->executeAPIRequest(request.data(), request.size());
            return boost::none;
        },
            [](const boost::any&) {
        });
    }

    void StartWallet()
    {
        AssertMainThread();

        if (m_Client)
        {
            HandleException(std::runtime_error("The client is already running"));
            return;
        }

        try
        {
            if (IsHeadless())
            {
                auto r = io::Reactor::create();
                io::Reactor::Scope scope(*r);
                WalletDB::initNoKeeper(m_DbPath, m_Pass);
                s_Mounted = true;

                EM_ASM
                (
                    console.log("headless wallet created!");
                );
            }

            EnsureFSMounted();
            auto dbFunc = [path = m_DbPath, pass = m_Pass, dbPtr = std::make_shared<WalletDB::Ptr>()]()
            {
                if (!*dbPtr)
                {
                    *dbPtr = OpenWallet(path, pass);
                }
                return *dbPtr;
            };

            m_Client = std::make_shared<WalletClient2>(Rules::get(), dbFunc, m_Node, m_Reactor);
            m_Client->SetHandler(this);

            auto additionalTxCreators = std::make_shared<std::unordered_map<TxType, BaseTransaction::Creator::Ptr>>();
            additionalTxCreators->emplace(TxType::PushTransaction, std::make_shared<lelantus::PushTransaction::Creator>(dbFunc));

            if (!m_CurrentRecoveryFile.empty())
            {
                m_Client->getAsync()->importRecovery(std::move(m_CurrentRecoveryFile)); // m_CurrentRecoveryFile should be cleared 
            }
            m_Client->getAsync()->enableBodyRequests(true);
            m_Client->start({}, true, additionalTxCreators);
        }
        catch (const std::exception& ex)
        {
            HandleException(ex);
            return;
        }
    }

    void StopWallet(val handler = val::null())
    {
        AssertMainThread();
        if (!m_Client)
        {
            LOG_WARNING() << "The client is stopped";
            return;
        }
        m_Client->getAsync()->makeIWTCall([this]()
        {
            m_WalletApi.reset();
            return boost::none;
        },
            [](const boost::any&) {
        });
        std::weak_ptr<WalletClient2> wp = m_Client;
        Assert(wp.use_count() == 1);
        m_Client->Stop([wp, sp = std::move(m_Client), handler = std::move(handler)]() mutable
        {
            AssertMainThread();
            Assert(wp.use_count() == 1);
            sp.reset(); // release client, at this point destructor should be called and handler code can rely on that client is really stopped and destroyed
            if (!handler.isNull())
            {
                auto handlerPtr = std::make_unique<val>(std::move(handler));
                emscripten_dispatch_to_thread_async(
                    get_thread_id(),
                    EM_FUNC_SIG_VI,
                    &WasmWalletClient::DoCallbackOnMainThread,
                    nullptr,
                    handlerPtr.release());
            }
        });
    }

    void OnSyncProgress(int done, int total) override
    {
        AssertMainThread();
        if (m_SyncHandler && !m_SyncHandler->isNull())
        {
            (*m_SyncHandler)(done, total);
        }
    }

    void OnResult(const json& result) override
    {
        AssertMainThread();
        auto r = result.dump();
        for (auto& cb : m_Callbacks)
        {
            if (!cb.isNull())
            {
                cb(r);
            }
        }
    }

    void OnApproveSend(const std::string& request, const std::string& info, WasmAppApi::WeakPtr api) override
    {
        AssertMainThread();
        if (m_ApproveSendHandler && !m_ApproveSendHandler->isNull())
        {
            (*m_ApproveSendHandler)(request, info, AppAPICallback(api));
        }
    }

    void OnApproveContractInfo(const std::string& request, const std::string& info, const std::string& amounts, WasmAppApi::WeakPtr api) override
    {
        AssertMainThread();
        if (m_ApproveContractInfoHandler && !m_ApproveContractInfoHandler->isNull())
        {
            (*m_ApproveContractInfoHandler)(request, info, amounts, AppAPICallback(api));
        }
    }

    void OnImportRecoveryProgress(uint64_t done, uint64_t total) override
    {
        OnImportRecoveryProgress(val::null(), done, total);
    }

    void OnImportRecoveryProgress(val error, uint64_t done, uint64_t total)
    {
        AssertMainThread();
        if (m_RecoveryCallback && !m_RecoveryCallback->isNull())
        {
            (*m_RecoveryCallback)(error, static_cast<int>(done), static_cast<int>(total));
            if (done == total || !error.isNull())
            {
                LOG_DEBUG() << "Recovery done";
                m_RecoveryCallback.reset();
            }
        }
    }

    void OnWalletError(ErrorType e) override
    {
        if (e == ErrorType::ImportRecoveryError)
        {
            auto error = val::global("Error").new_(val("Failed to import recovery"));
            OnImportRecoveryProgress(error, 0, 0);
        }
    }


    void CreateAppAPI(const std::string& apiver, const std::string& minapiver, const std::string& appid, const std::string& appname, val cb)
    {
        AssertMainThread();

        std::string version;
        if (IWalletApi::ValidateAPIVersion(apiver))
        {
            version = apiver;
        }
        else if (IWalletApi::ValidateAPIVersion(minapiver))
        {
            version = minapiver;
        }

        if (version.empty())
        {
            const std::string errmsg = std::string("Unsupported api version requested: ") + apiver + "/" + minapiver;
            LOG_WARNING() << errmsg;
            cb(errmsg, val::undefined());
            return;
        }

        std::weak_ptr<WalletClient2> weak2 = m_Client;
        WasmAppApi::ClientThread_Create(m_Client.get(), apiver, appid, appname, false,
            [cb, weak2](WasmAppApi::Ptr wapi)
            {
                if (!wapi)
                {
                    const char* errmsg = "CreateAppAPI: failed to create API";
                    LOG_WARNING() << errmsg;
                    cb(errmsg, val::undefined());
                    return;
                }

                WasmAppApi::WeakPtr weakApi = wapi;
                wapi->SetPostToClientHandler(
                    [weak2](std::function<void (void)>&& func)
                    {
                        if (auto client2 = weak2.lock())
                        {
                            client2->postFunctionToClientContext([func=std::move(func)]() {
                                func();
                            });
                        }
                    }
                );

                wapi->SetContractConsentHandler(
                    [weak2, weakApi](const std::string& req, const std::string& info, const std::string& amoutns)
                    {
                        auto client2 = weak2.lock();
                        auto wapi = weakApi.lock();

                        if(client2 && wapi)
                        {
                            client2->ClientThread_ApproveContractInfo(req, info, amoutns, wapi);
                        }
                    }
                );

                wapi->SetSendConsentHandler(
                    [weak2, weakApi](const std::string& req, const std::string& info)
                    {
                        auto client2 = weak2.lock();
                        auto wapi = weakApi.lock();

                        if(client2 && wapi)
                        {
                            client2->ClientThread_ApproveSend(req, info, wapi);
                        }
                    }
                );
                if (auto client = weak2.lock())
                {
                    client->RegisterApi(wapi);
                    cb(val::undefined(), std::make_unique<WasmAppApiProxy>(client, wapi));               
                }

            }
        );
    }

    static constexpr std::string_view RecoveryFileName{ "recovery.bin" };

    void ImportRecovery(const std::string& buf, val&& callback)
    {
        AssertMainThread();
        if (m_RecoveryCallback)
        {
            LOG_WARNING() << "Recovery is in progress";
            return;
        }
        try
        {
            {
                std::ofstream s(RecoveryFileName.data(), ios_base::binary | ios_base::out | ios_base::trunc);
                s.write(reinterpret_cast<const char*>(buf.data()), buf.size());
            }
            if (!callback.isNull())
            {
                m_RecoveryCallback = std::make_unique<val>(std::move(callback));
            }
            m_CurrentRecoveryFile = RecoveryFileName;
        }
        catch (const std::exception& ex)
        {
            HandleException(ex);
        }
    }

    void ImportRecoveryFromFile(const std::string& fileName, val&& callback)
    {
        AssertMainThread();
        if (m_RecoveryCallback)
        {
            LOG_WARNING() << "Recovery is in progress";
            return;
        }
        try
        {
            if (!callback.isNull())
            {
                m_RecoveryCallback = std::make_unique<val>(std::move(callback));
            }
            m_CurrentRecoveryFile = fileName;
        }
        catch (const std::exception& ex)
        {
            HandleException(ex);
        }
    }

    static void DoCallbackOnMainThread(val* h)
    {
        std::unique_ptr<val> handler(h);
        if (!handler->isNull())
        {
            (*handler)();
        }
    }

    bool IsRunning() const
    {
        AssertMainThread();
        return m_Client && m_Client->isRunning();
    }

    void SetHeadless(bool headless)
    {
        AssertMainThread();
        m_Headless = headless;
    }

    bool IsHeadless() const
    {
        AssertMainThread();
        return m_Headless;
    }

    static bool IsAppSupported(const std::string& apiver, const std::string& apivermin)
    {
        return beam::wallet::IsAppSupported(apiver, apivermin);
    }

    static std::string GenerateAppID(const std::string& appname, const std::string& appurl)
    {
        return beam::wallet::GenerateAppID(appname, appurl);
    }

    static std::string GeneratePhrase()
    {
        return boost::join(createMnemonic(getEntropy()), " ");
    }

    static bool IsAllowedWord(const std::string& word)
    {
        return isAllowedWord(word);
    }

    static bool IsValidPhrase(const std::string& words)
    {
        return isValidMnemonic(string_helpers::split(words, ' '));
    }

    static std::string ConvertTokenToJson(const std::string& token)
    {
        return wallet::ConvertTokenToJson(token);
    }

    static std::string ConvertJsonToToken(const std::string& jsonParams)
    {
        return wallet::ConvertJsonToToken(jsonParams);
    }

    static void EnsureFSMounted()
    {
        if (!s_Mounted)
        {
            EM_ASM
            (
                throw new Error("File system should be mounted!");
            );
        }
    }

    static void CreateWallet(const std::string& seed, const std::string& dbName, const std::string& pass)
    {
        AssertMainThread();
        EnsureFSMounted();
        try
        {
            CreateDatabase(seed, dbName, pass);
        }
        catch (const std::exception& ex)
        {
            HandleException(ex);
        }
    }

    static void DeleteWallet(const std::string& dbName)
    {
        AssertMainThread();
        EnsureFSMounted();
        try
        {
            fs::remove(dbName);
        }
        catch (const std::exception& ex)
        {
            HandleException(ex);
        }
    }

    static bool IsInitialized(const std::string& dbName)
    {
        return WalletDB::isInitialized(dbName);
    }

    using CallbackResult = std::pair<std::shared_ptr<val>, bool>;
    static void ProsessCallbackOnMainThread(CallbackResult* pCallback)
    {
        std::unique_ptr<CallbackResult> cb(pCallback);
        if (!cb->first->isNull())
        {
            (*cb->first)(cb->second);
        }
    }

    static void CheckPasswordImpl(const std::string& dbName, const std::string& pass, std::shared_ptr<val> cb)
    {
        WalletDB::isValidPassword(dbName, SecString(pass));
        auto res = WalletDB::isValidPassword(dbName, SecString(pass));
        LOG_DEBUG() << __FUNCTION__ << TRACE(dbName) << TRACE(pass) << TRACE(res);
        auto cbPtr = std::make_unique<CallbackResult>(std::move(cb), res);
        emscripten_async_run_in_main_runtime_thread(
            EM_FUNC_SIG_VI,
            &WasmWalletClient::ProsessCallbackOnMainThread,
            reinterpret_cast<int>(cbPtr.release()));
    }


    static void CheckPassword(const std::string& dbName, const std::string& pass, val cb)
    {
        auto pcb = std::make_shared<val>(cb);
        MyThread(&WasmWalletClient::CheckPasswordImpl, dbName, pass, pcb).detach();
    }

    static IWalletDB::Ptr OpenWallet(const std::string& dbName, const std::string& pass)
    {
        try
        {
            Rules::get().UpdateChecksum();
            LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();
            return WalletDB::open(dbName, SecString(pass));
        }
        catch (const DatabaseException& ex)
        {
            LOG_ERROR() << ex.what();
            throw;
        }
    }

    static void MountFS(val cb)
    {
        AssertMainThread();
        s_MountCB = cb;
        if (s_Mounted)
        {
            LOG_WARNING() << "File systen is already mounted.";
            return;
        }
        EM_ASM
        (
            {
                FS.mkdir("/beam_wallet");
                FS.mount(IDBFS, {}, "/beam_wallet");
                console.log("mounting...");
                FS.syncfs(true, function(error)
                {
                    if (error == null) {
                        dynCall('vi', $0, [$1]);
                    }
                    else {
                        dynCall('vi', $0, [error]);
                    }
                });

            }, OnMountFS, &s_Null
        );
    }
private:
    static void OnMountFS(val* error)
    {
        s_Mounted = true;
        if (!s_MountCB.isNull())
        {
            s_MountCB(*error);
        }
        else
        {
            LOG_WARNING() << "Callback for mount is not set";
        }
    }

private:
    static val s_MountCB;
    static bool s_Mounted;
    static val s_Null;
    std::shared_ptr<Logger> m_Logger;
    io::Reactor::Ptr m_Reactor;
    std::string m_DbPath;
    std::string m_Pass;
    std::string m_Node;
    std::vector<val> m_Callbacks;
    std::unique_ptr<val> m_SyncHandler;
    std::unique_ptr<val> m_ApproveSendHandler;
    std::unique_ptr<val> m_ApproveContractInfoHandler;
    std::unique_ptr<val> m_RecoveryCallback;
    WalletClient2::Ptr m_Client;
    IWalletApi::Ptr m_WalletApi;
    bool m_Headless = false;
    std::string m_CurrentRecoveryFile;
};

val WasmWalletClient::s_MountCB = val::null();
bool WasmWalletClient::s_Mounted = false;
val WasmWalletClient::s_Null = val::null();

// Binding code
EMSCRIPTEN_BINDINGS()
{
    class_<WasmWalletClient>("WasmWalletClient")
        .constructor<const std::string&, const std::string&, const std::string&>() // db + pass + node
        .constructor<const std::string&>() // node only, imply headless
        .function("startWallet", &WasmWalletClient::StartWallet)
        .function("stopWallet", &WasmWalletClient::StopWallet)
        .function("isRunning", &WasmWalletClient::IsRunning)
        .function("isHeadless", &WasmWalletClient::IsHeadless)
        .function("sendRequest", &WasmWalletClient::ExecuteAPIRequest)
        .function("subscribe", &WasmWalletClient::Subscribe)
        .function("unsubscribe", &WasmWalletClient::Unsubscribe)
        .function("setSyncHandler", &WasmWalletClient::SetSyncHandler)
        .function("setApproveSendHandler", &WasmWalletClient::SetApproveSendHandler)
        .function("setApproveContractInfoHandler", &WasmWalletClient::SetApproveContractInfoHandler)
        .function("createAppAPI", &WasmWalletClient::CreateAppAPI)
        .function("importRecovery", &WasmWalletClient::ImportRecovery)
        .function("importRecoveryFromFile", &WasmWalletClient::ImportRecoveryFromFile)
        .class_function("IsAppSupported", &WasmWalletClient::IsAppSupported)
        .class_function("GenerateAppID", &WasmWalletClient::GenerateAppID)
        .class_function("GeneratePhrase", &WasmWalletClient::GeneratePhrase)
        .class_function("IsAllowedWord", &WasmWalletClient::IsAllowedWord)
        .class_function("IsValidPhrase", &WasmWalletClient::IsValidPhrase)
        .class_function("ConvertTokenToJson", &WasmWalletClient::ConvertTokenToJson)
        .class_function("ConvertJsonToToken", &WasmWalletClient::ConvertJsonToToken)
        .class_function("CreateWallet", &WasmWalletClient::CreateWallet)
        .class_function("MountFS", &WasmWalletClient::MountFS)
        .class_function("DeleteWallet", &WasmWalletClient::DeleteWallet)
        .class_function("IsInitialized", &WasmWalletClient::IsInitialized)
        .class_function("CheckPassword", &WasmWalletClient::CheckPassword)
        ;
    class_<WasmAppApiProxy>("AppAPI")
        .function("callWalletApi", &WasmAppApiProxy::CallWalletAPI)
        .function("setHandler", &WasmAppApiProxy::SetResultHandler)
        ;
    class_<AppAPICallback>("AppAPICallback")
        .function("sendApproved", &AppAPICallback::SendApproved)
        .function("sendRejected", &AppAPICallback::SendRejected)
        .function("contractInfoApproved", &AppAPICallback::ContractInfoApproved)
        .function("contractInfoRejected", &AppAPICallback::ContractInfoRejected)
        ;
}