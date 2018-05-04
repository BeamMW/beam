//#include "wallet/sender.h"
//#include "wallet/receiver.h"

#include "p2p/protocol.h"
#include "p2p/connection.h"

#include "utility/logger.h"
#include "utility/bridge.h"
#include "utility/io/tcpserver.h"
#include "utility/io/timer.h"

#include "wallet/wallet.h"
#include "wallet/wallet_network.h"

#include "test_helpers.h"

WALLET_TEST_INIT

using namespace beam;
using namespace std;

void TestP2PNegotiation()
{
    /*WalletNetworkIO io;

    Wallet sender{ createKeyChain<KeychainS>(), io };
    Wallet receiver{ createKeyChain<KeychainR>(), io };*/
}


int main()
{
    return WALLET_CHECK_RESULT;
}