#include "wallet/wallet.h"
#include <assert.h>

using namespace beam;

int main()
{
    Wallet::Config cfg;

    Wallet::ToWallet::Shared receiver(std::make_shared<Wallet::ToWallet>());
    Wallet sender(receiver);

    Wallet::Result result = sender.sendMoneyTo(cfg, 2000);

    assert(result);
  
    return 0;
}
