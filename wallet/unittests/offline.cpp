#include "util.h"
#include "wallet/keystore.h"
#include "wallet/wallet_network.h"
#include "beam/node.h"
#include "utility/logger.h"
#include <future>
#include <boost/filesystem.hpp>

namespace beam {

struct WaitHandle {
    io::Reactor::Ptr reactor;
    std::future<void> future;
};

struct WalletParams {
    IKeyChain::Ptr keychain;
    IKeyStore::Ptr keystore;
    io::Address nodeAddress;
    PubKey sendFrom, sendTo;
};

WaitHandle run_wallet(const WalletParams& params) {
    WaitHandle ret;
    io::Reactor::Ptr reactor = io::Reactor::create();

    ret.future = std::async(
        std::launch::async,
        [&params, reactor]() {
            io::Reactor::Scope scope(*reactor);

            bool sender = !(params.sendTo == ECC::Zero);

            if (sender) {
                TxPeer receiverPeer = {};
//                 receiverPeer.m_walletID = sendTo;
                params.keychain->addPeer(receiverPeer);
            }

            auto wallet_io = std::make_shared<WalletNetworkIO>(params.nodeAddress, params.keychain, params.keystore, reactor);

            Wallet wallet{ params.keychain
                 , wallet_io
                 , [wallet_io](auto) { wallet_io->stop(); } };

            if (sender) {
                wallet.transfer_money(params.sendFrom, params.sendTo, 1000000, 100000, true);
            }

            wallet_io->start();
        }
    );

    ret.reactor = reactor;
    return ret;
}

struct NodeParams {
    io::Address nodeAddress;
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
            node.m_Cfg.m_MinerID = 0;
            node.m_Cfg.m_VerificationThreads = 1;
            node.m_Cfg.m_WalletKey.V = params.walletSeed;

            node.m_Cfg.m_TestMode.m_FakePowSolveTime_ms = 500;

            LOG_INFO() << "starting a node on " << node.m_Cfg.m_Listen.port() << " port...";

			ReadTreasury(node.m_Cfg.m_vTreasury, "_sender_");

            LOG_INFO() << "Treasury blocs read: " << node.m_Cfg.m_vTreasury.size();

            node.Initialize();

            reactor->run();
        }
    );

    ret.reactor = reactor;
    return ret;
}

} //namespace

int main(int argc, char* argv[]) {
    using namespace beam;

    boost::filesystem::remove_all("_sender");
    boost::filesystem::remove_all("_sender_");
    boost::filesystem::remove_all("_sender_ks");
    boost::filesystem::remove_all("_receiver");
    boost::filesystem::remove_all("_receiver_ks");

    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = Logger::create(logLevel, logLevel);
	ECC::InitializeContext();

    io::Address nodeAddress;
    if (argc > 1) {
        nodeAddress.resolve(argv[1]);
    } else {
        nodeAddress = io::Address::localhost().port(NODE_PORT);
    }
    if (nodeAddress.empty()) {
        return 255;
    }

    Rules::get().FakePoW = true;

    NodeParams nodeParams;
    WalletParams senderParams, receiverParams;

    nodeParams.nodeAddress = nodeAddress;
    senderParams.nodeAddress = nodeAddress;
    receiverParams.nodeAddress = nodeAddress;

    senderParams.keychain = init_keychain("_sender", &nodeParams.walletSeed);
    receiverParams.keychain = init_keychain("_receiver", 0);

    static const char KS_PASSWORD[] = "carbophos";

    IKeyStore::Options ksOptions;
    ksOptions.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
    ksOptions.fileName = "_sender_ks";
    senderParams.keystore = IKeyStore::create(ksOptions, KS_PASSWORD, sizeof(KS_PASSWORD));
    ksOptions.fileName = "_receiver_ks";
    receiverParams.keystore = IKeyStore::create(ksOptions, KS_PASSWORD, sizeof(KS_PASSWORD));

    senderParams.keystore->gen_keypair(senderParams.sendFrom, KS_PASSWORD, sizeof(KS_PASSWORD), true);
    receiverParams.keystore->gen_keypair(senderParams.sendTo, KS_PASSWORD, sizeof(KS_PASSWORD), true);

    WaitHandle nodeWH = run_node(nodeParams);
    WaitHandle senderWH = run_wallet(senderParams);
    WaitHandle receiverWH = run_wallet(receiverParams);

    senderWH.future.get();
    receiverWH.future.get();
    nodeWH.reactor->stop();
    nodeWH.future.get();
}
