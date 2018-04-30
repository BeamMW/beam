#include "wallet/wallet.h"
#include "wallet/sender.h"
#include "wallet/receiver.h"

#include "coin.h"

#include <assert.h>
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
    void generateRandom(void* p, uint32_t n)
    {
        for (uint32_t i = 0; i < n; i++)
            ((uint8_t*)p)[i] = (uint8_t)rand();
    }

    void setRandom(ECC::uintBig& x)
    {
        generateRandom(x.m_pData, sizeof(x.m_pData));
    }

    void setRandom(ECC::Scalar::Native& x)
    {
        ECC::Scalar s;
        while (true)
        {
            setRandom(s.m_Value);
            if (!x.Import(s))
                break;
        }
    }

    class BaseTestKeyChain : public IKeyChain
    {
    public:
        
        ECC::Scalar getNextKey()
        {
            return CoinData::keygen.next().get();
        }
        
        std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock)
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

        void store(const beam::Coin& coin)
        {

        }

        void update(const std::vector<beam::Coin>& coins)
        {

        }

        void remove(const std::vector<beam::Coin>& coins)
        {

        }

    protected:
        std::vector<beam::Coin> m_coins;
    };

    class TestKeyChain : public BaseTestKeyChain
    {
    public:
        TestKeyChain()
        {
            m_coins.emplace_back(ECC::Scalar::Native(200U), 5);
            m_coins.emplace_back(ECC::Scalar::Native(201U), 2);
            m_coins.emplace_back(ECC::Scalar::Native(202U), 3);
        }
    };

    class TestKeyChain2 : public BaseTestKeyChain
    {
    public:
        TestKeyChain2()
        {
            m_coins.emplace_back(ECC::Scalar::Native(300U), 1);
            m_coins.emplace_back(ECC::Scalar::Native(301U), 3);
        }
    };

    static const char* WalletName = "wallet.dat";
    static const char* TestPassword = "test password";
    class TestKeyChainIntegration : public IKeyChain
    {
    public:

        TestKeyChainIntegration()
        {
            std::ofstream os;
            os.open(WalletName, std::ofstream::binary);

            addCoin(os, 4);
            addCoin(os, 3);
            addCoin(os, 2);

            os.close();
        }

        ECC::Scalar getNextKey()
        {
            return CoinData::keygen.next().get();
        }

        std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool lock)
        {
            std::vector<beam::Coin> res;

            std::ifstream is;
            is.open(WalletName, std::ofstream::binary);

            size_t offset = 0;

            while(true)
            {
                std::unique_ptr<CoinData> coin(CoinData::recover(is, offset, TestPassword));

                if(coin) 
                    res.push_back(*coin);
                else break;

                offset += SIZE_COIN_DATA;
            }

            is.close();

            return res;
        }

        void store(const beam::Coin& coin)
        {

        }

        void update(const std::vector<beam::Coin>& coins)
        {

        }

        void remove(const std::vector<beam::Coin>& coins)
        {

        }
        
    private:

        void addCoin(std::ofstream& os, const ECC::Amount& amount)
        {
            CoinData coin(amount);
            coin.write(os, TestPassword);
        }
    };

    template<typename KeychainImpl>
    IKeyChain::Ptr createKeyChain()
    {
        return std::static_pointer_cast<IKeyChain>(std::make_shared<KeychainImpl>());
    }

    struct TestGateway : wallet::sender::IGateway
                       , wallet::receiver::IGateway
    {
        void sendTxInitiation(wallet::sender::InvitationData::Ptr) override
        {
            cout << "sent tx initiation message\n";
        }

        void sendTxConfirmation(wallet::sender::ConfirmationData::Ptr) override
        {
            cout << "sent senders's tx confirmation message\n";
        }

        void sendChangeOutputConfirmation() override
        {
            cout << "sent change output confirmation message\n";
        }

        void sendTxConfirmation(wallet::receiver::ConfirmationData::Ptr) override
        {
            cout << "sent recever's tx confirmation message\n";
        }

        void registerTx(const Uuid& txId, TransactionPtr) override
        {
            cout << "sent tx registration request\n";
        }

        void sendTxRegistered(const Uuid& txId) override
        {
            cout << "sent tx registration completed \n";
        }

        void removeSender(const Uuid&)
        {

        }

        void removeReceiver(const Uuid&)
        {

        }
    };

    struct IOLoop
    {
        using Task = function<void()>;
        IOLoop() : m_shutdown{ false }
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

        void run()
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

        deque<Task> m_tasks;
        mutex m_tasksMutex;
        condition_variable m_cv;
        atomic<bool> m_shutdown;
    };


    //
    // Test impl of the network io. The second thread isn't really needed, though this is much close to reality
    //
    struct TestNetworkBase : public NetworkIO
    {
        using Task = function<void()>;
        TestNetworkBase(IOLoop& mainLoop)
            : m_mainLoop(mainLoop)
            , m_thread{ [this] { m_networkLoop.run(); } }
        {

        }

        void shutdown()
        {
            m_networkLoop.shutdown();
            m_mainLoop.enqueueTask([this]()
            {
                m_thread.join();
                m_mainLoop.shutdown();
            });
        }

        void registerPeer(IWallet* walletPeer)
        {
            m_peers.push_back(walletPeer);
        }

        void enqueueNetworkTask(Task&& task)
        {
            m_networkLoop.enqueueTask([this, t = std::move(task)] ()
            {
                m_mainLoop.enqueueTask([t = std::move(t)](){t(); });
            });
        }

        vector<IWallet*> m_peers;
        IOLoop m_networkLoop;
        IOLoop& m_mainLoop;
        thread m_thread;
    };

    struct TestNetwork : public TestNetworkBase
    {
        TestNetwork(IOLoop& mainLoop) : TestNetworkBase{mainLoop}
        {}

        void sendTxInitiation(const Peer& peer, wallet::sender::InvitationData::Ptr data) override
        {
            cout << "[Sender] sendTxInitiation\n";
            enqueueNetworkTask([this, data] {m_peers[1]->handleTxInitiation(data); });
        }

        void sendTxConfirmation(const Peer& peer, wallet::sender::ConfirmationData::Ptr data) override
        {
            cout << "[Sender] sendTxConfirmation\n";
            enqueueNetworkTask([this, data] {m_peers[1]->handleTxConfirmation(data); });
        }

        void sendChangeOutputConfirmation(const Peer& peer) override
        {
            cout << "[Sender] sendChangeOutputConfirmation\n";
            enqueueNetworkTask([this, peer] {m_peers[0]->handleOutputConfirmation(peer); });
        }

        void sendTxConfirmation(const Peer& peer, wallet::receiver::ConfirmationData::Ptr data) override
        {
            cout << "[Receiver] sendTxConfirmation\n";
            enqueueNetworkTask([this, data] {m_peers[0]->handleTxConfirmation(data); });
        }

        void registerTx(const Peer& peer, const Uuid& txId, TransactionPtr tx) override
        {
            cout << "[Receiver] registerTx\n";
            enqueueNetworkTask([this, txId] {m_peers[1]->handleTxRegistration(txId); });
         
        }

        void sendTxRegistered(const Peer& peer, const Uuid& txId)
        {
            cout << "[Receiver] sendTxRegistered\n";
            enqueueNetworkTask([this, txId] {m_peers[0]->handleTxRegistration(txId); });
            shutdown();
        }
    };

    struct BadTestNetwork1 : public TestNetwork
    {
        BadTestNetwork1(IOLoop& mainLoop) : TestNetwork{ mainLoop }
        {}

        bool isFailed()
        {
            return (std::rand() % 2) == 0;
        }

        bool tryToSendTxFailed(const Uuid& txId, size_t peerId)
        {
            if (isFailed())
            {
                enqueueNetworkTask([this, txId, peerId]
                {
                    cout << "[Sender/Receiver] sendTxFailed\n";
                    m_peers[peerId]->handleTxFailed(txId);
                });
                return true;
            }
            return false;
        }

        void sendTxInitiation(const Peer& peer, wallet::sender::InvitationData::Ptr data) override
        {
            if (!tryToSendTxFailed(data->m_txId, 0))
            {
                TestNetwork::sendTxInitiation(peer, data);
            }
        }

        void sendTxConfirmation(const Peer& peer, wallet::sender::ConfirmationData::Ptr data) override
        {
            if (!tryToSendTxFailed(data->m_txId, 0))
            {
                TestNetwork::sendTxConfirmation(peer, data);
            }
        }

        void sendChangeOutputConfirmation(const Peer& peer) override
        {
           /* if (!tryToSendTxFailed(data->m_txId))
            {
                TestNetwork::sendTxInitiation(peer, data);
            }*/
        }

        void sendTxConfirmation(const Peer& peer, wallet::receiver::ConfirmationData::Ptr data) override
        {
            if (!tryToSendTxFailed(data->m_txId, 1))
            {
                TestNetwork::sendTxConfirmation(peer, data);
            };
        }

        void registerTx(const Peer& peer, const Uuid& txId, TransactionPtr) override
        {
            cout << "[Receiver] registerTx\n";
            //enqueueTask([this] {m_peers[1]->handleTxRegistration(Transaction()); });
            m_networkLoop.shutdown();
        }
    };
}

template<typename KeychainS, typename KeychainR>
void TestWalletNegotiation()
{
    cout << "\nTesting wallets negotiation...\n";

    Peer receiverLocator;
    IOLoop mainLoop;
    TestNetwork network{ mainLoop };
    Wallet sender(createKeyChain<KeychainS>(), network);
    Wallet receiver(createKeyChain<KeychainR>(), network);

    network.registerPeer(&sender);
    network.registerPeer(&receiver);

    sender.sendMoney(receiverLocator, 6);
    mainLoop.run();
}

void TestRollback()
{
   /* cout << "\nTesting wallets on bad network...\n";
    Peer receiverLocator;
    BadTestNetwork1 network;
    Wallet sender(createKeyChain(), network);
    Wallet receiver(createKeyChain2(), network);

    network.registerPeer(&sender);
    network.registerPeer(&receiver);

    sender.sendMoney(receiverLocator, 6);
    while (!network.isShutdown())
    {
        sender.pumpEvents();
        receiver.pumpEvents();
    }
    network.m_thread.join();*/
}

void TestFSM()
{
    cout << "\nTesting wallet's fsm...\nsender\n";
    TestGateway gateway;
    Uuid id;

    wallet::Sender s{ gateway, createKeyChain<TestKeyChain>(), id , 6};
    s.start();
    WALLET_CHECK(s.processEvent(wallet::Sender::TxInitCompleted{ std::make_shared<wallet::receiver::ConfirmationData>() }));
    WALLET_CHECK(s.processEvent(wallet::Sender::TxConfirmationCompleted()));
    WALLET_CHECK(s.processEvent(wallet::Sender::TxOutputConfirmCompleted()));

    cout << "\nreceiver\n";
    wallet::sender::InvitationData::Ptr initData = std::make_shared<wallet::sender::InvitationData>();
    initData->m_amount = 100;
    wallet::Receiver r{ gateway, createKeyChain<TestKeyChain>(), initData };
    r.start();
    WALLET_CHECK(!r.processEvent(wallet::Receiver::TxRegistrationCompleted()));
    WALLET_CHECK(r.processEvent(wallet::Receiver::TxFailed()));
    WALLET_CHECK(r.processEvent(wallet::Receiver::TxConfirmationCompleted()));
}

int main()
{
  //  TestFSM();
    TestWalletNegotiation<TestKeyChain, TestKeyChain2>();
    TestWalletNegotiation<TestKeyChainIntegration, TestKeyChain2>();
    TestRollback();

    return WALLET_CHECK_RESULT;
}
