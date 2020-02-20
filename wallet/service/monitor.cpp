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
    class Monitor
        : public proto::FlyClient
        , public proto::FlyClient::IBbsReceiver
        , public wallet::IWalletMessageConsumer
    {
    public:
        void subscribe(const WalletID& address, const ECC::Scalar::Native& privateKey)
        {
            auto addr = new AddressInfo;
            addr->m_Address = address;
            address.m_Channel.Export(addr->m_Channel);
            addr->m_PrivateKey = privateKey;
            m_Channels.insert(*addr);
            m_Addresses.insert(*addr);
        }

        void unSubscribe(const WalletID& address)
        {
            auto it = m_Addresses.find(address);
            if (it != m_Addresses.end())
            {
                m_Channels.erase(ChannelSet::s_iterator_to(*it));
                m_Addresses.erase(it);
                delete &(*it);
            }
        }

    private:

        void OnWalletMessage(const wallet::WalletID& peerID, const wallet::SetTxParameter& msg) override
        {
            LOG_INFO() << "new bbs message for peer: " << std::to_string(peerID);
        }

        void OnMsg(proto::BbsMsg&& msg) override
        {
            LOG_INFO() << "new bbs message on channel: " << msg.m_Channel;

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
                catch (const std::exception&)
                {
                    LOG_WARNING() << "BBS deserialization failed";
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

    };

    static const unsigned LOG_ROTATION_PERIOD_SEC = 3 * 60 * 60; // 3 hours

    class MonitorApiHandler 
        : public ISbbsMonitorApiHandler
        , public WebSocketServer::IHandler
    {
    public:
        MonitorApiHandler(Monitor& monitor, WebSocketServer::SendMessageFunc&& func)
            : m_monitor(monitor)
            , m_api(*this)
            , m_sendFunc(std::move(func))
        {
        }

        ~MonitorApiHandler()
        {}

        void onMessage(const JsonRpcId& id, const Subscribe& data)
        {
            LOG_DEBUG() << "Subscribe(id = " << id << ")";

            m_monitor.subscribe(data.address, data.privateKey);

            doResponse(id, Subscribe::Response{});
        }

        void onMessage(const JsonRpcId& id, const UnSubscribe& data)
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

        void onInvalidJsonRpc(const json& msg)
        {
            LOG_DEBUG() << "onInvalidJsonRpc: " << msg;
            if (m_sendFunc)
            {
                m_sendFunc(msg.dump());
            }
        }

        void processData(const std::string& data) override
        {
            try
            {
                json msg = json::parse(data.c_str(), data.c_str() + data.size());

                m_api.parse(data.c_str(), data.size());
            }
            catch (const nlohmann::detail::exception & e)
            {
                LOG_ERROR() << "json parse: " << e.what() << "\n";
            }
        }
        Monitor& m_monitor;
        SbbsMonitorApi m_api;
        WebSocketServer::SendMessageFunc m_sendFunc;
    };


    class Server : public wallet::WebSocketServer
    {
    public:
        Server(Monitor& monitor, io::Reactor::Ptr reactor, uint16_t port)
            : WebSocketServer(reactor, port,
                [&monitor](auto&& func)->IHandler::Ptr
                { return std::make_unique<MonitorApiHandler>(monitor, std::move(func)); }
            )
        {
        }
    };

}

int main(int argc, char* argv[])
{
    using namespace beam;
    namespace po = boost::program_options;

#define LOG_FILES_DIR "logs"
#define LOG_FILES_PREFIX "monitor_"

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
                std::ifstream cfg("monitor.cfg");

                if (cfg)
                {
                    po::store(po::parse_config_file(cfg, desc), vm);
                }
            }

            vm.notify();

            clean_old_logfiles(LOG_FILES_DIR, LOG_FILES_PREFIX, options.logCleanupPeriod);

            getRulesOptions(vm);

            Rules::get().UpdateChecksum();
            LOG_INFO() << "Beam Wallet SBBS Monitor " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
            LOG_INFO() << "Rules signature: " << Rules::get().get_SignatureStr();

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

        LogRotation logRotation(*reactor, LOG_ROTATION_PERIOD_SEC, options.logCleanupPeriod);

        LOG_INFO() << "Starting server on port " << options.port;
        
        Monitor monitor;
        proto::FlyClient::NetworkStd nnet(monitor);
        nnet.m_Cfg.m_vNodes.push_back(nodeAddress);
        nnet.Connect();
 
        for (BbsChannel c = 0; c < 1024; ++c)
        {
            nnet.BbsSubscribe(c, getTimestamp(), &monitor);
        }
 
        Server server(monitor, reactor, options.port);
        reactor->run();

        LOG_INFO() << "Done";
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
