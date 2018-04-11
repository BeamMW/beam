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
            m_coins.emplace_back(200, 5);
            m_coins.emplace_back(201, 2);
            m_coins.emplace_back(202, 3);
       //     m_coins.push_back(std::make_unique<Coin>(203, 1));
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
