#include "util.h"
#include "wallet/bbsutil.h"
#include "wallet/wallet_network.h"
#include "beam/node.h"
#include "utility/logger.h"
#include <future>

namespace beam {

struct WaitHandle {
    io::Reactor::Ptr reactor;
    std::future<void> future;
};

struct WalletParams {
    IKeyChain::Ptr keychain;
    io::Address nodeAddress;
    util::PubKey pubKey;
    util::PrivKey privKey;
};

WaitHandle run_wallet(const WalletParams& params, const util::PubKey& sendTo) {
    WaitHandle ret;
    io::Reactor::Ptr reactor;

    ret.future = std::async(
        std::launch::async,
        [&params, sendTo, reactor]() {
            bool sender = !(sendTo == ECC::Zero);

            if (sender) {
                TxPeer receiverPeer = {};
//                 receiverPeer.m_walletID = sendTo;
                params.keychain->addPeer(receiverPeer);
            }

            auto wallet_io = std::make_shared<WalletNetworkIO>(params.nodeAddress, params.keychain, reactor);
            wallet_io->listen_to_bbs_channel(util::channel_from_wallet_id(params.pubKey));

            Wallet wallet{ params.keychain
                 , wallet_io
                 , [wallet_io](auto) { wallet_io->stop(); } };

            if (sender) {
                wallet.transfer_money(sendTo, 1000000, 100000, true);
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
    io::Reactor::Ptr reactor;

    ret.future = std::async(
        std::launch::async,
        [&params, reactor]() {
            beam::Node node;

            node.m_Cfg.m_Listen.port(params.nodeAddress.port());
            node.m_Cfg.m_Listen.ip(params.nodeAddress.ip());
            node.m_Cfg.m_MiningThreads = 1;
            node.m_Cfg.m_MinerID = 0;
            node.m_Cfg.m_VerificationThreads = 1;
            node.m_Cfg.m_WalletKey.V = params.walletSeed;

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
    using namespace beam::util;

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

    WalletParams senderParams, receiverParams;
    gen_keypair(senderParams.privKey, senderParams.pubKey);
    gen_keypair(receiverParams.privKey, receiverParams.pubKey);
    senderParams.nodeAddress = nodeAddress;
    receiverParams.nodeAddress = nodeAddress;

    NodeParams nodeParams;
    nodeParams.nodeAddress = nodeAddress;

    // TODO temporary initialization
    senderParams.keychain = init_keychain("_sender", senderParams.pubKey, senderParams.privKey, &nodeParams.walletSeed);
    receiverParams.keychain = init_keychain("_receiver", receiverParams.pubKey, receiverParams.privKey, 0);

    WaitHandle nodeWH = run_node(nodeParams);
    WaitHandle senderWH = run_wallet(senderParams, receiverParams.pubKey);
    WaitHandle receiverWH = run_wallet(receiverParams, PubKey());

    senderWH.future.get();
    receiverWH.future.get();
    nodeWH.reactor->stop();
    nodeWH.future.get();
}
