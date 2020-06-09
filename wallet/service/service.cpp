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

#include "wallet/api/api_handler.h"
#include "wallet/core/wallet_db.h"
#include "wallet/core/simple_transaction.h"
#include "keykeeper/local_private_key_keeper.h"

#include <boost/uuid/uuid_generators.hpp>

#include "nlohmann/json.hpp"
#include "version.h"

#include "keykeeper/wasm_key_keeper.h"
#include "pipe.h"
#include "utils.h"

#include "websocket_server.h"
#include "node_connection.h"

using json = nlohmann::json;
static const size_t PACKER_FRAGMENTS_SIZE = 4096;

using namespace beam;
using namespace beam::wallet;

namespace beam::wallet
{
    namespace {
        std::string makeDBPath(const std::string& name)
        {
            const char *DB_FOLDER = "wallets";
            auto path = boost::filesystem::system_complete(DB_FOLDER);

            if (!boost::filesystem::exists(path))
            {
                boost::filesystem::create_directories(path);
            }

            std::string fname = std::string(name) + ".db";
            path /= fname;
            return path.string();
        }
    }

    WalletServiceApi::WalletServiceApi(IWalletServiceApiHandler& handler, ACL acl)
        : WalletApi(handler,
                false, // assets are forcibly disabled in wallet service
                acl)
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
            createWallet.pass = params["pass"].get<std::string>();
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'pass' parameter must be specified.", id };

        if (existsJsonParam(params, "ownerkey"))
        {
            createWallet.ownerKey = params["ownerkey"].get<std::string>();
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'ownerkey' parameter must be specified.", id };

        getHandler().onMessage(id, createWallet);
    }

    void WalletServiceApi::onOpenWalletMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        OpenWallet openWallet;

        if (existsJsonParam(params, "pass"))
        {
            openWallet.pass = params["pass"].get<std::string>();
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'pass' parameter must be specified.", id };

        if (existsJsonParam(params, "id"))
        {
            openWallet.id = params["id"].get<std::string>();
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

    void WalletServiceApi::onCalcChangeMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        ParameterReader reader(id, params);
        CalcChange message;
        message.amount = reader.readAmount("amount");
        getHandler().onMessage(id, message);
    }

    void WalletServiceApi::onChangePasswordMessage(const JsonRpcId& id, const nlohmann::json& params)
    {
        checkJsonParam(params, "new_pass", id);
        ChangePassword message;
        message.newPassword = params["new_pass"].get<std::string>();
        getHandler().onMessage(id, message);
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

    void WalletServiceApi::getResponse(const JsonRpcId& id, const CalcChange::Response& res, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", 
                {
                    {"change", res.change},
                    {"change_str", std::to_string(res.change)} // string representation 
                }
            }
        };
    }

    void WalletServiceApi::getResponse(const JsonRpcId& id, const ChangePassword::Response& res, json& msg)
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
        WalletApiServer(io::Reactor::Ptr reactor, bool withAssets, const io::Address& nodeAddr, uint16_t port, const std::string& allowedOrigin, bool withPipes)
            : WebSocketServer(reactor, port,
            [this, withAssets, reactor, &nodeAddr] (auto&& func)
            {
                return std::make_unique<ServiceApiConnection>(withAssets, nodeAddr, func, reactor, _walletMap);
            },
            [withPipes] ()
            {
                if (withPipes)
                {
                    Pipe syncPipe(Pipe::SyncFileDescriptor);
                    syncPipe.notifyListening();
                }
            },
            allowedOrigin)
        {
            if (withPipes)
            {
                _heartbeatPipe  = std::make_unique<Pipe>(Pipe::HeartbeatFileDescriptor);
                _heartbeatTimer = io::Timer::create(*reactor);
                _heartbeatTimer->start(Pipe::HeartbeatInterval, true, [this]() {
                    assert(_heartbeatPipe != nullptr);
                    _heartbeatPipe->notifyAlive();
                });
            }
        }

    private:
        io::Timer::Ptr _heartbeatTimer;
        std::unique_ptr<Pipe> _heartbeatPipe;

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
            : public PrivateKeyKeeper_WithMarshaller
            , public std::enable_shared_from_this<WasmKeyKeeperProxy>
        {
        public:

            WasmKeyKeeperProxy(Key::IPKdf::Ptr ownerKdf, IApiConnectionHandler& connection)
                : _ownerKdf(ownerKdf)
                , _connection(connection)
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
                return PrivateKeyKeeper_WithMarshaller::InvokeSync(x);
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
        };

        class MyApiConnection : public WalletApiHandler
        {
        public:
            MyApiConnection(IApiConnectionHandler* handler, IWalletData& walletData, WalletApi::ACL acl, bool withAssets)
                : WalletApiHandler(walletData, acl, withAssets)
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
            , private WalletApiHandler::IWalletData
            , public WebSocketServer::IHandler
            , public IApiConnectionHandler
        {
        public:
            ServiceApiConnection(bool withAssets, const io::Address& nodeAddr, WebSocketServer::SendMessageFunc sendFunc, io::Reactor::Ptr reactor, WalletMap& walletMap)
                : _apiConnection(this, *this, boost::none, withAssets)
                , _sendFunc(sendFunc)
                , _reactor(reactor)
                , _api(*this)
                , _walletMap(walletMap)
                , _nodeAddr(nodeAddr)
                , _withAssets(withAssets)
            {
                assert(_sendFunc);
            }

            virtual ~ServiceApiConnection() noexcept
            {
                try
                {
                    // _walletDB and _wallet should be destroyed in the context of _reactor
                    if (_walletDB || _wallet)
                    {
                        auto holder = std::make_shared<io::AsyncEvent::Ptr>();
                        *holder = io::AsyncEvent::create(*_reactor, 
                            [holder, walletDB = std::move(_walletDB), wallet = std::move(_wallet)]() mutable
                            {
                                holder.reset();
                            });
                        (*holder)->post();
                    }
                }
                catch (...)
                {

                }
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
                    else if (WalletApi::existsJsonParam(msg, "error"))
                    {
                        const auto& error = msg["error"];
                        LOG_ERROR() << "JSON RPC error id: " << error["id"] << " message: " << error["message"];
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
                try
                {
                    LOG_DEBUG() << "CreateWallet(id = " << id << ")";

                    beam::KeyString ks;

                    ks.SetPassword(data.pass);
                    ks.m_sRes = data.ownerKey;

                    std::shared_ptr<ECC::HKdfPub> ownerKdf = std::make_shared<ECC::HKdfPub>();

                    if (ks.Import(*ownerKdf))
                    {
                        auto keyKeeper = createKeyKeeper(ownerKdf);
                        auto dbName = generateWalletID(ownerKdf);
                        IWalletDB::Ptr walletDB = WalletDB::init(makeDBPath(dbName), SecString(data.pass), keyKeeper);

                        if (walletDB)
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
                catch (const DatabaseException& ex)
                {
                     _apiConnection.doError(id, ApiError::DatabaseError, ex.what());
                }
            }

            void onMessage(const JsonRpcId& id, const OpenWallet& data) override
            {
                try
                {
                    LOG_DEBUG() << "OpenWallet(id = " << id << ")";

                    auto it = _walletMap.find(data.id);
                    if (it == _walletMap.end())
                    {
                        _walletDB = WalletDB::open(makeDBPath(data.id), SecString(data.pass),
                                                   createKeyKeeperFromDB(data.id, data.pass));
                        _wallet = std::make_shared<Wallet>(_walletDB, _withAssets);

                        Key::IPKdf::Ptr pKey = _walletDB->get_OwnerKdf();
                        KeyString ks;
                        ks.SetPassword(Blob(data.pass.data(), static_cast<uint32_t>(data.pass.size())));
                        ks.m_sMeta = std::to_string(0);
                        ks.ExportP(*pKey);
                        _walletMap[data.id].ownerKey = ks.m_sRes;

                    } else if (auto wdb = it->second.walletDB.lock(); wdb)
                    {
                        _walletDB = wdb;
                        _wallet = it->second.wallet.lock();
                    } else
                    {
                        _walletDB = WalletDB::open(makeDBPath(data.id), SecString(data.pass),
                                                   createKeyKeeper(data.pass, it->second.ownerKey));
                        _wallet = std::make_shared<Wallet>(_walletDB, _withAssets);
                    }

                    if (!_walletDB)
                    {
                        _apiConnection.doError(id, ApiError::InternalErrorJsonRpc, "Wallet not opened.");
                        return;
                    }

                    _walletMap[data.id].walletDB = _walletDB;
                    _walletMap[data.id].wallet = _wallet;

                    LOG_DEBUG() << "wallet sucessfully opened...";

                    _wallet->ResumeAllTransactions();

                    auto nnet = std::make_shared<ServiceNodeConnection>(*_wallet);
                    nnet->m_Cfg.m_PollPeriod_ms = 0;//options.pollPeriod_ms.value;

                    if (nnet->m_Cfg.m_PollPeriod_ms)
                    {
                        LOG_INFO() << "Node poll period = " << nnet->m_Cfg.m_PollPeriod_ms << " ms";
                        uint32_t timeout_ms = std::max(Rules::get().DA.Target_s * 1000, nnet->m_Cfg.m_PollPeriod_ms);
                        if (timeout_ms != nnet->m_Cfg.m_PollPeriod_ms)
                        {
                            LOG_INFO() << "Node poll period has been automatically rounded up to block rate: "
                                       << timeout_ms << " ms";
                        }
                    }
                    uint32_t responceTime_s = Rules::get().DA.Target_s * wallet::kDefaultTxResponseTime;
                    if (nnet->m_Cfg.m_PollPeriod_ms >= responceTime_s * 1000)
                    {
                        LOG_WARNING() << "The \"--node_poll_period\" parameter set to more than "
                                      << uint32_t(responceTime_s / 3600) << " hours may cause transaction problems.";
                    }
                    nnet->m_Cfg.m_vNodes.push_back(_nodeAddr);
                    nnet->Connect();

                    auto wnet = std::make_shared<WalletNetworkViaBbs>(*_wallet, nnet, _walletDB);
                    _wallet->AddMessageEndpoint(wnet);
                    _wallet->SetNodeEndpoint(nnet);

                    // !TODO: not sure, do we need this id in the future
                    auto session = generateUid();
                    doResponse(id, OpenWallet::Response{session});
                }
                catch(const DatabaseNotFoundException& ex)
                {
                    _apiConnection.doError(id, ApiError::DatabaseNotFound, ex.what());
                }
                catch(const DatabaseException& ex)
                {
                    _apiConnection.doError(id, ApiError::DatabaseError, ex.what());
                }
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

            void onMessage(const JsonRpcId& id, const CalcChange& data) override
            {
                LOG_DEBUG() << "CalcChange(id = " << id << ")";

                auto coins = _walletDB->selectCoins(data.amount, Zero);
                Amount sum = 0;
                for (auto& c : coins)
                {
                    sum += c.m_ID.m_Value;
                }
                Amount change = (sum > data.amount) ? (sum - data.amount) : 0UL;
                doResponse(id, CalcChange::Response{change});
            }

            void onMessage(const JsonRpcId& id, const ChangePassword& data) override
            {
                LOG_DEBUG() << "ChangePassword(id = " << id << ")";

                _walletDB->changePassword(data.newPassword);

                doResponse(id, ChangePassword::Response{ });
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
                auto walletDB = WalletDB::open(makeDBPath(id), SecString(pass));
                Key::IPKdf::Ptr pKey = walletDB->get_OwnerKdf();
                return createKeyKeeper(pKey);
            }

            IPrivateKeyKeeper2::Ptr createKeyKeeper(Key::IPKdf::Ptr ownerKdf)
            {
                return std::make_shared<WasmKeyKeeperProxy>(ownerKdf, *this);
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
            io::Address _nodeAddr;
            bool _withAssets;
        };

    };
}

int main(int argc, char* argv[])
{
    using namespace beam;
    namespace po = boost::program_options;

    const char* LOG_FILES_DIR = "logs";
    const char* LOG_FILES_PREFIX = "service_";

    const auto path = boost::filesystem::system_complete(LOG_FILES_DIR);
    auto logger = beam::Logger::create(LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG, LOG_FILES_PREFIX, path.string());

    try
    {
        struct
        {
            uint16_t port;
            std::string nodeURI;
            Nonnegative<uint32_t> pollPeriod_ms;
            uint32_t logCleanupPeriod;
            std::string allowedOrigin;
            bool withPipes = false;
            bool withAssets = false;
        } options;

        io::Address node_addr;

        {
            po::options_description desc("Wallet API general options");
            desc.add_options()
                (cli::HELP_FULL, "list of all options")
                (cli::PORT_FULL, po::value(&options.port)->default_value(8080), "port to start server on")
                (cli::NODE_ADDR_FULL, po::value<std::string>(&options.nodeURI), "address of node")
                (cli::ALLOWED_ORIGIN, po::value<std::string>(&options.allowedOrigin)->default_value(""), "allowed origin")
                (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>(&options.logCleanupPeriod)->default_value(5), "old logfiles cleanup period(days)")
                (cli::NODE_POLL_PERIOD, po::value<Nonnegative<uint32_t>>(&options.pollPeriod_ms)->default_value(Nonnegative<uint32_t>(0)), "Node poll period in milliseconds. Set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks if it is less then it will be rounded up to block rate value.")
                (cli::WITH_SYNC_PIPES,  po::bool_switch(&options.withPipes)->default_value(false), "enable sync pipes")
                (cli::WITH_ASSETS, po::bool_switch(&options.withAssets)->default_value(false), "enable confidential assets transactions")
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

            ReadCfgFromFileCommon(vm, desc);
            ReadCfgFromFile(vm, desc, "wallet-api.cfg");
            vm.notify();

            clean_old_logfiles(LOG_FILES_DIR, LOG_FILES_PREFIX, beam::wallet::days2sec(options.logCleanupPeriod));

            getRulesOptions(vm);
            Rules::get().UpdateChecksum();
            LOG_INFO() << "Beam Wallet API " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
            LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();
            LOG_INFO() << "Current folder is " << boost::filesystem::current_path().string();
            
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

        const unsigned LOG_ROTATION_PERIOD_SEC = 12 * 60 * 60; // in seconds, 12 hours
        LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD_SEC, beam::wallet::days2sec(options.logCleanupPeriod));
        LOG_INFO() << "Log rotation: " <<  beam::wallet::sec2readable(LOG_ROTATION_PERIOD_SEC) << ". Log cleanup: " << options.logCleanupPeriod << " days.";
        LOG_INFO() << "Starting server on port " << options.port << ", sync pipes " << options.withPipes;

        WalletApiServer server(reactor, options.withAssets, node_addr, options.port, options.allowedOrigin, options.withPipes);
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
