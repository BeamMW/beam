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
#include "version.h"

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
        WalletApiServer(IWalletDB::Ptr walletDB, Wallet& wallet, WalletNetworkViaBbs& wnet, io::Reactor& reactor, io::Address listenTo)
            : _reactor(reactor)
            , _bindAddress(listenTo)
            , _walletDB(walletDB)
            , _wallet(wallet)
            , _wnet(wnet)
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

                _connections[peer.u64()] = std::make_unique<Connection>(*this, _walletDB, _wallet, _wnet, peer.u64(), std::move(newStream));
            }

            LOG_DEBUG() << "on_stream_accepted";
        }

    private:
        class Connection : IWalletApiHandler, IWalletDbObserver
        {
        public:
            Connection(ConnectionToServer& owner, IWalletDB::Ptr walletDB, Wallet& wallet, WalletNetworkViaBbs& wnet, uint64_t id, io::TcpStream::Ptr&& newStream)
                : _owner(owner)
                , _id(id)
                , _stream(std::move(newStream))
                , _lineProtocol(BIND_THIS_MEMFN(on_raw_message), BIND_THIS_MEMFN(on_write))
                , _walletDB(walletDB)
                , _wallet(wallet)
                , _api(*this)
                , _wnet(wnet)
            {
                _stream->enable_keepalive(2);
                _stream->enable_read(BIND_THIS_MEMFN(on_stream_data));

                _walletDB->subscribe(this);
            }

            virtual ~Connection()
            {
                _walletDB->unsubscribe(this);
            }

            void onCoinsChanged() override {}
            void onTransactionChanged(ChangeAction action, std::vector<TxDescription>&& items) override {}

            void onSystemStateChanged() override 
            {
                
            }

            void onAddressChanged() override {}

            void on_write(io::SharedBuffer&& msg) 
            {
                _stream->write(msg);
            }

            template<typename T>
            void doResponse(int id, const T& response)
            {
                json msg;
                _api.getResponse(id, response, msg);
                serialize_json_msg(_lineProtocol, msg);
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

                serialize_json_msg(_lineProtocol, msg);
            }

            void onInvalidJsonRpc(const json& msg) override
            {
                LOG_DEBUG() << "onInvalidJsonRpc: " << msg;

                serialize_json_msg(_lineProtocol, msg);
            }

            void onMessage(int id, const CreateAddress& data) override 
            {
                LOG_DEBUG() << "CreateAddress(id = " << id << " metadata = " << data.metadata << " lifetime = " << data.lifetime << ")";

                WalletAddress address = wallet::createAddress(*_walletDB);
                address.m_duration = data.lifetime * 60 * 60;

                _walletDB->saveAddress(address);

                _wnet.AddOwnAddress(address);

                doResponse(id, CreateAddress::Response{ address.m_walletID });
            }

            void onMessage(int id, const ValidateAddress& data) override
            {
                LOG_DEBUG() << "ValidateAddress( address = " << std::to_string(data.address) << ")";

                _walletDB->getAddress(data.address);

                doResponse(id, ValidateAddress::Response{ data.address.IsValid(), _walletDB->getAddress(data.address) ? true : false });
            }

            void onMessage(int id, const Send& data) override
            {
                LOG_DEBUG() << "Send(id = " << id << " amount = " << data.value << " fee = " << data.fee <<  " address = " << std::to_string(data.address) << ")";

                WalletAddress senderAddress = wallet::createAddress(*_walletDB);
                _walletDB->saveAddress(senderAddress);

                _wnet.AddOwnAddress(senderAddress);

                ByteBuffer message(data.comment.begin(), data.comment.end());

                auto txId = _wallet.transfer_money(senderAddress.m_walletID, data.address, data.value, data.fee, true, 120, std::move(message));

                if (txId)
                {
                    doResponse(id, Send::Response{ *txId });
                }
                else
                {
                    doError(id, INTERNAL_JSON_RPC_ERROR, "Transaction could not be created. Please look at logs.");
                }
            }

            void onMessage(int id, const Replace& data) override
            {
                methodNotImplementedYet(id);
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

                    wallet::getTxParameter(*_walletDB, tx->m_txId, wallet::TxParameterID::KernelProofHeight, result.kernelProofHeight);

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

                WalletAddress senderAddress = wallet::createAddress(*_walletDB);
                _walletDB->saveAddress(senderAddress);
                _wnet.AddOwnAddress(senderAddress);

                auto txId = _wallet.split_coins(senderAddress.m_walletID, data.coins, data.fee);

                if (txId)
                {
                    doResponse(id, Send::Response{ *txId });
                }
                else
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

            void onMessage(int id, const GetUtxo& data) override 
            {
                LOG_DEBUG() << "GetUtxo(id = " << id << ")";

                GetUtxo::Response response;
                _walletDB->visit([&response](const Coin& c)->bool
                {
                    response.utxos.push_back(c);
                    return true;
                });

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
                }

				wallet::Totals totals(*_walletDB);

                response.available = totals.Avail;
                response.receiving = totals.Incoming;
                response.sending = totals.Outgoing;
                response.maturing = totals.Maturing;

                response.locked = 0; // same as Outgoing?

                doResponse(id, response);
            }

            void onMessage(int id, const Lock& data) override
            {
                methodNotImplementedYet(id);
            }

            void onMessage(int id, const Unlock& data) override
            {
                methodNotImplementedYet(id);
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

                        wallet::getTxParameter(*_walletDB, tx.m_txId, wallet::TxParameterID::KernelProofHeight, item.kernelProofHeight);
                        res.resultList.push_back(item);
                    }
                }

                // filter transactions by status if provided
                if (data.filter.status)
                {
                    decltype(res.resultList) filteredList;

                    for (const auto& it : res.resultList)
                        if (it.tx.m_status == *data.filter.status)
                            filteredList.push_back(it);

                    res.resultList = filteredList;
                }

                // filter transactions by height if provided
                if (data.filter.height)
                {
                    decltype(res.resultList) filteredList;

                    for (const auto& it : res.resultList)
                        if (it.kernelProofHeight == *data.filter.height)
                            filteredList.push_back(it);

                    res.resultList = filteredList;
                }

                doResponse(id, res);
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
            void methodNotImplementedYet(int id)
            {
                doError(id, NOTFOUND_JSON_RPC, "Method not implemented yet.");
            }

        private:
            ConnectionToServer& _owner;
            uint64_t _id;
            io::TcpStream::Ptr _stream;
            LineProtocol _lineProtocol;
            IWalletDB::Ptr _walletDB;
            Wallet& _wallet;
            WalletApi _api;
            WalletNetworkViaBbs& _wnet;
        };

        io::Reactor& _reactor;
        io::TcpServer::Ptr _server;
        io::Address _bindAddress;
        std::map<uint64_t, std::unique_ptr<Connection>> _connections;
        IWalletDB::Ptr _walletDB;
        Wallet& _wallet;
        WalletNetworkViaBbs& _wnet;
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
            LOG_INFO() << "Beam Wallet API " << PROJECT_VERSION << " (" << BRANCH_NAME << ")";
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

        io::Address listenTo = io::Address().port(options.port);
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

        WalletApiServer server(walletDB, wallet, wnet, *reactor, listenTo);

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
