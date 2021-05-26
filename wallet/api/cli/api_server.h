// Copyright 2018-2021 The Beam Team
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

namespace beam
{

    class IWalletApiServer
    {
    public:
        virtual void closeConnection(uint64_t id) = 0;
    };

    class IServerConnection
    {
    public:
        typedef std::shared_ptr<IServerConnection> Ptr;
        virtual ~IServerConnection() {}
    };

    class WalletApiServer
        : public IWalletApiServer
    {
    public:
        WalletApiServer(const std::string& apiVersion, IWalletDB::Ptr walletDB,
            Wallet::Ptr wallet,
            proto::FlyClient::NetworkStd::Ptr nnet,
            io::Reactor& reactor,
            io::Address listenTo,
            bool useHttp,
            IWalletApi::ACL acl,
            const TlsOptions& tlsOptions,
            const std::vector<uint32_t>& whitelist)

            : _apiVersion(apiVersion)
            , _reactor(reactor)
            , _bindAddress(listenTo)
            , _useHttp(useHttp)
            , _tlsOptions(tlsOptions)
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

        void initSwapFeature(proto::FlyClient::INetwork& nnet, IWalletMessageEndpoint& wnet)
        {
            _swapsProvider = std::make_shared<ApiCliSwap>(_walletDB);
            _swapsProvider->initSwapFeature(nnet, wnet);
        }

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

        template<typename T>
        IServerConnection::Ptr createConnection(io::TcpStream::Ptr&& newStream)
        {
            if (!_walletData)
            {
                _walletData = std::make_unique<IWalletApi::InitData>();
                _walletData->walletDB = _walletDB;
                _walletData->wallet = _wallet;
                _walletData->swaps = _swapsProvider;
                _walletData->acl = _acl;
                _walletData->contracts = IShadersManager::CreateInstance(_wallet, _walletDB, _network);
            }

            return std::make_shared<T>(_apiVersion, *this, std::move(newStream), *_walletData);
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
        class TcpApiConnection
            : public IServerConnection
            , public IWalletApiHandler
        {
        public:
            TcpApiConnection(const std::string& apiVersion, IWalletApiServer& server, io::TcpStream::Ptr&& newStream, IWalletApi::InitData& walletData)
                : _server(server)
                , _stream(std::move(newStream))
                , _lineProtocol(BIND_THIS_MEMFN(on_raw_message), BIND_THIS_MEMFN(on_write))
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
            HttpApiConnection(const std::string& apiVersion, IWalletApiServer& server, io::TcpStream::Ptr&& newStream, IWalletApi::InitData& walletData)
                : _server(server)
                , _keepalive(false)
                , _msgCreator(2000)
                , _packer(PACKER_FRAGMENTS_SIZE)
            {
                _walletApi = IWalletApi::CreateInstance(apiVersion, *this, walletData);

                newStream->enable_keepalive(1);
                auto peer = newStream->peer_address();

                _connection = std::make_unique<HttpConnection>(
                    peer.u64(),
                    BaseConnection::inbound,
                    BIND_THIS_MEMFN(on_request),
                    1024 * 1024,
                    1024,
                    std::move(newStream)
                    );
            }

            void sendAPIResponse(const json& result) override
            {
                serialize_json_msg(_body, _packer, result);
                _keepalive = send(_connection, 200, "OK");
            }

        private:
            bool on_request(uint64_t id, const HttpMsgReader::Message& msg)
            {
                if ((msg.what != HttpMsgReader::http_message || !msg.msg) && msg.what != HttpMsgReader::message_too_long)
                {
                    LOG_DEBUG() << "-peer " << io::Address::from_u64(id) << " : " << msg.error_str();
                    _connection->shutdown();
                    _server.closeConnection(id);
                    return false;
                }

                if (msg.what == HttpMsgReader::message_too_long)
                {
                    _keepalive = send(_connection, 413, "Payload Too Large");
                }
                else if (msg.msg->get_path() != "/api/wallet")
                {
                    _keepalive = send(_connection, 404, "Not Found");
                }
                else
                {
                    _body.clear();

                    size_t size = 0;
                    auto data = msg.msg->get_body(size);

                    const auto asyncResult = _walletApi->executeAPIRequest(reinterpret_cast<const char*>(data), size);
                    _keepalive = asyncResult == ApiSyncMode::RunningAsync;
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
            bool                _keepalive;
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
        bool               _useHttp;
        TlsOptions         _tlsOptions;

        std::unordered_map<uint64_t, IServerConnection::Ptr> _connections;
        std::vector<uint64_t> _pendingToClose;
        IWalletApi::ACL _acl;
        std::vector<uint32_t> _whitelist;

        // specific for wallet API
        IWalletDB::Ptr _walletDB;
        Wallet::Ptr _wallet;
        proto::FlyClient::NetworkStd::Ptr _network;

        std::shared_ptr<ApiCliSwap> _swapsProvider;
        std::unique_ptr<IWalletApi::InitData> _walletData;

    };
}