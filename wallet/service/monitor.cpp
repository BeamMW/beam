// Copyright 2020 The Beam Team
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

#include "monitor.h"

#include "websocket_server.h"
#include "wallet/core/wallet_network.h"
#include "wallet/core/default_peers.h"
#include "utility/logger.h"
#include "utility/cli/options.h"
#include "utility/log_rotation.h"
#include "version.h"
#include "pipe.h"
#include "utils.h"
#include "node_connection.h"

#include <memory>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <boost/intrusive/set.hpp>


using namespace beam;

namespace beam::wallet
{
    SbbsMonitorApi::SbbsMonitorApi(ISbbsMonitorApiHandler& handler, ACL acl)
        : Api(handler, acl)
    {
#define REG_FUNC(api, name, writeAccess) \
        _methods[name] = {BIND_THIS_MEMFN(on##api##Message), writeAccess};

        WALLET_MONITOR_API_METHODS(REG_FUNC)

#undef REG_FUNC
    }

    ISbbsMonitorApiHandler& SbbsMonitorApi::getHandler() const
    {
        return static_cast<ISbbsMonitorApiHandler&>(_handler);
    }

    void SbbsMonitorApi::getResponse(const JsonRpcId& id, const Subscribe::Response& data, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "done"}
        };
    }

    void SbbsMonitorApi::getResponse(const JsonRpcId& id, const UnSubscribe::Response& data, json& msg)
    {
        msg = json
        {
            {JsonRpcHrd, JsonRpcVerHrd},
            {"id", id},
            {"result", "done"}
        };
    }

    void SbbsMonitorApi::onSubscribeMessage(const JsonRpcId& id, const json& msg)
    {
        Subscribe message;

        if (existsJsonParam(msg, "address"))
        {
            message.address.FromHex(msg["address"]);
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'address' parameter must be specified.", id };

        if (existsJsonParam(msg, "privateKey"))
        {
            auto buffer = from_hex(msg["privateKey"]);

            ECC::Scalar s;
            Deserializer d;
            d.reset(buffer);
            d& s;

            if (message.privateKey.Import(s))
            {
                throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "invalid private key", id };
            }
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'privateKey' parameter must be specified.", id };

        if (existsJsonParam(msg, "expires"))
        {
            message.expires = msg["expires"];
        }
        else
        {
            message.expires = std::numeric_limits<Timestamp>::max();
        }

        getHandler().onMessage(id, message);
    }

    void SbbsMonitorApi::onUnSubscribeMessage(const JsonRpcId& id, const json& msg)
    {
        UnSubscribe message;

        if (existsJsonParam(msg, "address"))
        {
            message.address.FromHex(msg["address"]);
        }
        else throw jsonrpc_exception{ ApiError::InvalidJsonRpc, "'address' parameter must be specified.", id };

        getHandler().onMessage(id, message);
    }
}

namespace
{
    using namespace beam::wallet;
    namespace bi = boost::intrusive;

    struct IMonitorConnectionHandler
    {
        virtual void sendRequestAsync(const json& msg) = 0;
    };

    class Monitor
        : public proto::FlyClient
        , public proto::FlyClient::IBbsReceiver
        , public wallet::IWalletMessageConsumer
    {
    public:
        Monitor()
            : m_AddressExirationTimer(io::Timer::create(io::Reactor::get_Current()))
        {

        }

        void subscribe(const WalletID& address, const ECC::Scalar::Native& privateKey, Timestamp expirationTime)
        {
            auto addr = new AddressInfo;
            addr->m_Address = address;
            address.m_Channel.Export(addr->m_Channel);
            addr->m_PrivateKey = privateKey;
            addr->m_ExpirationTime = expirationTime;
            m_Channels.insert(*addr);
            m_Addresses.insert(*addr);
            LOG_DEBUG() << "Subscribed to " << std::to_string(address);

            startExpirationTimer();
        }

        void unSubscribe(const WalletID& address)
        {
            auto it = m_Addresses.find(address);
            if (it != m_Addresses.end())
            {
                DeleteAddress(&(*it));
                LOG_DEBUG() << "Unsubscribed from " << std::to_string(address);
            }
            else
            {
                LOG_WARNING() << "unSubscribe unsuccessful: there is no subscription to " << std::to_string(address);
            }
        }

        void setHandler(IMonitorConnectionHandler* handler)
        {
            m_ConnectionHandler = handler;
        }

    private:
        struct AddressInfo;
        void DeleteAddress(AddressInfo* addr)
        {
            m_Channels.erase(ChannelSet::s_iterator_to(*addr));
            m_Addresses.erase(AddressSet::s_iterator_to(*addr));
            delete addr;
        }

        void onExpirationTimer()
        {
            std::vector<AddressInfo*> addressedToDelete;
            auto t = getTimestamp();
            for (AddressInfo& addr : m_Addresses)
            {
                if (addr.m_ExpirationTime < t)
                {
                    addressedToDelete.push_back(&addr);
                }
            }
            for (auto* addr : addressedToDelete)
            {
                DeleteAddress(addr);
            }
            if (!m_Addresses.empty())
            {
                startExpirationTimer();
            }
        }

        void startExpirationTimer()
        {
            m_AddressExirationTimer->start(60000, false, [this] { onExpirationTimer(); });
        }

        void OnWalletMessage(const wallet::WalletID& peerID, const wallet::SetTxParameter& msg) override
        {
            if (msg.m_Type != TxType::Simple)
            {
                return;
            }

            TxFailureReason failReason = TxFailureReason::Unknown;
            if (msg.GetParameter<TxFailureReason>(TxParameterID::FailureReason, failReason))
            {
                json jsonMsg =
                {
                    {Api::JsonRpcHrd, Api::JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "new_message"},
                    {"params",
                        {
                            {"txtype",         "simple"},
                            {"txid",           std::to_string(msg.m_TxID)},
                            {"address",        std::to_string(peerID)},
                            {"failureReason",  static_cast<int>(failReason)}
                        }
                    }
                };
                sendAsync(jsonMsg);
            }
            else
            {
                beam::Amount amount = 0;
                msg.GetParameter<beam::Amount>(TxParameterID::Amount, amount);

                json jsonMsg =
                {
                    {Api::JsonRpcHrd, Api::JsonRpcVerHrd},
                    {"id", 0},
                    {"method", "new_message"},
                    {"params",
                        {
                            {"txtype",   "simple"},
                            {"txid",     std::to_string(msg.m_TxID)},
                            {"address",  std::to_string(peerID)},
                            {"amount",   amount}
                        }
                    }
                };
                sendAsync(jsonMsg);
            }
        }

        void OnMsg(proto::BbsMsg&& msg) override
        {
            LOG_DEBUG() << "new bbs message on channel: " << msg.m_Channel;

            auto itBbs = m_BbsTimestamps.find(msg.m_Channel);
            if (m_BbsTimestamps.end() != itBbs)
            {
                std::setmax(itBbs->second, msg.m_TimePosted);
            }
            else
            {
                m_BbsTimestamps[msg.m_Channel] = msg.m_TimePosted;
            }

            if (msg.m_Message.empty())
            {
                return;
            }

            auto range = m_Channels.lower_bound_range(msg.m_Channel);

            for (auto it = range.first; it != range.second; ++it)
            {
                ByteBuffer buf = msg.m_Message; // duplicate
                uint8_t* pMsg = &buf.front();
                uint32_t nSize = static_cast<uint32_t>(buf.size());

                if (!proto::Bbs::Decrypt(pMsg, nSize, it->m_PrivateKey))
                    continue;

                SetTxParameter msgWallet;
                bool bValid = false;

                try 
                {
                    Deserializer der;
                    der.reset(pMsg, nSize);
                    der& msgWallet;
                    bValid = true;
                }
                catch (const std::exception& ex)
                {
                    LOG_WARNING() << "BBS deserialization failed: " << ex.what();
                }

                if (bValid)
                {
                    WalletID wid;
                    OnWalletMessage(it->m_Address, msgWallet);
                    break;
                }
            }
        }

        Block::SystemState::IHistory& get_History() override 
        {
            return m_Headers;
        }

        void sendAsync(const json& msg)
        {
            if (m_ConnectionHandler)
            {
                m_ConnectionHandler->sendRequestAsync(msg);
            }
            else
            {
                LOG_WARNING() << "Cannot send notification, connection was closed";
            }
        }

    private:

        Block::SystemState::HistoryMap m_Headers;
        
        struct ChannelTag {};
        using ChannelHook = bi::set_base_hook<bi::tag<ChannelTag>>;

        struct AddressInfo 
            : bi::set_base_hook<>
            , ChannelHook
        {
            WalletID m_Address;
            BbsChannel m_Channel;
            ECC::Scalar::Native m_PrivateKey;
            Timestamp m_ExpirationTime;
        };

        struct ChannelKey
        {
            using type = BbsChannel;
            const type& operator()(const AddressInfo& addr) const 
            {
                return addr.m_Channel;
            }
        };

        struct AddressKey
        {
            using type = WalletID;
            const type& operator()(const AddressInfo& addr) const { return addr.m_Address; }
        };

        using ChannelSet = bi::multiset<AddressInfo, bi::key_of_value<ChannelKey>, bi::base_hook<ChannelHook>>;
        using AddressSet = bi::set<AddressInfo, bi::key_of_value<AddressKey>>;

        ChannelSet m_Channels;
        AddressSet m_Addresses;
        std::unordered_map<BbsChannel, Timestamp> m_BbsTimestamps;
        io::Timer::Ptr m_AddressExirationTimer;

        IMonitorConnectionHandler* m_ConnectionHandler;

    };

    class MonitorClientHandler
        : public ISbbsMonitorApiHandler
        , public IMonitorConnectionHandler
        , public WebSocketServer::ClientHandler
    {
    public:
        MonitorClientHandler(Monitor& monitor, WebSocketServer::SendFunc sendFunc)
            : m_monitor(monitor)
            , m_api(*this)
            , m_sendFunc(sendFunc)
        {
            m_monitor.setHandler(this);
        }

        ~MonitorClientHandler()
        {
            m_monitor.setHandler(nullptr);
        }

        void onMessage(const JsonRpcId& id, const Subscribe& data) override
        {
            LOG_DEBUG() << "Subscribe(id = " << id << ")";

            m_monitor.subscribe(data.address, data.privateKey, data.expires);
            doResponse(id, Subscribe::Response{});
        }

        void onMessage(const JsonRpcId& id, const UnSubscribe& data) override
        {
            LOG_DEBUG() << "UnSubscribe(id = " << id << ")";

            m_monitor.unSubscribe(data.address);
            doResponse(id, UnSubscribe::Response{});
        }

        template<typename T>
        void doResponse(const JsonRpcId& id, const T& response)
        {
            if (m_sendFunc)
            {
                json msg;
                m_api.getResponse(id, response, msg);
                m_sendFunc(msg.dump());
            }
        }

        void onInvalidJsonRpc(const json& msg) override
        {
            LOG_DEBUG() << "onInvalidJsonRpc: " << msg;
            sendAsync(msg);
        }

        void onWSDataReceived(const std::string& data) override
        {
            try
            {
                json msg = json::parse(data.c_str(), data.c_str() + data.size());

                if (WalletApi::existsJsonParam(msg, "result"))
                {
                    if (m_requests == 0)
                    {
                        LOG_WARNING() << "Unexpected JSON RPC response: " << msg.dump();
                        return;
                    }
                    --m_requests;
                }
                else if (WalletApi::existsJsonParam(msg, "error"))
                {
                    const auto& error = msg["error"];
                    LOG_ERROR() << "JSON RPC error id: " << error["id"] << " message: " << error["message"];
                }
                else
                {
                    m_api.parse(data.c_str(), data.size());
                }
            }
            catch (const nlohmann::detail::exception & e)
            {
                LOG_ERROR() << "json parse: " << e.what() << "\n";
            }
        }

        void sendRequestAsync(const json& msg) override
        {
            sendAsync(msg);
            ++m_requests;
        }

        void sendAsync(const json& msg)
        {
            if (m_sendFunc)
            {
                m_sendFunc(msg.dump());
            }
        }

        Monitor& m_monitor;
        SbbsMonitorApi m_api;
        WebSocketServer::SendFunc m_sendFunc;
        int m_requests = 0;
    };

    class Server
            : public wallet::WebSocketServer
    {
    public:
        Server(Monitor& monitor, io::Reactor::Ptr reactor, uint16_t port, bool withPipes)
            : WebSocketServer(reactor, port, "")
            , _monitor(monitor)
            , _reactor(std::move(reactor))
            , _withPipes (withPipes)
        {
            std::string name = "SBBS Monitor";
            LOG_INFO() << name << " alive log interval: " << msec2readable(getAliveLogInterval());
            _aliveLogTimer = io::Timer::create(*_reactor);
            _aliveLogTimer->start(getAliveLogInterval(), true, [name]() {
                logAlive(name);
            });
        }

        ~Server() = default;

    private:
        void onWSStart() override
        {
            if (_withPipes)
            {
                Pipe syncPipe(Pipe::SyncFileDescriptor);
                syncPipe.notifyListening();

                _heartbeatPipe = std::make_unique<Pipe>(Pipe::HeartbeatFileDescriptor);
                _heartbeatTimer = io::Timer::create(*_reactor);
                _heartbeatTimer->start(Pipe::HeartbeatInterval, true, [this]() {
                    assert(_heartbeatPipe != nullptr);
                    _heartbeatPipe->notifyAlive();
                });
            }
        }

        WebSocketServer::ClientHandler::Ptr onNewWSClient(WebSocketServer::SendFunc wsSend) override
        {
            return std::make_unique<MonitorClientHandler>(_monitor, wsSend);
        }

        Monitor& _monitor;
        io::Timer::Ptr _heartbeatTimer;
        io::Timer::Ptr _aliveLogTimer;
        std::unique_ptr<Pipe> _heartbeatPipe;
        io::Reactor::Ptr _reactor;
        bool _withPipes;
    };
}

int main(int argc, char* argv[])
{
    using namespace beam;
    namespace po = boost::program_options;

    const char* LOG_FILES_DIR = "logs";
    const char* LOG_FILES_PREFIX = "monitor_";

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
            bool withPipes;
        } options;

        io::Address nodeAddress;
        io::Reactor::Ptr reactor = io::Reactor::create();
        {
            po::options_description desc("Wallet API general options");
            desc.add_options()
                (cli::HELP_FULL, "list of all options")
                (cli::PORT_FULL, po::value(&options.port)->default_value(8080), "port to start server on")
                (cli::NODE_ADDR_FULL, po::value<std::string>(&options.nodeURI), "address of node")
                (cli::LOG_CLEANUP_DAYS, po::value<uint32_t>(&options.logCleanupPeriod)->default_value(5), "old logfiles cleanup period(days)")
                (cli::NODE_POLL_PERIOD, po::value<Nonnegative<uint32_t>>(&options.pollPeriod_ms)->default_value(Nonnegative<uint32_t>(0)), "Node poll period in milliseconds. Set to 0 to keep connection. Anyway poll period would be no less than the expected rate of blocks if it is less then it will be rounded up to block rate value.")
                (cli::WITH_SYNC_PIPES,  po::bool_switch(&options.withPipes)->default_value(false), "Enable or disable sync pipes")
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
            ReadCfgFromFile(vm, desc, "monitor.cfg");
            vm.notify();

            clean_old_logfiles(LOG_FILES_DIR, LOG_FILES_PREFIX, beam::wallet::days2sec(options.logCleanupPeriod));
            getRulesOptions(vm);

            Rules::get().UpdateChecksum();
            LOG_INFO() << "Beam Wallet SBBS Monitor " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
            LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();
            LOG_INFO() << "Current folder is " << boost::filesystem::current_path().string();

            #ifdef NDEBUG
            LOG_INFO() << "Log mode: Non-Debug";
            #else
            LOG_INFO() << "Log mode: Debug";
            #endif

            if (vm.count(cli::NODE_ADDR) == 0)
            {
                LOG_ERROR() << "node address should be specified";
                return -1;
            }

            if (!nodeAddress.resolve(options.nodeURI.c_str()))
            {
                LOG_ERROR() << "unable to resolve node address: `" << options.nodeURI << "`";
                return -1;
            }
        }

        io::Reactor::Scope scope(*reactor);
        io::Reactor::GracefulIntHandler gih(*reactor);

        const unsigned LOG_ROTATION_PERIOD_SEC = 12 * 60 * 60; // in seconds, 12 hours
        LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD_SEC, beam::wallet::days2sec(options.logCleanupPeriod));
        LOG_INFO() << "Log rotation: " <<  beam::wallet::sec2readable(LOG_ROTATION_PERIOD_SEC) << ". Log cleanup: " << options.logCleanupPeriod << " days.";
        LOG_INFO() << "Starting server on port " << options.port << ", sync pipes " << options.withPipes;
        
        Monitor monitor;
        MonitorNodeConnection nnet(monitor);
        nnet.m_Cfg.m_vNodes.push_back(nodeAddress);
        nnet.Connect();
 
        for (BbsChannel c = 0; c < proto::Bbs::s_MaxWalletChannels; ++c)
        {
            nnet.BbsSubscribe(c, getTimestamp(), &monitor);
        }
 
        Server server(monitor, reactor, options.port, options.withPipes);
        reactor->run();

        LOG_INFO() << "Beam Wallet SBBS Monitor. Done.";
    }
    catch (const std::exception & e)
    {
        LOG_ERROR() << "EXCEPTION: " << e.what();
    }
    catch (...)
    {
        LOG_ERROR() << "NON_STD EXCEPTION";
    }

    return 0;
}
