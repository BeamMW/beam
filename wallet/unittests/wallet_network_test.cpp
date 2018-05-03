#include "wallet/wallet.h"
#include "wallet/sender.h"
#include "wallet/receiver.h"

#include "p2p/protocol.h"
#include "p2p/connection.h"

#include "utility/logger.h"
#include "utility/bridge.h"
#include "utility/io/tcpserver.h"
#include "utility/io/timer.h"

#include "test_helpers.h"

WALLET_TEST_INIT

void TestP2PNegotiation()
{}


int main()
{
    return WALLET_CHECK_RESULT;
}