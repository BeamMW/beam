#include "wallet/wallet.h"
#include <assert.h>

#include "wallet/sender.h"
#include "wallet/receiver.h"

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

    struct TestGateway : wallet::sender::IGateway
                       , wallet::receiver::IGateway
    {
        void sendTxInitiation(const wallet::sender::InvitationData&) override
        {

        }

        void sendTxConfirmation(const wallet::sender::ConfirmationData&) override
        {

        }

        void sendChangeOutputConfirmation() override
        {

        }

        void sendTxConfirmation(const wallet::receiver::ConfirmationData&) override
        {

        }

        void registerTx(const Transaction&) override
        {

        }
    };
}

int main()
{
    Wallet::Config cfg;

    Wallet::ToWallet::Shared receiver(std::make_shared<Wallet::ToWallet>());

    Wallet sender(receiver, createKeyChain());

    Wallet::Result result = sender.sendMoneyTo(cfg, 6);

    //assert(result);
    TestGateway gateway;
    wallet::Sender s{ gateway };
    s.m_fsm.start();
    s.m_fsm.process_event(wallet::Sender::TxInitCompleted());
    s.m_fsm.process_event(wallet::Sender::TxConfirmationFailed());
    s.m_fsm.stop();

    wallet::Receiver r{ gateway };
    r.m_fsm.start();
    r.m_fsm.process_event(wallet::Receiver::TxConfirmationFailed());
    r.m_fsm.process_event(wallet::Receiver::TxConfirmationCompleted());
    r.m_fsm.stop();

    return 0;
}
