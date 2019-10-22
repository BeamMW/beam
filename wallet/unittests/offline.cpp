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


#include "util.h"
#include "wallet/wallet_network.h"
#include "node/node.h"
#include "utility/logger.h"
#include <future>
#include <boost/filesystem.hpp>
#include "keykeeper/local_private_key_keeper.h"

namespace beam {
    using namespace wallet;
struct WalletDBObserver : IWalletDbObserver {
    void onCoinsChanged() {
        LOG_DEBUG() << _who << " " << __FUNCTION__;
    }
    void onTransactionChanged(ChangeAction, const std::vector<TxDescription>& )  {
        LOG_INFO() << _who << " QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ " << __FUNCTION__;
    }
    void onSystemStateChanged(const Block::SystemState::ID& stateID)  {
        LOG_INFO() << _who << " " << __FUNCTION__;
    }
    void onAddressChanged(ChangeAction action, const std::vector<WalletAddress>& items)  {
        LOG_INFO() << _who << " " << __FUNCTION__;
    }

    WalletDBObserver(std::string who) : _who(std::move(who)) {}

    std::string _who;
};

struct WaitHandle {
    io::Reactor::Ptr reactor;
    std::future<void> future;
};

struct WalletParams {
    IWalletDB::Ptr walletDB;
    io::Address nodeAddress;
    io::Reactor::Ptr reactor;
	WalletID sendFrom, sendTo;
};

WaitHandle run_wallet(const WalletParams& params) {
    WaitHandle ret;
    io::Reactor::Ptr reactor = io::Reactor::create();

    ret.future = std::async(
        std::launch::async,
        [&params, reactor]() {
            io::Reactor::Scope scope(*reactor);

            bool sender = !(params.sendTo == Zero);

//            if (sender) {
//                TxPeer receiverPeer = {};
////                 receiverPeer.m_walletID = sendTo;
//                params.walletDB->addPeer(receiverPeer);
//            }

            auto keyKeeper = std::make_shared<LocalPrivateKeyKeeper>(params.walletDB, params.walletDB->get_MasterKdf());
			Wallet wallet{ params.walletDB, keyKeeper, [](auto) { io::Reactor::get_Current().stop(); } };

			auto nnet = std::make_shared<proto::FlyClient::NetworkStd>(wallet);
			nnet->m_Cfg.m_vNodes.push_back(params.nodeAddress);
			nnet->Connect();

			wallet.AddMessageEndpoint(std::make_shared<WalletNetworkViaBbs>(wallet, nnet, params.walletDB, keyKeeper));
			wallet.SetNodeEndpoint(nnet);

            if (sender) {
                wallet.StartTransaction(CreateSimpleTransactionParameters()
                    .SetParameter(TxParameterID::MyID, params.sendFrom)
                    .SetParameter(TxParameterID::PeerID, params.sendTo)
                    .SetParameter(TxParameterID::Amount, Amount(1000000))
                    .SetParameter(TxParameterID::Fee, Amount(100000)));
            }

			io::Reactor::get_Current().run();
        }
    );

    ret.reactor = reactor;
    return ret;
}

struct NodeParams {
    io::Address nodeAddress;
    io::Address connectTo;
    ECC::uintBig walletSeed;
};

static const uint16_t NODE_PORT=20000;

WaitHandle run_node(const NodeParams& params) {
    WaitHandle ret;
    io::Reactor::Ptr reactor = io::Reactor::create();

    ret.future = std::async(
        std::launch::async,
        [&params, reactor]() {
            io::Reactor::Scope scope(*reactor);
            beam::Node node;

            node.m_Cfg.m_Listen.port(params.nodeAddress.port());
            node.m_Cfg.m_Listen.ip(params.nodeAddress.ip());
            node.m_Cfg.m_MiningThreads = 1;
            node.m_Cfg.m_VerificationThreads = 1;

			node.m_Keys.InitSingleKey(params.walletSeed);

            node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 500;

            if (!params.connectTo.empty()) {
                node.m_Cfg.m_Connect.push_back(params.connectTo);
            } else {
                ReadTreasury(node.m_Cfg.m_Treasury, "_sender_");
                LOG_INFO() << "Treasury blocs read: " << node.m_Cfg.m_Treasury.size();
            }

            LOG_INFO() << "starting a node on " << node.m_Cfg.m_Listen.port() << " port...";

            node.Initialize();

            reactor->run();
        }
    );

    ret.reactor = reactor;
    return ret;
}

void cleanup_files() {
    boost::filesystem::remove_all("_sender");
    boost::filesystem::remove_all("_sender_");
    boost::filesystem::remove_all("_sender_ks");
    boost::filesystem::remove_all("_receiver");
    boost::filesystem::remove_all("_receiver_ks");
}

void test_offline(bool twoNodes) {
    cleanup_files();
    using namespace beam;

    io::Address nodeAddress, node2Address;
    nodeAddress = io::Address::localhost().port(NODE_PORT);
    if (twoNodes) {
        node2Address = io::Address::localhost().port(NODE_PORT + 1);
    }

    NodeParams nodeParams, node2Params;
    WalletParams senderParams, receiverParams;

    nodeParams.nodeAddress = nodeAddress;
    senderParams.nodeAddress = nodeAddress;
    if (twoNodes) {
        node2Params.nodeAddress = node2Address;
        nodeParams.connectTo = node2Address;
        receiverParams.nodeAddress = node2Address;
    } else {
        receiverParams.nodeAddress = nodeAddress;
    }

    senderParams.reactor = io::Reactor::create();
    senderParams.walletDB = init_wallet_db("_sender", &nodeParams.walletSeed, senderParams.reactor);
    auto keyKeeper = std::make_shared<LocalPrivateKeyKeeper>(senderParams.walletDB, senderParams.walletDB->get_MasterKdf());

    receiverParams.reactor = io::Reactor::create();
    receiverParams.walletDB = init_wallet_db("_receiver", 0, receiverParams.reactor);

	WalletAddress wa = storage::createAddress(*senderParams.walletDB, keyKeeper);
	senderParams.walletDB->saveAddress(wa);
	senderParams.sendFrom = wa.m_walletID;
    wa = storage::createAddress(*senderParams.walletDB, keyKeeper);
    receiverParams.walletDB->saveAddress(wa);
	senderParams.sendTo = wa.m_walletID;

    WalletDBObserver senderObserver("AAAAAAAAAAAAAAAAAAAAAA"), receiverObserver("BBBBBBBBBBBBBBBBBBBBBB");

    senderParams.walletDB->Subscribe(&senderObserver);
    receiverParams.walletDB->Subscribe(&receiverObserver);

    WaitHandle node2WH;
    if (twoNodes) {
        node2WH = run_node(node2Params);
    }

    WaitHandle nodeWH = run_node(nodeParams);
    WaitHandle senderWH = run_wallet(senderParams);
    WaitHandle receiverWH = run_wallet(receiverParams);

    senderWH.future.get();
    receiverWH.future.get();
    nodeWH.reactor->stop();
    nodeWH.future.get();

    if (twoNodes) {
        node2WH.reactor->stop();
        node2WH.future.get();
    }
}

int test_one_node() {
    LOG_INFO() << "\n====================== 1 node 2 wallets";
    int ret = 0;
    try {
        test_offline(false);
    } catch (const std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what();
        ret = 1;
    }
    return ret;
}

int test_two_nodes() {
    LOG_INFO() << "\n====================== 2 nodes 2 wallets";
    int ret = 0;
    try {
        test_offline(true);
    } catch (const std::exception& e) {
        LOG_ERROR() << "Exception: " << e.what();
        ret = 2;
    }
    return ret;
}

} //namespace

int main(int argc, char* argv[]) {
    using namespace beam;

    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
    logger->set_header_formatter(
        [](char* buf, size_t maxSize, const char* timestampFormatted, const LogMessageHeader& header) -> size_t {
            if (header.line)
                return snprintf(buf, maxSize, "%c %s (%s, %d) ", loglevel_tag(header.level), timestampFormatted, header.func, (int)get_thread_id());
            return snprintf(buf, maxSize, "%c %s (%d) ", loglevel_tag(header.level), timestampFormatted, (int)get_thread_id());
        }
    );
	ECC::InitializeContext();

    Rules::get().FakePoW = true;

    /*
    io::Address nodeAddress;
    if (argc > 1) {
        nodeAddress.resolve(argv[1]);
    } else {
        nodeAddress = io::Address::localhost().port(NODE_PORT);
    }
    if (nodeAddress.empty()) {
        return 255;
    }
    */

    int ret = test_one_node();
    ret += test_two_nodes();
    return ret;
}
