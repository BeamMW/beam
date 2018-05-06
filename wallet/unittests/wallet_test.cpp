#include "wallet/wallet_network.h"
#include "wallet/wallet.h"

#include "coin.h"
#include "test_helpers.h"

#include <assert.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

using namespace beam;
using namespace std;

WALLET_TEST_INIT

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
                c.m_status = Coin::Locked;
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

                if (coin)
                {
                    coin->m_status = Coin::Locked;
                    res.push_back(*coin);
                }
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
        void send_tx_invitation(wallet::sender::InvitationData::Ptr) override
        {
            cout << "sent tx initiation message\n";
        }

        void send_tx_confirmation(wallet::sender::ConfirmationData::Ptr) override
        {
            cout << "sent senders's tx confirmation message\n";
        }

        void send_output_confirmation() override
        {
            cout << "sent change output confirmation message\n";
        }

        void on_tx_completed(const Uuid& txId) override
        {
            cout << __FUNCTION__ <<"\n";
        }

        void send_tx_confirmation(wallet::receiver::ConfirmationData::Ptr) override
        {
            cout << "sent recever's tx confirmation message\n";
        }

        void register_tx(wallet::receiver::RegisterTxData::Ptr) override
        {
            cout << "sent tx registration request\n";
        }

        void send_tx_registered(UuidPtr&&) override
        {
            cout << "sent tx registration completed \n";
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
    struct TestNetworkBase : public INetworkIO
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

        void send_tx_invitation(PeerId to, wallet::sender::InvitationData::Ptr&& data) override
        {
            cout << "[Sender] send_tx_invitation\n";
            enqueueNetworkTask([this, to, data] () mutable {m_peers[1]->handle_tx_invitation(to, move(data)); });
        }

        void send_tx_confirmation(PeerId to, wallet::sender::ConfirmationData::Ptr&& data) override
        {
            cout << "[Sender] send_tx_confirmation\n";
            enqueueNetworkTask([this, to, data] () mutable {m_peers[1]->handle_tx_confirmation(to, move(data)); });
        }

        void send_output_confirmation(PeerId to, None&&) override
        {
            cout << "[Sender] send_output_confirmation\n";
            enqueueNetworkTask([this, to] {m_peers[0]->handle_output_confirmation(to, None()); });
        }

        void send_tx_confirmation(PeerId to, wallet::receiver::ConfirmationData::Ptr&& data) override
        {
            cout << "[Receiver] send_tx_confirmation\n";
            enqueueNetworkTask([this, to, data] () mutable {m_peers[0]->handle_tx_confirmation(to, move(data)); });
        }

        void register_tx(PeerId to, wallet::receiver::RegisterTxData::Ptr&& data) override
        {
            cout << "[Receiver] register_tx\n";
            enqueueNetworkTask([this, to, data] {m_peers[1]->handle_tx_registration(to, make_unique<Uuid>(data->m_txId)); });
         
        }

        void send_tx_registered(PeerId to, UuidPtr&& txId) override
        {
            cout << "[Receiver] send_tx_registered\n";

            enqueueNetworkTask([this, to, txId] () mutable {m_peers[0]->handle_tx_registration(to, move(txId)); });
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

        bool tryToSendTxFailed(const Uuid& txId, PeerId peerId)
        {
            if (isFailed())
            {
                enqueueNetworkTask([this, txId, peerId]
                {
                    cout << "[Sender/Receiver] sendTxFailed\n";
                    m_peers[peerId]->handle_tx_failed(PeerId(), make_unique<Uuid>(txId));
                });
                return true;
            }
            return false;
        }

        void send_tx_invitation(PeerId to, wallet::sender::InvitationData::Ptr&& data) override
        {
            if (!tryToSendTxFailed(data->m_txId, 0))
            {
                TestNetwork::send_tx_invitation(to, move(data));
            }
        }

        void send_tx_confirmation(PeerId to, wallet::sender::ConfirmationData::Ptr&& data) override
        {
            if (!tryToSendTxFailed(data->m_txId, 0))
            {
                TestNetwork::send_tx_confirmation(to, move(data));
            }
        }

        void send_output_confirmation(PeerId to, None&&) override
        {
           /* if (!tryToSendTxFailed(data->m_txId))
            {
                TestNetwork::send_tx_invitation(peer, data);
            }*/
        }

        void send_tx_confirmation(PeerId to, wallet::receiver::ConfirmationData::Ptr&& data) override
        {
            if (!tryToSendTxFailed(data->m_txId, 1))
            {
                TestNetwork::send_tx_confirmation(to, move(data));
            };
        }

        void register_tx(PeerId to, wallet::receiver::RegisterTxData::Ptr&&) override
        {
            cout << "[Receiver] register_tx\n";
            //enqueueTask([this] {m_peers[1]->handle_tx_registration(Transaction()); });
            m_networkLoop.shutdown();
        }
    };
}

template<typename KeychainS, typename KeychainR>
void TestWalletNegotiation()
{
    cout << "\nTesting wallets negotiation...\n";

    PeerId receiver_id = 4;
    IOLoop mainLoop;
    TestNetwork network{ mainLoop };
    Wallet sender(createKeyChain<KeychainS>(), network);
    Wallet receiver(createKeyChain<KeychainR>(), network);

    network.registerPeer(&sender);
    network.registerPeer(&receiver);

    sender.set_node_id(5);
    receiver.set_node_id(9);

    sender.send_money(receiver_id, 6);
    mainLoop.run();
}

void TestRollback()
{

}

void TestFSM()
{
    cout << "\nTesting wallet's fsm...\nsender\n";
    TestGateway gateway;
    Uuid id;

    wallet::Sender s{ gateway, createKeyChain<TestKeyChain>(), id , 6, 1};
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

class TestNode : public IMsgHandler
{
public:
    TestNode(io::Address address)
        : m_protocol{0, 0, 1, *this, 200}
        , m_address{ address }
        , m_reactor{io::Reactor::create()}
        , m_server{io::TcpServer::create(m_reactor, m_address, BIND_THIS_MEMFN(on_stream_accepted))}
        , m_tag{ 0 }

    {
        m_protocol.add_message_handler<wallet::receiver::RegisterTxData, &TestNode::on_register_tx>(txRegisterCode, 1, 2000);
        m_protocol.add_message_handler<None, &TestNode::on_confirm_output>(txConfirmOutputCode, 1, 2000);
    }

    void start()
    {
        m_thread.start(BIND_THIS_MEMFN(thread_func));
    }

    void stop()
    {
        m_reactor->stop();
        wait();
    }

    void wait()
    {
        if (this_thread::get_id() != m_thread_id) {
            m_thread.join();
        }
    }

private:
    void thread_func()
    {
        m_thread_id = this_thread::get_id();
        m_reactor->run();
    }

    // IMsgHandler
    void on_protocol_error(uint64_t fromStream, ProtocolError error) override
    {
        assert(false && "NODE: on_protocol_error");
    }

    // protocol handler
    bool on_register_tx(uint64_t connectionId, wallet::receiver::RegisterTxData&& data)
    {
        send(connectionId, txRegisteredCode, data.m_txId);
        return true;
    }

    bool on_confirm_output(uint64_t connectionId, None&& data)
    {
        send(connectionId, txOutputConfirmedCode, None());
        return true;
    }

    template <typename T>
    void send(PeerId to, MsgType type, T&& data)
    {
        auto it = m_connections.find(to);
        if (it != m_connections.end()) {
            SerializedMsg msgToSend;
            m_protocol.serialize(msgToSend, type, data);
            it->second->write_msg(msgToSend); 
        }
        else {
            LOG_ERROR() << "No connection";
            // add some handling
        }
    }

    void on_connection_error(uint64_t fromStream, int errorCode) override
    {
        assert(false && "NODE: on_connection_error");
    }

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, int errorCode)
    {
        if (errorCode == 0) {
            LOG_DEBUG() << "Stream accepted";
            auto tag = ++m_tag;// id
            m_connections.emplace(tag, make_unique<Connection>(
                    m_protocol,
                    tag,
                    100,
                    std::move(newStream)));
        }
        else {
       //     on_connection_error(m_address.packed, errorCode);
        }
    }

    Protocol<TestNode> m_protocol;
    io::Address m_address;
    io::Reactor::Ptr m_reactor;
    io::TcpServer::Ptr m_server;

    std::map<uint64_t, std::unique_ptr<Connection>> m_connections;
    Thread m_thread;
    thread::id m_thread_id;
    uint64_t m_tag;
};

void TestP2PWalletNegotiation()
{
    cout << "\nTesting p2p wallets negotiation...\n";

    auto node_address = io::Address::localhost().port(32125);
    auto receiver_address = io::Address::localhost().port(32124);
    auto sender_address = io::Address::localhost().port(32123);

    TestNode node{ node_address };
    WalletNetworkIO sender_io{ sender_address};
    WalletNetworkIO receiver_io{ receiver_address };
    
    io::Reactor::Ptr main_reactor{ io::Reactor::create() };

    int completion_count = 2;

    auto action = [&] (const Uuid&) { // called in main thread
        if (!(--completion_count)) {
            node.stop();
            sender_io.stop();
            receiver_io.stop();
            main_reactor->stop();
        }
    };

    Wallet sender(createKeyChain<TestKeyChain>(), sender_io.get_network_proxy(), action );
    Wallet receiver(createKeyChain<TestKeyChain2>(), receiver_io.get_network_proxy(), action);

    NetworkToWalletBridge sender_bridge{ sender, main_reactor };
    NetworkToWalletBridge receiver_bridge{ receiver, main_reactor };

    sender_io.set_wallet_proxy(&sender_bridge);
    receiver_io.set_wallet_proxy(&receiver_bridge);

    sender_io.connect(node_address, [&sender](uint64_t tag, int status) {
        sender.set_node_id(tag);
    });

    receiver_io.connect(node_address, [&receiver](uint64_t tag, int status) {
        receiver.set_node_id(tag);
    });

    sender_io.connect(receiver_address, [&sender](uint64_t tag, int status) {
        sender.send_money(tag, 6);
    });

    sender_io.start();
    receiver_io.start();
    node.start();
    main_reactor->run();

    node.wait();
    sender_io.wait();
    receiver_io.wait();
}

int main()
{
    TestP2PWalletNegotiation();
    TestWalletNegotiation<TestKeyChain, TestKeyChain2>();
    TestWalletNegotiation<TestKeyChainIntegration, TestKeyChain2>();
    TestRollback();
    TestFSM();

    return WALLET_CHECK_RESULT;
}
