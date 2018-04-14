#include "wallet/wallet.h"
#include <assert.h>

using namespace beam;


namespace
{
    class TestKeyChain : public IKeyChain
    {
    public:
        TestKeyChain()
        {
            m_coins.emplace_back(ECC::Scalar::Native(200U), 5);
            m_coins.emplace_back(ECC::Scalar::Native(201U), 2);
            m_coins.emplace_back(ECC::Scalar::Native(202U), 3);
        }

        std::vector<beam::Coin> getCoins(const ECC::Amount& amount)
        {
            std::vector<beam::Coin> res;
            ECC::Amount t = 0;
            for(auto& c : m_coins)
            {
                t += c.m_amount;
                res.push_back(c); 
                if (t >= amount)
                {
                    break;
                }
            }
            return res;
        }
    private:
        std::vector<beam::Coin> m_coins;
    };

    IKeyChain::Ptr createKeyChain()
    {
        return std::static_pointer_cast<IKeyChain>(std::make_shared<TestKeyChain>());
    }
}

int main()
{
    Wallet::Config cfg;

    Wallet::ToWallet::Shared receiver(std::make_shared<Wallet::ToWallet>());

    Wallet sender(receiver, createKeyChain());

    Wallet::Result result = sender.sendMoneyTo(cfg, 6);

    assert(result);
  
    return 0;
}
