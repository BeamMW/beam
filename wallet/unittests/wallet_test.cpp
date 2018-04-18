#include "wallet/wallet.h"
#include <assert.h>

#include "wallet/sender.h"
#include "wallet/receiver.h"
#include <iostream>

using namespace beam;
using namespace std;

int g_failureCount = 0;

void PrintFailure(const char* expression, const char* file, int line)
{
    cout << "\"" << expression << "\"" << " assertion failed. File: " << file << " at line: " << line << "\n";
    ++g_failureCount;
}

#define WALLET_CHECK(s) \
do {\
    if (!s) {\
        PrintFailure(#s, __FILE__, __LINE__);\
    }\
} while(false)\

#define WALLET_CHECK_RESULT g_failureCount ? -1 : 0;


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

    class TestKeyChain2 : public IKeyChain
    {
    public:
        TestKeyChain2()
        {
            m_coins.emplace_back(ECC::Scalar::Native(300U), 1);
            m_coins.emplace_back(ECC::Scalar::Native(301U), 3);
        }

        std::vector<beam::Coin> getCoins(const ECC::Amount& amount)
        {
            std::vector<beam::Coin> res;
            ECC::Amount t = 0;
            for (auto& c : m_coins)
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

    IKeyChain::Ptr createKeyChain2()
    {
        return std::static_pointer_cast<IKeyChain>(std::make_shared<TestKeyChain2>());
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

    struct TestNetwork : public NetworkIO
    {
        void registerPeer(IWallet* walletPeer)
        {
            m_peers.push_back(walletPeer);
        }

        void sendTxInitiation(const PeerLocator& locator, const wallet::Sender::InvitationData& data) override
        {
            m_peers[1]->handleTxInitiation(data);
        }

        void sendTxConfirmation(const PeerLocator& locator, const wallet::Sender::ConfirmationData& data) override
        {
            m_peers[1]->handleTxConfirmation(data);
        }

        void sendChangeOutputConfirmation(const PeerLocator& locator) override
        {
            //m_peers[1]->();
        }

        void sendTxConfirmation(const PeerLocator& locator, const wallet::Receiver::ConfirmationData& data) override
        {
            m_peers[0]->handleTxConfirmation(data);
        }

        void registerTx(const PeerLocator& locator, const Transaction&) override
        {

        }
        std::vector<IWallet*> m_peers;
    };
}

void TestWalletNegotiation()
{
    cout << "\nTesting wallets negotiation...\n";
    PeerLocator senderNodeLocator, receiverNodeLocator;
    PeerLocator receiverLocator;
    TestNetwork network;
    Wallet sender{ createKeyChain(), network };
    Wallet receiver{ createKeyChain2(), network };

    network.registerPeer(&sender);
    network.registerPeer(&receiver);
    
    sender.sendMoney(receiverLocator, 6);
}

void TestFSM()
{
    cout << "\nTesting wallet's fsm...\nsender\n";
    TestGateway gateway;
    Uuid id;
    wallet::Sender s{ gateway, id };
    s.start();
    WALLET_CHECK(s.processEvent(wallet::Sender::TxInitCompleted()));
    WALLET_CHECK(s.processEvent(wallet::Sender::TxConfirmationFailed()));
    cout << "\nreceiver\n";
    wallet::Receiver r{ gateway, id };
    r.start();
    WALLET_CHECK(!r.processEvent(wallet::Receiver::TxRegistrationCompleted()));
    WALLET_CHECK(r.processEvent(wallet::Receiver::TxConfirmationFailed()));
    WALLET_CHECK(r.processEvent(wallet::Receiver::TxConfirmationCompleted()));
}

int main()
{
    TestWalletNegotiation();
    TestFSM();
    //Wallet::Config cfg;

    //Wallet::ToWallet::Shared receiver(std::make_shared<Wallet::ToWallet>());

    //Wallet sender(receiver, createKeyChain());

    //Wallet::Result result = sender.sendMoneyTo(cfg, 6);

    //assert(result);
    

    return WALLET_CHECK_RESULT;
}
