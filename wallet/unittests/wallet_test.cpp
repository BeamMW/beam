#include "wallet/wallet.h"
#include <assert.h>

#include "wallet/sender.h"
#include "wallet/receiver.h"
#include <iostream>

using namespace beam;
using namespace std;

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

    struct TestGateway : wallet::Sender::IGateway
                       , wallet::Receiver::IGateway
    {
        void sendTxInitiation(const wallet::Sender::InvitationData&) override
        {
            cout << "sent tx initiation message\n";
        }

        void sendTxConfirmation(const wallet::Sender::ConfirmationData&) override
        {
            cout << "sent senders's tx confirmation message\n";
        }

        void sendChangeOutputConfirmation() override
        {
            cout << "sent change output confirmation message\n";
        }

        void sendTxConfirmation(const wallet::Receiver::ConfirmationData&) override
        {
            cout << "sent recever's tx confirmation message\n";
        }

        void registerTx(const Transaction&) override
        {
            cout << "sent tx registration request\n";
        }
    };
}

int g_failureCount = 0;

void PrintFailure(const char* expression, const char* file, int line)
{
    cout << "\"" << expression << "\"" <<" assertion failed. File: "<< file << " at line: " << line << "\n";
    ++g_failureCount;
}

#define WALLET_CHECK(s) \
do {\
    if (!s) {\
        PrintFailure(#s, __FILE__, __LINE__);\
    }\
} while(false)\

#define WALLET_CHECK_RESULT g_failureCount ? -1 : 0;


int main()
{
    TestGateway gateway;
    Wallet::Config cfg;

    Wallet::ToWallet::Shared receiver(std::make_shared<Wallet::ToWallet>());

    Wallet sender(receiver, createKeyChain());

    Wallet::Result result = sender.sendMoneyTo(cfg, 6);

    //assert(result);

    wallet::Sender s{ gateway };
    WALLET_CHECK(s.processEvent(wallet::Sender::TxInitCompleted()));
    WALLET_CHECK(s.processEvent(wallet::Sender::TxConfirmationFailed()));

    wallet::Receiver r{ gateway };
    
    WALLET_CHECK(!r.processEvent(wallet::Receiver::TxRegistrationCompleted()));
    WALLET_CHECK(r.processEvent(wallet::Receiver::TxConfirmationFailed()));
    WALLET_CHECK(r.processEvent(wallet::Receiver::TxConfirmationCompleted()));

    return WALLET_CHECK_RESULT;
}
