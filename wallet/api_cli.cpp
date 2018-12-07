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
#include <map>

#include "utility/helpers.h"
#include "utility/io/timer.h"
#include "utility/io/tcpserver.h"
#include "utility/options.h"
#include "utility/io/json_serializer.h"

#include "p2p/line_protocol.h"

#include "wallet/wallet_db.h"
#include "wallet/wallet_network.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

static const unsigned LOG_ROTATION_PERIOD = 3 * 60 * 60 * 1000; // 3 hours

namespace beam
{
    struct ConnectionToServer 
    {
        virtual ~ConnectionToServer() = default;

        virtual void on_bad_peer(uint64_t from) = 0;
    };

    class WalletApiServer : public ConnectionToServer
    {
    public:
        WalletApiServer(IWalletDB::Ptr walletDB, io::Reactor& reactor, io::Address listenTo)
            : _reactor(reactor)
            , _bindAddress(listenTo)
            , _walletDB(walletDB)
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

    protected:

        void on_bad_peer(uint64_t from) override
        {
            _connections.erase(from);
        }

    private:

        void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
        {
            if (errorCode == 0) 
            {
                auto peer = newStream->peer_address();
                LOG_DEBUG() << "+peer " << peer;

                _connections[peer.u64()] = std::make_unique<Connection>(*this, _walletDB, peer.u64(), std::move(newStream));
            }

            LOG_DEBUG() << "on_stream_accepted";
        }

    private:
        class Connection : IWalletApiHandler
        {
        public:
            Connection(ConnectionToServer& owner, IWalletDB::Ptr walletDB, uint64_t id, io::TcpStream::Ptr&& newStream)
                : _owner(owner)
                , _id(id)
                , _stream(std::move(newStream))
                , _lineProtocol(BIND_THIS_MEMFN(on_raw_message), BIND_THIS_MEMFN(on_write))
                , _walletDB(walletDB)
                , _api(*this)
            {
                _stream->enable_keepalive(2);
                _stream->enable_read(BIND_THIS_MEMFN(on_stream_data));
            }

            void on_write(io::SharedBuffer&& msg) 
            {
                _stream->write(msg);
            }

            void onInvalidJsonRpc(const json& msg) override
            {
                LOG_DEBUG() << "onInvalidJsonRpc: " << msg;

                serialize_json_msg(_lineProtocol, msg);
            }

            void onMessage(int id, const CreateAddress& data) override 
            {
                LOG_DEBUG() << "CreateAddress(" << id << "," << data.metadata << ")";

                WalletAddress address = wallet::createAddress(_walletDB);

                json msg;
                CreateAddress::Response response{ address.m_walletID };
                _api.getResponse(id, response, msg);
                serialize_json_msg(_lineProtocol, msg);
            }


            void onMessage(int id, const Send& data) override {}
            void onMessage(int id, const Replace& data) override {}
            void onMessage(int id, const Status& data) override {}
            void onMessage(int id, const Split& data) override {}

            void onMessage(int id, const Balance& data) override 
            {
                LOG_DEBUG() << "Balance(" << id << "," << data.type << "," << std::to_string(data.address) << ")";

                json msg;
                Balance::Response response{wallet::getAvailable(_walletDB)};
                _api.getResponse(id, response, msg);
                serialize_json_msg(_lineProtocol, msg);
            }

            void onMessage(int id, const GetUtxo& data) override {}
            void onMessage(int id, const Lock& data) override {}
            void onMessage(int id, const Unlock& data) override {}
            void onMessage(int id, const CreateUtxo& data) override {}
            void onMessage(int id, const Poll& data) override {}

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
                    _owner.on_bad_peer(_id);
                    return false;
                }

                if (!_lineProtocol.new_data_from_stream(data, size)) 
                {
                    LOG_INFO() << "stream corrupted";
                    _owner.on_bad_peer(_id);
                    return false;
                }

                return true;
            }
        private:
            ConnectionToServer& _owner;
            uint64_t _id;
            io::TcpStream::Ptr _stream;
            LineProtocol _lineProtocol;
            IWalletDB::Ptr _walletDB;
            WalletApi _api;
        };

        io::Reactor& _reactor;
        io::TcpServer::Ptr _server;
        io::Address _bindAddress;
        std::map<uint64_t, std::unique_ptr<Connection>> _connections;
        IWalletDB::Ptr _walletDB;
    };
}

int main(int argc, char* argv[])
{
    using namespace beam;
    namespace po = boost::program_options;

    auto logger = Logger::create(LOG_LEVEL_VERBOSE, LOG_LEVEL_VERBOSE);

    try
    {
        struct
        {
            uint16_t port;
            std::string walletPath;
            std::string nodeURI;
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

            vm.notify();

            Rules::get().UpdateChecksum();
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

        io::Address listenTo = io::Address::localhost().port(options.port);
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

        WalletApiServer server(walletDB, *reactor, listenTo);

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
