#include "wallet/wallet.h"
#include <assert.h>

#include "wallet/sender.h"
#include "wallet/receiver.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

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
        void sendTxInitiation(wallet::Sender::InvitationData::Ptr) override
        {
            cout << "sent tx initiation message\n";
        }

        void sendTxConfirmation(wallet::Sender::ConfirmationData::Ptr) override
        {
            cout << "sent senders's tx confirmation message\n";
        }

        void sendChangeOutputConfirmation() override
        {
            cout << "sent change output confirmation message\n";
        }

        void sendTxConfirmation(wallet::Receiver::ConfirmationData::Ptr) override
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
        using Task = function<void()>;
        TestNetwork()
            : m_shutdown{false}
            , m_thread{ [this] {ioLoop(); } }
        {

        }

        void shutdown()
        {
            m_shutdown.store(true);
            m_cv.notify_all();
        }

        bool isShutdown() const
        {
            return m_shutdown.load();
        }

        void ioLoop()
        {
            while (!isShutdown())
            {
                Task task;
                {
                    unique_lock<mutex> lock(m_tasksMutex);
                    if (!m_tasks.empty())
                    {
                        task = move(m_tasks.front());
                        m_tasks.pop_front();
                    }
                    else
                    {
                        m_cv.wait(lock, [this] { return !m_tasks.empty() || isShutdown(); });
                    }
                }
                if (task)
                {
                    try
                    {
                        task();
                    }
                    catch (...)
                    {

                    }
                }
            }
        }

        void enqueueTask(Task&& task)
        {
            lock_guard<mutex> lock{ m_tasksMutex };
            m_tasks.push_back(move(task));
            m_cv.notify_one();
        }
        
        void registerPeer(IWallet* walletPeer)
        {
            m_peers.push_back(walletPeer);
        }

        void sendTxInitiation(const PeerLocator& locator, wallet::Sender::InvitationData::Ptr data) override
        {
            cout << "[Sender] sendTxInitiation\n";
            enqueueTask([this, data] {m_peers[1]->handleTxInitiation(data); });
        }

        void sendTxConfirmation(const PeerLocator& locator, wallet::Sender::ConfirmationData::Ptr data) override
        {
            cout << "[Sender] sendTxConfirmation\n";
            enqueueTask([this, data] {m_peers[1]->handleTxConfirmation(data); });
        }

        void sendChangeOutputConfirmation(const PeerLocator& locator) override
        {
            cout << "[Sender] sendChangeOutputConfirmation\n";
            //m_peers[1]->();
        }

        void sendTxConfirmation(const PeerLocator& locator, wallet::Receiver::ConfirmationData::Ptr data) override
        {
            cout << "[Receiver] sendTxConfirmation\n";
            enqueueTask([this, data] {m_peers[0]->handleTxConfirmation(data); });
        }

        void registerTx(const PeerLocator& locator, const Transaction&) override
        {
            cout << "[Receiver] registerTx\n";
            //enqueueTask([this] {m_peers[1]->handleTxRegistration(Transaction()); });
            shutdown();
        }

        vector<IWallet*> m_peers;
        deque<Task> m_tasks;
        mutex m_tasksMutex;
        condition_variable m_cv;
        atomic<bool> m_shutdown;
        thread m_thread;
    };
}

void TestWalletNegotiation()
{
    cout << "\nTesting wallets negotiation...\n";
    PeerLocator receiverLocator;
    TestNetwork network;
    Wallet sender( createKeyChain(), network );
    Wallet receiver( createKeyChain2(), network );

    network.registerPeer(&sender);
    network.registerPeer(&receiver);
    
    sender.sendMoney(receiverLocator, 6);
    while (!network.isShutdown())
    {
        sender.pumpEvents();
        receiver.pumpEvents();
    }
    network.m_thread.join();
}

void TestFSM()
{
    cout << "\nTesting wallet's fsm...\nsender\n";
    TestGateway gateway;
    Uuid id;
    wallet::Sender s{ gateway, id, createKeyChain(), 6};
    s.start();
    WALLET_CHECK(s.processEvent(wallet::Sender::TxInitCompleted()));
    WALLET_CHECK(s.processEvent(wallet::Sender::TxConfirmationCompleted()));
    WALLET_CHECK(s.processEvent(wallet::Sender::TxOutputConfirmCompleted()));
    
    cout << "\nreceiver\n";
    wallet::Sender::InvitationData::Ptr initData = std::make_unique<wallet::Sender::InvitationData>();
    initData->m_amount = 100;
    wallet::Receiver r{ gateway, std::move(initData)};
    r.start();
    WALLET_CHECK(!r.processEvent(wallet::Receiver::TxRegistrationCompleted()));
    WALLET_CHECK(r.processEvent(wallet::Receiver::TxConfirmationFailed()));
    WALLET_CHECK(r.processEvent(wallet::Receiver::TxConfirmationCompleted()));
}

int main()
{
    TestFSM();
    TestWalletNegotiation();
//    
    //Wallet::Config cfg;

    //Wallet::ToWallet::Shared receiver(std::make_shared<Wallet::ToWallet>());

    //Wallet sender(receiver, createKeyChain());

    //Wallet::Result result = sender.sendMoneyTo(cfg, 6);

    //assert(result);
    

    return WALLET_CHECK_RESULT;
}
