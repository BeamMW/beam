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

#include "service.h"

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <map>
#include <queue>
#include <cstdio>

#include "utility/cli/options.h"
#include "utility/helpers.h"
#include "utility/io/timer.h"
#include "utility/io/json_serializer.h"
#include "utility/string_helpers.h"
#include "utility/log_rotation.h"

#include "wallet/api/api_connection.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/simple_transaction.h"
#include "keykeeper/local_private_key_keeper.h"

#include <boost/uuid/uuid_generators.hpp>

#include "nlohmann/json.hpp"
#include "version.h"

#include "keykeeper/wasm_key_keeper.h"
#include "pipe.h"

#include "websocket_server.h"

using json = nlohmann::json;

static const unsigned LOG_ROTATION_PERIOD = 3 * 60 * 60 * 1000; // 3 hours
static const size_t PACKER_FRAGMENTS_SIZE = 4096;

using namespace beam;
using namespace beam::wallet;

namespace beam::wallet
{
    io::Address node_addr;

    WalletServiceApi::WalletServiceApi(IWalletServiceApiHandler& handler, ACL acl)
        : WalletApi(handler, acl)
    {
#define REG_FUNC(api, name, writeAccess) \
        _methods[name] = {BIND_THIS_MEMFN(on##api##Message), writeAccess};

        WALLET_SERVICE_API_METHODS(REG_FUNC)

#undef REG_FUNC
    };

    IWalletServiceApiHandler& WalletServiceApi::getHandler() const
    {
        return static_cast<IWalletServiceApiHandler&>(_handler);
    }

    void WalletServiceApi::onCreateWalletMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        CreateWallet createWallet;

        if (existsJsonParam(params, "pass"))
        {
            createWallet.pass = params["pass"];
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'pass' parameter must be specified.", id };

        if (existsJsonParam(params, "ownerkey"))
        {
            createWallet.ownerKey = params["ownerkey"];
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'ownerkey' parameter must be specified.", id };

        getHandler().onMessage(id, createWallet);
    }

    void WalletServiceApi::onOpenWalletMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        OpenWallet openWallet;

        if (existsJsonParam(params, "pass"))
        {
            openWallet.pass = params["pass"];
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'pass' parameter must be specified.", id };

        if (existsJsonParam(params, "id"))
        {
            openWallet.id = params["id"];
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'id' parameter must be specified.", id };

        getHandler().onMessage(id, openWallet);
    }

    void WalletServiceApi::onPingMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        getHandler().onMessage(id, Ping{});
    }

    void WalletServiceApi::onReleaseMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        getHandler().onMessage(id, Release{});
    }

    void WalletServiceApi::getResponse(const JsonRpcId& id, const CreateWallet::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.id}
        };
    }

    void WalletServiceApi::getResponse(const JsonRpcId& id, const OpenWallet::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", res.session}
        };
    }

    void WalletServiceApi::getResponse(const JsonRpcId& id, const Ping::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "pong"}
        };
    }

    void WalletServiceApi::getResponse(const JsonRpcId& id, const Release::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "done"}
        };
    }
}

namespace
{
    void fail(boost::system::error_code ec, char const* what)
    {
        LOG_ERROR() << what << ": " << ec.message();
    }

    class WalletApiServer : public WebSocketServer
    {
    public:

        WalletApiServer(io::Reactor::Ptr reactor, uint16_t port)
            : WebSocketServer(reactor, port,
            [this, reactor] (auto&& func) {
                return std::make_unique<ServiceApiConnection>(func, reactor, _walletMap);
            },
            [] () {
                Pipe syncPipe(Pipe::SyncFileDescriptor);
                syncPipe.notify("LISTENING");
            })
            , _heartbeatPipe(Pipe::HeartbeatFileDescriptor)
        {
            _heartbeatTimer = io::Timer::create(*reactor);
            _heartbeatTimer->start(Pipe::HeartbeatInterval, true, [this] () {
                _heartbeatPipe.notify("alive");
            });
        }

    private:
        io::Timer::Ptr _heartbeatTimer;
        Pipe _heartbeatPipe;

    private:
        struct WalletInfo
        {
            std::string ownerKey;
            std::weak_ptr<Wallet> wallet;
            std::weak_ptr<IWalletDB> walletDB;

            WalletInfo(const std::string& ownerKey, Wallet::Ptr wallet, IWalletDB::Ptr walletDB)
                : ownerKey(ownerKey)
                , wallet(wallet)
                , walletDB(walletDB)
            {}
            WalletInfo() = default;
        };

        using WalletMap = std::unordered_map<std::string, WalletInfo>;

        WalletMap _walletMap;

        struct IApiConnectionHandler
        {
            virtual void serializeMsg(const json& msg) = 0;
            using KeyKeeperFunc = std::function<void(const json&)>;
            virtual void sendAsync(const json& msg, KeyKeeperFunc func) = 0;
        };

        class WasmKeyKeeperProxy 
            : public PrivateKeyKeeper_AsyncNotify
            , public std::enable_shared_from_this<WasmKeyKeeperProxy>
        {
        public:

            WasmKeyKeeperProxy(Key::IPKdf::Ptr ownerKdf, IApiConnectionHandler& connection, io::Reactor::Ptr reactor)
                : _ownerKdf(ownerKdf)
                , _connection(connection)
                , _reactor(reactor)
            {}
            virtual ~WasmKeyKeeperProxy(){}

            Status::Type InvokeSync(Method::get_Kdf& x) override
            {
                if (x.m_Root)
                {
                    assert(_ownerKdf);
                    x.m_pPKdf = _ownerKdf;
                    return Status::Success;
                }
                return PrivateKeyKeeper_AsyncNotify::InvokeSync(x);
            }

            void InvokeAsync(Method::get_Kdf& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "get_kdf"},
                    {"params",
                        {
                            {"root", x.m_Root},
                            {"child_key_num", x.m_iChild}
                        }
                    }
                };


                _connection.sendAsync(msg, [this, &x, h](const json& msg)
                    {
                        Status::Type s = GetStatus(msg);
                        if (s == Status::Success)
                        {
                            ByteBuffer buf = from_base64<ByteBuffer>(msg["pub_kdf"]);
                            ECC::HKdfPub::Packed* packed = reinterpret_cast<ECC::HKdfPub::Packed*>(&buf[0]);
                        
                            auto pubKdf = std::make_shared<ECC::HKdfPub>();
                            pubKdf->Import(*packed);
                            x.m_pPKdf = pubKdf;
                        }
                        PushOut(s, h);
                    });
            }

            void InvokeAsync(Method::get_NumSlots& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "get_slots"}
                };

                _connection.sendAsync(msg, [this, &x, h](const json& msg)
                {
                    Status::Type s = GetStatus(msg);
                    if (s == Status::Success)
                    {
                        x.m_Count = msg["count"];
                    }
                    PushOut(s, h);
                });
            }

            void InvokeAsync(Method::CreateOutput& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "create_output"},
                    {"params",
                        {
                            {"scheme", to_base64(x.m_hScheme)},
                            {"id", to_base64(x.m_Cid)}
                        }
                    }
                };

                _connection.sendAsync(msg, [this, &x, h](const json& msg)
                    {
                        Status::Type s = GetStatus(msg);
                        if (s == Status::Success)
                        {
                            x.m_pResult = from_base64<Output::Ptr>(msg["result"]);
                        }
                        PushOut(s, h);
                    });
            }

            void InvokeAsync(Method::SignReceiver& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "sign_receiver"},
                    {"params",
                        {
                            {"inputs",    to_base64(x.m_vInputs)},
                            {"outputs",   to_base64(x.m_vOutputs)},
                            {"kernel",    to_base64(x.m_pKernel)},
                            {"non_conv",  x.m_NonConventional},
                            {"peer_id",   to_base64(x.m_Peer)},
                            {"my_id_key", to_base64(x.m_MyIDKey)}
                        }
                    }
                };

                _connection.sendAsync(msg, [this, &x, h](const json& msg)
                    {
                        Status::Type s = GetStatus(msg);
                        if (s == Status::Success)
                        {
                            GetMutualResult(x, msg);
                        }
                        PushOut(s, h);
                    });
            }

            void InvokeAsync(Method::SignSender& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "sign_sender"},
                    {"params",
                        {
                            {"inputs",    to_base64(x.m_vInputs)},
                            {"outputs",   to_base64(x.m_vOutputs)},
                            {"kernel",    to_base64(x.m_pKernel)},
                            {"non_conv",  x.m_NonConventional},
                            {"peer_id",   to_base64(x.m_Peer)},
                            {"my_id_key", to_base64(x.m_MyIDKey)},
                            {"slot",      x.m_Slot},
                            {"agreement", to_base64(x.m_UserAgreement)},
                            {"my_id",     to_base64(x.m_MyID)},
                            {"payment_proof_sig", to_base64(x.m_PaymentProofSignature)}
                        }
                    }
                };

                _connection.sendAsync(msg, [this, &x, h](const json& msg)
                    {
                        Status::Type s = GetStatus(msg);

                        if (s == Status::Success)
                        {
                            if (x.m_UserAgreement == Zero)
                            {
                                x.m_UserAgreement = from_base64<ECC::Hash::Value>(msg["agreement"]);
                                x.m_pKernel->m_Commitment = from_base64<ECC::Point>(msg["commitment"]);
                                x.m_pKernel->m_Signature.m_NoncePub = from_base64<ECC::Point>(msg["pub_nonce"]);
                            }
                            else
                            {
                                GetCommonResult(x, msg);
                            }
                        }
                        PushOut(s, h);
                    });
            }

            void InvokeAsync(Method::SignSplit& x, const Handler::Ptr& h) override
            {
                json msg =
                {
                    {WalletApi::JsonRpcHrd, WalletApi::JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "sign_split"},
                    {"params",
                        {
                            {"inputs",   to_base64(x.m_vInputs)},
                            {"outputs",  to_base64(x.m_vOutputs)},
                            {"kernel",   to_base64(x.m_pKernel)},
                            {"non_conv", x.m_NonConventional}
                        }
                    }
                };

                _connection.sendAsync(msg, [this, &x, h](const json& msg)
                    {
                        Status::Type s = GetStatus(msg);
                        if (s == Status::Success)
                        {
                            GetCommonResult(x, msg);
                        }
                        PushOut(s, h);
                    });
            }


            static void GetMutualResult(Method::TxMutual& x, const json& msg)
            {
                x.m_PaymentProofSignature = from_base64<ECC::Signature>(msg["payment_proof_sig"]);
                GetCommonResult(x, msg);
            }

            static void GetCommonResult(Method::TxCommon& x, const json& msg)
            {
                auto offset = from_base64<ECC::Scalar>(msg["offset"]);
                x.m_kOffset.Import(offset);
                x.m_pKernel = from_base64<TxKernelStd::Ptr>(msg["kernel"]);
            }

            static Status::Type GetStatus(const json& msg)
            {
                return msg["status"];
            }

        private:
            Key::IPKdf::Ptr _ownerKdf;
            IApiConnectionHandler& _connection;
            io::Reactor::Ptr _reactor;
        };

        class MyApiConnection : public ApiConnection
        {
        public:
            MyApiConnection(IApiConnectionHandler* handler, IWalletData& walletData, WalletApi::ACL acl)
                : ApiConnection(walletData, acl)
                , _handler(handler)
            {
            
            }

            void serializeMsg(const json& msg) override
            {
                _handler->serializeMsg(msg);
            }

        private:
            IApiConnectionHandler* _handler;
        };
        
        class ServiceApiConnection 
            : public IWalletServiceApiHandler
            , private ApiConnection::IWalletData
            , public WebSocketServer::IHandler
            , public IApiConnectionHandler
        {
        public:
            ServiceApiConnection(WebSocketServer::SendMessageFunc sendFunc, io::Reactor::Ptr reactor, WalletMap& walletMap)
                : _apiConnection(this, *this, boost::none)
                , _sendFunc(sendFunc)
                , _reactor(reactor)
                , _api(*this)
                , _walletMap(walletMap)
            {
                assert(_sendFunc);
            }

            virtual ~ServiceApiConnection()
            {
            }

            // IWalletData methods
            IWalletDB::Ptr getWalletDB() override
            {
                assert(_walletDB);
                return _walletDB;
            }

            Wallet& getWallet() override
            {
                assert(_wallet);
                return *_wallet;
            }

#ifdef BEAM_ATOMIC_SWAP_SUPPORT
            const IAtomicSwapProvider& getAtomicSwapProvider() const override
            {
                throw std::runtime_error("not supported");
            }
#endif  // BEAM_ATOMIC_SWAP_SUPPORT

            void serializeMsg(const json& msg) override
            {
                _sendFunc(msg.dump());
            }

            void sendAsync(const json& msg, KeyKeeperFunc func) override
            {
                _keeperCallbacks.push(std::move(func));
                serializeMsg(msg);
            }

            void processData(const std::string& data) override
            {
                try
                {
                    json msg = json::parse(data.c_str(), data.c_str() + data.size());

                    if (WalletApi::existsJsonParam(msg, "result"))
                    {
                        if (_keeperCallbacks.empty())
                            return;

                        _keeperCallbacks.front()(msg["result"]);
                        _keeperCallbacks.pop();
                    }
                    else
                    {
                        // !TODO: don't forget to cache this request
                        _api.parse(data.c_str(), data.size());
                    }
                }
                catch (const nlohmann::detail::exception & e)
                {
                    LOG_ERROR() << "json parse: " << e.what() << "\n";
                }
            }

            void onInvalidJsonRpc(const json& msg) override
            {
                _apiConnection.onInvalidJsonRpc(msg);
            }

            template<typename T>
            void doResponse(const JsonRpcId& id, const T& response)
            {
                json msg;
                _api.getResponse(id, response, msg);
                serializeMsg(msg);
            }

#define MESSAGE_FUNC(api, name, _) \
            void onMessage(const JsonRpcId& id, const wallet::api& data) override \
            { _apiConnection.onMessage(id, data); } 

            WALLET_API_METHODS(MESSAGE_FUNC)

#undef MESSAGE_FUNC

            void onMessage(const JsonRpcId& id, const CreateWallet& data) override
            {
                LOG_DEBUG() << "CreateWallet(id = " << id << ")";

                beam::KeyString ks;

                ks.SetPassword(data.pass);
                ks.m_sRes = data.ownerKey;

                std::shared_ptr<ECC::HKdfPub> ownerKdf = std::make_shared<ECC::HKdfPub>();

                if(ks.Import(*ownerKdf))
                {
                    auto keyKeeper = createKeyKeeper(ownerKdf);
                    auto dbName = generateWalletID(ownerKdf);
                    IWalletDB::Ptr walletDB = WalletDB::init(dbName + ".db", SecString(data.pass), keyKeeper);

                    if(walletDB)
                    {
                        _walletMap[dbName] = WalletInfo(data.ownerKey, {}, walletDB);
                        // generate default address
                        WalletAddress address;
                        walletDB->createAddress(address);
                        address.m_label = "default";
                        walletDB->saveAddress(address);

                        doResponse(id, CreateWallet::Response{dbName});
                        return;
                    }
                }

                _apiConnection.doError(id, ApiError::InternalErrorJsonRpc, "Wallet not created.");
            }

            void onMessage(const JsonRpcId& id, const OpenWallet& data) override
            {
                LOG_DEBUG() << "OpenWallet(id = " << id << ")";

                auto it = _walletMap.find(data.id);
                if (it == _walletMap.end())
                {
                    _walletDB = WalletDB::open(data.id + ".db", SecString(data.pass), createKeyKeeperFromDB(data.id, data.pass));
                    _wallet = std::make_shared<Wallet>(_walletDB);

                    Key::IPKdf::Ptr pKey = _walletDB->get_OwnerKdf();
                    KeyString ks;
                    ks.SetPassword(Blob(data.pass.data(), static_cast<uint32_t>(data.pass.size())));
                    ks.m_sMeta = std::to_string(0);
                    ks.ExportP(*pKey);
                    _walletMap[data.id].ownerKey = ks.m_sRes;

                }
                else if (auto wdb = it->second.walletDB.lock(); wdb)
                {
                    _walletDB = wdb;
                    _wallet = it->second.wallet.lock();
                }
                else
                {
                    _walletDB = WalletDB::open(data.id + ".db", SecString(data.pass), createKeyKeeper(data.pass, it->second.ownerKey));
                    _wallet = std::make_shared<Wallet>(_walletDB);
                }
                
                if(!_walletDB)
                {
                    _apiConnection.doError(id, ApiError::InternalErrorJsonRpc, "Wallet not opened.");
                    return;
                }

                _walletMap[data.id].walletDB = _walletDB;
                _walletMap[data.id].wallet = _wallet;

                LOG_INFO() << "wallet sucessfully opened...";

                _wallet->ResumeAllTransactions();

                auto nnet = std::make_shared<proto::FlyClient::NetworkStd>(*_wallet);
                nnet->m_Cfg.m_PollPeriod_ms = 0;//options.pollPeriod_ms.value;
            
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

                auto wnet = std::make_shared<WalletNetworkViaBbs>(*_wallet, nnet, _walletDB);
                _wallet->AddMessageEndpoint(wnet);
                _wallet->SetNodeEndpoint(nnet);

                // !TODO: not sure, do we need this id in the future
                auto session = generateUid();

                doResponse(id, OpenWallet::Response{session});
            }

            void onMessage(const JsonRpcId& id, const wallet::Ping& data) override
            {
                LOG_DEBUG() << "Ping(id = " << id << ")";
                doResponse(id, wallet::Ping::Response{});
            }

            void onMessage(const JsonRpcId& id, const Release& data) override
            {
                LOG_DEBUG() << "Release(id = " << id << ")";
                doResponse(id, Release::Response{});
            }

            static std::string generateWalletID(Key::IPKdf::Ptr ownerKdf)
            {
                Key::ID kid(Zero);
                kid.m_Type = ECC::Key::Type::WalletID;

                ECC::Point::Native pt;
                ECC::Hash::Value hv;
                kid.get_Hash(hv);
                ownerKdf->DerivePKeyG(pt, hv);
                PeerID pid;
                pid.Import(pt);
                return pid.str();
            }

            static std::string generateUid()
            {
                std::array<uint8_t, 16> buf{};
                {
                    boost::uuids::uuid uid = boost::uuids::random_generator()();
                    std::copy(uid.begin(), uid.end(), buf.begin());
                }

                return to_hex(buf.data(), buf.size());
            }

            IPrivateKeyKeeper2::Ptr createKeyKeeper(const std::string& pass, const std::string& ownerKey)
            {
                beam::KeyString ks;

                ks.SetPassword(pass);
                ks.m_sRes = ownerKey;

                std::shared_ptr<ECC::HKdfPub> ownerKdf = std::make_shared<ECC::HKdfPub>();

                if (ks.Import(*ownerKdf))
                {
                    return createKeyKeeper(ownerKdf);
                }
                return {};
            }

            IPrivateKeyKeeper2::Ptr createKeyKeeperFromDB(const std::string& id, const std::string& pass)
            {
                auto walletDB = WalletDB::open(id + ".db", SecString(pass));
                Key::IPKdf::Ptr pKey = walletDB->get_OwnerKdf();
                return createKeyKeeper(pKey);
            }

            IPrivateKeyKeeper2::Ptr createKeyKeeper(Key::IPKdf::Ptr ownerKdf)
            {
                return std::make_shared<WasmKeyKeeperProxy>(ownerKdf, *this, _reactor);
            }

        protected:
            MyApiConnection _apiConnection;
            WebSocketServer::SendMessageFunc _sendFunc;
            io::Reactor::Ptr _reactor;
            std::queue<KeyKeeperFunc> _keeperCallbacks;
            IWalletDB::Ptr _walletDB;
            Wallet::Ptr _wallet;
            WalletServiceApi _api;
            WalletMap& _walletMap;
        };

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
            std::string nodeURI;
            Nonnegative<uint32_t> pollPeriod_ms;
            uint32_t logCleanupPeriod;

        } options;

        {
            po::options_description desc("Wallet API general options");
            desc.add_options()
                (cli::HELP_FULL, "list of all options")
                (cli::PORT_FULL, po::value(&options.port)->default_value(8080), "port to start server on")
                (cli::NODE_ADDR_FULL, po::value<std::string>(&options.nodeURI), "address of node")
                (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>(&options.logCleanupPeriod)->default_value(5), "old logfiles cleanup period(days)")
                (cli::NODE_POLL_PERIOD, po::value<Nonnegative<uint32_t>>(&options.pollPeriod_ms)->default_value(Nonnegative<uint32_t>(0)), "Node poll period in milliseconds. Set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks if it is less then it will be rounded up to block rate value.")
            ;

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
            
            if (vm.count(cli::NODE_ADDR) == 0)
            {
                LOG_ERROR() << "node address should be specified";
                return -1;
            }

            if (!node_addr.resolve(options.nodeURI.c_str()))
            {
                LOG_ERROR() << "unable to resolve node address: `" << options.nodeURI << "`";
                return -1;
            }
        }

        io::Reactor::Ptr reactor = io::Reactor::create();
        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);

        LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD, 5);//options.logCleanupPeriod);

        LOG_INFO() << "Starting server on port " << options.port;
        WalletApiServer server(reactor, options.port);
        reactor->run();

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
