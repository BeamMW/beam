#include "util.h"
#include "wallet/bbsutil.h"
#include "wallet/wallet_network.h"
#include <future>

ECC::Initializer g_Initializer;

namespace beam {

struct WaitHandle {
    io::Reactor::Ptr reactor;
    std::future<void> future;
};

WaitHandle run_wallet(io::Address nodeAddress, IKeyChain::Ptr&& keychain, const util::PubKey& sendTo) {
    WaitHandle ret;
    io::Reactor::Ptr reactor;

    ret.future = std::async(
        std::launch::async,
        [keychain, sendTo, nodeAddress, reactor]() {
            bool sender = !(sendTo == ECC::Zero);

            if (sender) {
                TxPeer receiverPeer = {};
                receiverPeer.m_walletID = sendTo;
                keychain->addPeer(receiverPeer);
            }

            auto wallet_io = std::make_shared<WalletNetworkIO>(nodeAddress, keychain, reactor);
            Wallet wallet{ keychain
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

} //namespace

int main(int argc, char* argv[]) {
    using namespace beam;
    using namespace beam::util;

    io::Address nodeAddress;
    if (argc > 1) {
        nodeAddress.resolve(argv[1]);
    } else {
        nodeAddress = io::Address::localhost().port(20000);
    }
    if (nodeAddress.empty()) {
        return 255;
    }

    PubKey senderPubKey, receiverPubKey;
    PrivKey senderPrivKey, receiverPrivKey;
    gen_keypair(senderPrivKey, senderPubKey);
    gen_keypair(receiverPrivKey, receiverPubKey);

    // TODO temporary initialization
    auto senderKeychain = init_keychain("_sender", senderPubKey, senderPrivKey, true);
    auto receiverKeychain = init_keychain("_receiver", receiverPubKey, receiverPrivKey, false);

    WaitHandle senderWH = run_wallet(nodeAddress, std::move(senderKeychain), receiverPubKey);

}
