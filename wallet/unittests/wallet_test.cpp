#include "wallet/wallet_network.h"
#include "wallet/wallet.h"

#include "test_helpers.h"

#include <assert.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include "sqlite_keychain.hpp"
#include "core/proto.h"

using namespace beam;
using namespace std;
using namespace ECC;

WALLET_TEST_INIT

namespace
{
    class BaseTestKeyChain : public IKeyChain
    {
    public:

        ECC::Scalar::Native calcKey(const Coin&) const
        {
            return ECC::Scalar::Native();
        }

        std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool /*lock*/)
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

        void store(beam::Coin& ) override {}
        void store(std::vector<beam::Coin>& ) {}
        void update(const std::vector<beam::Coin>& ) override {}
        void remove(const std::vector<beam::Coin>& ) override {}
        void remove(const beam::Coin& ) override {}
        void visit(std::function<bool(const beam::Coin& coin)> func) override {}
		void setVarRaw(const char* name, const void* data, int size) override {}
		int getVarRaw(const char* name, void* data) const override { return 0; }
		void setSystemStateID(const Block::SystemState::ID& stateID) override {};
		bool getSystemStateID(Block::SystemState::ID& stateID) const override { return false; };

        Height getCurrentHeight() const override
        {
            return 134;
        }

    protected:
        std::vector<beam::Coin> m_coins;
    };

    class TestKeyChain : public BaseTestKeyChain
    {
    public:
        TestKeyChain()
        {
            m_coins.emplace_back(5);
            m_coins.emplace_back(2);
            m_coins.emplace_back(3);
        }
    };

    class TestKeyChain2 : public BaseTestKeyChain
    {
    public:
        TestKeyChain2()
        {
            m_coins.emplace_back(1);
            m_coins.emplace_back(3);
        }
    };

    struct SqliteKeychainInt : SqliteKeychain
    {
        SqliteKeychainInt()
        {
            Block::SystemState::ID id = { 0 };
            id.m_Height = 134;
            setVar("SystemStateID", id);
            for (auto amount : {5, 2, 1})
            {
                Coin coin(amount);
                store(coin);
            }
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

        void send_tx_failed(const Uuid& /*txId*/) override
        {

        }

        void on_tx_completed(const Uuid& /*txId*/) override
        {
            cout << __FUNCTION__ << "\n";
        }

        void send_tx_confirmation(wallet::receiver::ConfirmationData::Ptr) override
        {
            cout << "sent recever's tx confirmation message\n";
        }

        void register_tx(const Uuid&, Transaction::Ptr) override
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
            : m_peerCount{0}
            , m_mainLoop(mainLoop)
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
            m_networkLoop.enqueueTask([this, t = std::move(task)]()
            {
                m_mainLoop.enqueueTask([t = std::move(t)](){t(); });
            });
        }

        int m_peerCount;

        vector<IWallet*> m_peers;
        IOLoop m_networkLoop;
        IOLoop& m_mainLoop;
        thread m_thread;
    };

    struct TestNetwork : public TestNetworkBase
    {
        TestNetwork(IOLoop& mainLoop) : TestNetworkBase{ mainLoop }
        {}

        void send_tx_message(PeerId to, wallet::sender::InvitationData::Ptr&& data) override
        {
            cout << "[Sender] send_tx_invitation\n";
            ++m_peerCount;
            assert(data->m_height == 134);
            WALLET_CHECK(data->m_height == 134);
            WALLET_CHECK(data->m_amount == 6);
            enqueueNetworkTask([this, to, data]() mutable {m_peers[1]->handle_tx_message(to, move(data)); });
        }

        void send_tx_message(PeerId to, wallet::sender::ConfirmationData::Ptr&& data) override
        {
            cout << "[Sender] send_tx_confirmation\n";
            enqueueNetworkTask([this, to, data]() mutable {m_peers[1]->handle_tx_message(to, move(data)); });
        }

        void send_tx_message(PeerId to, wallet::receiver::ConfirmationData::Ptr&& data) override
        {
            cout << "[Receiver] send_tx_confirmation\n";
            ++m_peerCount;
            enqueueNetworkTask([this, to, data]() mutable {m_peers[0]->handle_tx_message(to, move(data)); });
        }

        void send_tx_message(PeerId to, wallet::TxRegisteredData&& res) override
        {
            cout << "[Receiver] send_tx_registered\n";

            enqueueNetworkTask([this, to, res = move(res)]() mutable {m_peers[0]->handle_tx_message(to, move(res)); });
        }

        void send_node_message(proto::NewTransaction&& data) override
        {
            cout << "[Receiver] register_tx\n";
            enqueueNetworkTask([this, data] {m_peers[1]->handle_node_message(proto::Boolean{ true }); });

        }

        void send_node_message(proto::GetMined&& data) override
        {
            cout << "[Receiver] register_tx\n";
            enqueueNetworkTask([this, data] {m_peers[1]->handle_node_message(proto::Mined{ }); });

        }

        void send_node_message(proto::GetProofUtxo&&) override
        {
            cout << "[Sender] send_output_confirmation\n";
            int id = m_proof_id;
            --m_proof_id;
            if (m_proof_id < 0)
            {
                m_proof_id = 1;
            }
            enqueueNetworkTask([this, id] {m_peers[id]->handle_node_message(proto::ProofUtxo()); });
        }

        void send_node_message(proto::GetHdr&&) override
        {
            cout << "[Sender] request chain header\n";
        }

        void close_connection(beam::PeerId) override
        {
        }

        int m_proof_id{ 1 };
    };
}

template<typename KeychainS, typename KeychainR>
void TestWalletNegotiation()
{
    cout << "\nTesting wallets negotiation...\n";
    
    PeerId receiver_id = 4;
    IOLoop mainLoop;
    TestNetwork network{ mainLoop };

    int count = 0;
    auto f = [&count, &network](const auto& /*id*/)
    {
        if (++count >= network.m_peerCount)
        {
            network.shutdown();
        }
    };

    Wallet sender(createKeyChain<KeychainS>(), network, f);
    Wallet receiver(createKeyChain<KeychainR>(), network, f);

    network.registerPeer(&sender);
    network.registerPeer(&receiver);

    sender.transfer_money(receiver_id, 6);
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

    wallet::Sender s{ gateway, createKeyChain<TestKeyChain>(), id , 6};
    s.start();
    WALLET_CHECK(s.process_event(wallet::Sender::TxInitCompleted{ std::make_shared<wallet::receiver::ConfirmationData>() }));
    WALLET_CHECK(s.process_event(wallet::Sender::TxConfirmationCompleted()));

    cout << "\nreceiver\n";
    wallet::sender::InvitationData::Ptr initData = std::make_shared<wallet::sender::InvitationData>();
    initData->m_amount = 100;
    wallet::Receiver r{ gateway, createKeyChain<TestKeyChain>(), initData };
    r.start();
    WALLET_CHECK(!r.process_event(wallet::Receiver::TxRegistrationCompleted()));
    WALLET_CHECK(r.process_event(wallet::Receiver::TxFailed()));
    WALLET_CHECK(r.process_event(wallet::Receiver::TxConfirmationCompleted()));
}

enum NodeNetworkMessageCodes : uint8_t
{
    NewTransactionCode = 23,
    BooleanCode = 5,
    GetUtxoProofCode = 10,
    ProofUtxoCode = 12,
	ConfigCode = 20
};

class TestNode : public IMsgHandler
{
public:
    TestNode(io::Address address, io::Reactor::Ptr reactor = io::Reactor::Ptr())
        : m_protocol{ 0xAA, 0xBB, 0xCC, *this, 200}
        , m_address{ address }
        , m_reactor{ reactor ? reactor : io::Reactor::create()}
        , m_server{io::TcpServer::create(m_reactor, m_address, BIND_THIS_MEMFN(on_stream_accepted))}
        , m_tag{ 0 }
    {
        m_protocol.add_message_handler<proto::NewTransaction, &TestNode::on_message>(NewTransactionCode, 1, 2000);
        m_protocol.add_message_handler<proto::GetProofUtxo,   &TestNode::on_message>(GetUtxoProofCode, 1, 2000);
		m_protocol.add_message_handler<proto::Config,         &TestNode::on_message>(ConfigCode, 1, 2000);
    }

private:

    // IMsgHandler
    void on_protocol_error(uint64_t /*fromStream*/, ProtocolError /*error*/) override
    {
        assert(false && "NODE: on_protocol_error");
    }

    // protocol handler
    bool on_message(uint64_t connectionId, proto::NewTransaction&& /*data*/)
    {
        send(connectionId, BooleanCode, proto::Boolean{true});
        return true;
    }

    bool on_message(uint64_t connectionId, proto::GetProofUtxo&& /*data*/)
    {
        send(connectionId, ProofUtxoCode, proto::ProofUtxo());
        return true;
    }

	bool on_message(uint64_t connectionId, proto::Config&& /*data*/)
	{
		return true;
	}

    template <typename T>
    void send(PeerId to, MsgType type, T&& data)
    {
        auto it = m_connections.find(to);
        if (it != m_connections.end())
        {
            SerializedMsg msgToSend;
            m_protocol.serialize(msgToSend, type, data);
            it->second->write_msg(msgToSend); 
        }
        else 
        {
            LOG_ERROR() << "No connection";
            // add some handling
        }
    }

    void on_connection_error(uint64_t /*fromStream*/, int /*errorCode*/) override
    {
        assert(false && "NODE: on_connection_error");
    }

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, int errorCode)
    {
        if (errorCode == 0)
        {
            LOG_DEBUG() << "Stream accepted";
            auto tag = ++m_tag;// id
            m_connections.emplace(tag, make_unique<Connection>(
                    m_protocol,
                    tag,
                    Connection::inbound,
                    100,
                    std::move(newStream)));
        }
        else
        {
       //     on_connection_error(m_address.packed, errorCode);
        }
    }

    Protocol<TestNode> m_protocol;
    io::Address m_address;
    io::Reactor::Ptr m_reactor;
    io::TcpServer::Ptr m_server;

    std::map<uint64_t, std::unique_ptr<Connection>> m_connections;
    uint64_t m_tag;
};

void TestP2PWalletNegotiationST()
{
    cout << "\nTesting p2p wallets negotiation single thread...\n";

    auto node_address = io::Address::localhost().port(32125);
    auto receiver_address = io::Address::localhost().port(32124);
    auto sender_address = io::Address::localhost().port(32123);

    io::Reactor::Ptr main_reactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*main_reactor);

    TestNode node{ node_address, main_reactor };
    WalletNetworkIO sender_io{ sender_address, node_address, false, createKeyChain<TestKeyChain>(), main_reactor };
    WalletNetworkIO receiver_io{ receiver_address, node_address, true, createKeyChain<TestKeyChain2>(), main_reactor, 1000, 100 };

    sender_io.transfer_money(receiver_address, 6);

    main_reactor->run();
}

void TestSplitKey()
{
    auto nonce = beam::generateNonce();
    auto res1 = beam::split_key(nonce, 123456789);
    auto res2 = beam::split_key(nonce, 123456789);
    auto res3 = beam::split_key(nonce, 123456789);
    WALLET_CHECK(res1.first == res2.first && res2.first == res3.first);
    WALLET_CHECK(res1.second == res2.second && res2.second == res3.second);
    Scalar::Native s2 = res1.second;
    s2 = -s2;
    Scalar::Native s1 = res1.first+s2;
    WALLET_CHECK(s1 == nonce);
}

int main()
{
    LoggerConfig lc;
    int logLevel = LOG_LEVEL_DEBUG;
    lc.consoleLevel = logLevel;
    lc.flushLevel = logLevel;
    auto logger = Logger::create(lc);

    TestSplitKey();

    TestP2PWalletNegotiationST();
    TestWalletNegotiation<TestKeyChain, TestKeyChain2>();
    TestWalletNegotiation<SqliteKeychainInt, TestKeyChain2>();
    TestRollback();
    TestFSM();

    return WALLET_CHECK_RESULT;
}
