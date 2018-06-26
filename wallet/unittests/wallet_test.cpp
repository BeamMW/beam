#include "wallet/wallet_network.h"
#include "wallet/wallet.h"
#include "utility/test_helpers.h"

#include "test_helpers.h"

#include <assert.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include "core/proto.h"
#include "wallet/wallet_serialization.h"
#include <boost/filesystem.hpp>

using namespace beam;
using namespace std;
using namespace ECC;

WALLET_TEST_INIT

namespace
{
    class BaseTestKeyChain : public IKeyChain
    {
    public:

        ECC::Scalar::Native calcKey(const Coin&) const override
        {
            return ECC::Scalar::Native();
        }

        std::vector<beam::Coin> getCoins(const ECC::Amount& amount, bool /*lock*/) override
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
        void store(std::vector<beam::Coin>& ) override {}
        void update(const std::vector<beam::Coin>& ) override {}
        void remove(const std::vector<beam::Coin>& ) override {}
        void remove(const beam::Coin& ) override {}
        void visit(std::function<bool(const beam::Coin& coin)> func) override {}
		void setVarRaw(const char* name, const void* data, int size) override {}
		int getVarRaw(const char* name, void* data) const override { return 0; }
		void setSystemStateID(const Block::SystemState::ID& stateID) override {};
		bool getSystemStateID(Block::SystemState::ID& stateID) const override { return false; };

        std::vector<TxDescription> getTxHistory(uint64_t start, int count) override { return {}; };
        boost::optional<TxDescription> getTx(const Uuid& txId) override { return boost::optional<TxDescription>{}; };
        void saveTx(const TxDescription &) override {};
        void deleteTx(const Uuid& txId) override {};

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

    template<typename KeychainImpl>
    IKeyChain::Ptr createKeychain()
    {
        return std::static_pointer_cast<IKeyChain>(std::make_shared<KeychainImpl>());
    }

    IKeyChain::Ptr createSqliteKeychain(const string& path)
    {
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = ECC::Zero;
        if (boost::filesystem::exists(path))
        {
            boost::filesystem::remove(path);
        }

        auto keychain = Keychain::init(path, "pass123", seed);
        beam::Block::SystemState::ID id = {};
        id.m_Height = 134;
        keychain->setSystemStateID(id);
        return keychain;
    }

    IKeyChain::Ptr createSenderKeychain()
    {
        auto keychain = createSqliteKeychain("sender_wallet.db");
        for (auto amount : { 5, 2, 1, 9 })
        {
            Coin coin(amount);
            coin.m_maturity = 0;
            keychain->store(coin);
        }
        return keychain;
    }

    IKeyChain::Ptr createReceiverKeychain()
    {
        auto keychain = createSqliteKeychain("receiver_wallet.db");
        return keychain;
    }

    struct TestGateway : wallet::sender::IGateway
        , wallet::receiver::IGateway
    {
        void send_tx_invitation(const TxDescription& tx, wallet::InviteReceiver&&) override
        {
            cout << "sent tx initiation message\n";
        }

        void send_tx_confirmation(const TxDescription& tx, wallet::ConfirmTransaction&&) override
        {
            cout << "sent senders's tx confirmation message\n";
        }

        void send_tx_failed(const TxDescription&) override
        {

        }

        void on_tx_completed(const TxDescription&) override
        {
            cout << __FUNCTION__ << "\n";
        }

        void send_tx_confirmation(const TxDescription& tx, wallet::ConfirmInvitation&&) override
        {
            cout << "sent recever's tx confirmation message\n";
        }

        void register_tx(const TxDescription& tx, Transaction::Ptr) override
        {
            cout << "sent tx registration request\n";
        }

        void send_tx_registered(const TxDescription& tx) override
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

            walletPeer->handle_node_message(proto::NewTip{});

            proto::Hdr msg = {};

            msg.m_Description.m_Height = 134;
            walletPeer->handle_node_message(move(msg));
        }

        void enqueueNetworkTask(Task&& task)
        {
            m_networkLoop.enqueueTask([this, t = std::move(task)]()
            {
                m_mainLoop.enqueueTask([t = std::move(t)](){t(); });
            });
        }

        template<typename Msg>
        void send(size_t peedId, uint64_t to, Msg&& msg)
        {
            Serializer s;
            s & msg;
            ByteBuffer buf;
            s.swap_buf(buf);
            enqueueNetworkTask([this, peedId, to, buf = move(buf)]()
            {
                Deserializer d;
                d.reset(&buf[0], buf.size());
                Msg msg;
                d & msg;
                m_peers[peedId]->handle_tx_message(to, move(msg));
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

        void send_tx_message(PeerId to, wallet::InviteReceiver&& data) override
        {
            cout << "[Sender] send_tx_invitation\n";
            ++m_peerCount;
            assert(data.m_height == 134);
            WALLET_CHECK(data.m_height == 134);
            WALLET_CHECK(data.m_amount == 6);
            send(1, to, move(data));
        }

        void send_tx_message(PeerId to, wallet::ConfirmTransaction&& data) override
        {
            cout << "[Sender] send_tx_confirmation\n";
            send(1, to, move(data));
        }

        void send_tx_message(PeerId to, wallet::ConfirmInvitation&& data) override
        {
            cout << "[Receiver] send_tx_confirmation\n";
            ++m_peerCount;
            send(0, to, move(data));
        }

        void send_tx_message(PeerId to, wallet::TxRegistered&& data) override
        {
            cout << "[Receiver] send_tx_registered\n";
            send(0, to, move(data));
        }

        void send_tx_message(beam::PeerId to, beam::wallet::TxFailed&& data) override
        {
            cout << "TxFailed\n";
            send(0, to, move(data));
        }

        void send_node_message(proto::NewTransaction&& data) override
        {
            cout << "[Receiver] register_tx\n";
            enqueueNetworkTask([this, data] {m_peers[1]->handle_node_message(proto::Boolean{ true }); });
        }

        void send_node_message(proto::GetMined&& data) override
        {
            cout << "[Receiver] register_tx\n";
            enqueueNetworkTask([this] {m_peers[1]->handle_node_message(proto::Mined{ }); });
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

        void close_node_connection() override
        {
            ++m_closeNodeCount;
        }

        int m_proof_id{ 1 };
        int m_closeNodeCount{ 0 };
    };
}

void TestWalletNegotiation(IKeyChain::Ptr senderKeychain, IKeyChain::Ptr receiverKeychain)
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

    Wallet sender(senderKeychain, network, f);
    Wallet receiver(receiverKeychain, network, f);

    network.registerPeer(&sender);
    network.registerPeer(&receiver);

    sender.transfer_money(receiver_id, 6, {});
    mainLoop.run();

    WALLET_CHECK(network.m_closeNodeCount == 4);
}

void TestRollback()
{

}

void TestFSM()
{
    cout << "\nTesting wallet's fsm...\nsender\n";
    TestGateway gateway;

    TxDescription stx = {};
    stx.m_amount = 6;
    wallet::Sender s{ gateway, createKeychain<TestKeyChain>(), stx};
    s.start();
    WALLET_CHECK(s.process_event(wallet::Sender::TxInitCompleted{ wallet::ConfirmInvitation() }));
    WALLET_CHECK(s.process_event(wallet::Sender::TxConfirmationCompleted()));

    cout << "\nreceiver\n";
    wallet::InviteReceiver initData;
    initData.m_amount = 100;
    wallet::Receiver r{ gateway, createKeychain<TestKeyChain>(), {}, initData };
    r.start();
    WALLET_CHECK(!r.process_event(wallet::Receiver::TxRegistrationCompleted()));
    WALLET_CHECK(r.process_event(wallet::Receiver::TxFailed()));
    WALLET_CHECK(r.process_event(wallet::Receiver::TxConfirmationCompleted()));
}

enum NodeNetworkMessageCodes : uint8_t
{
    NewTipCode = 1,
    NewTransactionCode = 23,
    BooleanCode = 5,
    HdrCode = 3,
    GetUtxoProofCode = 10,
    ProofUtxoCode = 12,
	ConfigCode = 20
};

class TestNode : public IErrorHandler
{
public:
    TestNode(io::Address address, io::Reactor::Ptr reactor = io::Reactor::Ptr())
        : m_protocol{ 0xAA, 0xBB, 0xCC, 150, *this, 2000}
        , m_address{ address }
        , m_reactor{ reactor ? reactor : io::Reactor::create()}
        , m_server{io::TcpServer::create(m_reactor, m_address, BIND_THIS_MEMFN(on_stream_accepted))}
        , m_tag{ 0 }
    {
        m_protocol.add_message_handler<TestNode, proto::NewTransaction, &TestNode::on_message>(NewTransactionCode, this, 1, 2000);
        m_protocol.add_message_handler<TestNode, proto::GetProofUtxo,   &TestNode::on_message>(GetUtxoProofCode, this, 1, 2000);
		m_protocol.add_message_handler<TestNode, proto::Config,         &TestNode::on_message>(ConfigCode, this, 1, 2000);
    }

private:

    // IErrorHandler
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
        proto::Hdr msg = {};

        msg.m_Description.m_Height = 134;
        send(connectionId, HdrCode, move(msg));
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

    void on_connection_error(uint64_t fromStream, io::ErrorCode /*errorCode*/) override
    {
        if (auto it = m_connections.find(fromStream); it != m_connections.end())
        {
            ++m_closeCount;
        }
    }

    void on_stream_accepted(io::TcpStream::Ptr&& newStream, int errorCode)
    {
        if (errorCode == 0)
        {
            LOG_DEBUG() << "Stream accepted: " << newStream->peer_address();
            auto tag = ++m_tag;// id
            m_connections.emplace(tag, make_unique<Connection>(
                    m_protocol,
                    tag,
                    Connection::inbound,
                    100,
                    std::move(newStream)));

            ++m_connectCount;
            send(tag, NewTipCode, proto::NewTip{ });
        }
        else
        {
       //     on_connection_error(m_address.packed, errorCode);
        }
    }

    Protocol m_protocol;
    io::Address m_address;
    io::Reactor::Ptr m_reactor;
    io::TcpServer::Ptr m_server;

    std::map<uint64_t, std::unique_ptr<Connection>> m_connections;
    uint64_t m_tag;
public:
    int m_connectCount{ 0 };
    int m_closeCount{ 0 };
};

void TestP2PWalletNegotiationST()
{
    cout << "\nTesting p2p wallets negotiation single thread...\n";

    auto node_address = io::Address::localhost().port(32125);
    auto receiver_address = io::Address::localhost().port(32124);
    auto sender_address = io::Address::localhost().port(32123);

    io::Reactor::Ptr main_reactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*main_reactor);

    auto senderKeychain = createSenderKeychain();
    auto receiverKeychain = createReceiverKeychain();

    WALLET_CHECK(senderKeychain->getCoins(6, false).size() == 3);

    WALLET_CHECK(senderKeychain->getTxHistory().empty());
    WALLET_CHECK(receiverKeychain->getTxHistory().empty());

    helpers::StopWatch sw;
    sw.start();
    TestNode node{ node_address, main_reactor };
    WalletNetworkIO sender_io{ sender_address, node_address, false, senderKeychain, main_reactor };
    WalletNetworkIO receiver_io{ receiver_address, node_address, true, receiverKeychain, main_reactor, 1000, 5000, 100 };

    Uuid txId = sender_io.transfer_money(receiver_address, 6);

    main_reactor->run();
    sw.stop();
    cout << "First transfer elapsed time: " << sw.milliseconds() << " ms\n";

    // check coins
    vector<Coin> newSenderCoins;
    senderKeychain->visit([&newSenderCoins](const Coin& c)->bool
    {
        newSenderCoins.push_back(c);
        return true;
    });
    vector<Coin> newReceiverCoins;
    receiverKeychain->visit([&newReceiverCoins](const Coin& c)->bool
    {
        newReceiverCoins.push_back(c);
        return true;
    });

    WALLET_CHECK(newSenderCoins.size() == 5);
    WALLET_CHECK(newReceiverCoins.size() == 1);
    WALLET_CHECK(newReceiverCoins[0].m_amount == 6);
    WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Unconfirmed);
    WALLET_CHECK(newReceiverCoins[0].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[0].m_amount == 5);
    WALLET_CHECK(newSenderCoins[0].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[0].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[1].m_amount == 2);
    WALLET_CHECK(newSenderCoins[1].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[1].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[2].m_amount == 1);
    WALLET_CHECK(newSenderCoins[2].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[2].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[3].m_amount == 9);
    WALLET_CHECK(newSenderCoins[3].m_status == Coin::Unspent);
    WALLET_CHECK(newSenderCoins[3].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[4].m_amount == 2);
    WALLET_CHECK(newSenderCoins[4].m_status == Coin::Unconfirmed);
    WALLET_CHECK(newSenderCoins[4].m_key_type == KeyType::Regular);
    
    // Tx history check
    auto sh = senderKeychain->getTxHistory();
    WALLET_CHECK(sh.size() == 1);
    auto rh = receiverKeychain->getTxHistory();
    WALLET_CHECK(rh.size() == 1);
    auto stx = senderKeychain->getTx(txId);
    WALLET_CHECK(stx.is_initialized());
    auto rtx = receiverKeychain->getTx(txId);
    WALLET_CHECK(rtx.is_initialized());

    WALLET_CHECK(stx->m_txId == rtx->m_txId);
    WALLET_CHECK(stx->m_amount == rtx->m_amount);
    WALLET_CHECK(stx->m_message == rtx->m_message);
    WALLET_CHECK(stx->m_createTime <= rtx->m_createTime);
    WALLET_CHECK(stx->m_status == rtx->m_status);
    WALLET_CHECK(stx->m_fsmState.empty());
    WALLET_CHECK(rtx->m_fsmState.empty());
    WALLET_CHECK(stx->m_sender == true);
    WALLET_CHECK(rtx->m_sender == false);

    // second transfer
    sw.start();
    txId = sender_io.transfer_money(receiver_address, 6);
    main_reactor->run();
    sw.stop();
    cout << "Second transfer elapsed time: " << sw.milliseconds() << " ms\n";

    // check coins
    newSenderCoins.clear();
    senderKeychain->visit([&newSenderCoins](const Coin& c)->bool
    {
        newSenderCoins.push_back(c);
        return true;
    });
    newReceiverCoins.clear();
    receiverKeychain->visit([&newReceiverCoins](const Coin& c)->bool
    {
        newReceiverCoins.push_back(c);
        return true;
    });

    WALLET_CHECK(newSenderCoins.size() == 6);
    WALLET_CHECK(newReceiverCoins.size() == 2);

    WALLET_CHECK(newReceiverCoins[0].m_amount == 6);
    WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Unconfirmed);
    WALLET_CHECK(newReceiverCoins[0].m_key_type == KeyType::Regular);
    
    WALLET_CHECK(newReceiverCoins[1].m_amount == 6);
    WALLET_CHECK(newReceiverCoins[1].m_status == Coin::Unconfirmed);
    WALLET_CHECK(newReceiverCoins[1].m_key_type == KeyType::Regular);


    WALLET_CHECK(newSenderCoins[0].m_amount == 5);
    WALLET_CHECK(newSenderCoins[0].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[0].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[1].m_amount == 2);
    WALLET_CHECK(newSenderCoins[1].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[1].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[2].m_amount == 1);
    WALLET_CHECK(newSenderCoins[2].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[2].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[3].m_amount == 9);
    WALLET_CHECK(newSenderCoins[3].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[3].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[4].m_amount == 2);
    WALLET_CHECK(newSenderCoins[4].m_status == Coin::Unconfirmed);
    WALLET_CHECK(newSenderCoins[4].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[5].m_amount == 3);
    WALLET_CHECK(newSenderCoins[5].m_status == Coin::Unconfirmed);
    WALLET_CHECK(newSenderCoins[5].m_key_type == KeyType::Regular);

    // Tx history check
    sh = senderKeychain->getTxHistory();
    WALLET_CHECK(sh.size() == 2);
    rh = receiverKeychain->getTxHistory();
    WALLET_CHECK(rh.size() == 2);
    stx = senderKeychain->getTx(txId);
    WALLET_CHECK(stx.is_initialized());
    rtx = receiverKeychain->getTx(txId);
    WALLET_CHECK(rtx.is_initialized());

    WALLET_CHECK(stx->m_txId == rtx->m_txId);
    WALLET_CHECK(stx->m_amount == rtx->m_amount);
    WALLET_CHECK(stx->m_message == rtx->m_message);
    WALLET_CHECK(stx->m_createTime <= rtx->m_createTime);
    WALLET_CHECK(stx->m_status == rtx->m_status);
    WALLET_CHECK(stx->m_fsmState.empty());
    WALLET_CHECK(rtx->m_fsmState.empty());
    WALLET_CHECK(stx->m_sender == true);
    WALLET_CHECK(rtx->m_sender == false);


    // third transfer. no enough money should appear
    sw.start();
    txId = sender_io.transfer_money(receiver_address, 6);
    main_reactor->run();
    sw.stop();
    cout << "Third transfer elapsed time: " << sw.milliseconds() << " ms\n";
    // check coins
    newSenderCoins.clear();
    senderKeychain->visit([&newSenderCoins](const Coin& c)->bool
    {
        newSenderCoins.push_back(c);
        return true;
    });
    newReceiverCoins.clear();
    receiverKeychain->visit([&newReceiverCoins](const Coin& c)->bool
    {
        newReceiverCoins.push_back(c);
        return true;
    });

    // no coins 
    WALLET_CHECK(newSenderCoins.size() == 6);
    WALLET_CHECK(newReceiverCoins.size() == 2);

    // Tx history check. New failed tx should be added to sender
    sh = senderKeychain->getTxHistory();
    WALLET_CHECK(sh.size() == 3);
    rh = receiverKeychain->getTxHistory();
    WALLET_CHECK(rh.size() == 2);
    stx = senderKeychain->getTx(txId);
    WALLET_CHECK(stx.is_initialized());
    rtx = receiverKeychain->getTx(txId);
    WALLET_CHECK(!rtx.is_initialized());

    WALLET_CHECK(stx->m_amount == 6);
    WALLET_CHECK(stx->m_status == TxDescription::Failed);
    WALLET_CHECK(stx->m_fsmState.empty());
    WALLET_CHECK(stx->m_sender == true);
 }

void TestSplitKey()
{
	Scalar::Native nonce;
	nonce = (uint64_t) 0xa231234f92381353UL;

    auto res1 = beam::wallet::splitKey(nonce, 123456789);
    auto res2 = beam::wallet::splitKey(nonce, 123456789);
    auto res3 = beam::wallet::splitKey(nonce, 123456789);
    WALLET_CHECK(res1.first == res2.first && res2.first == res3.first);
    WALLET_CHECK(res1.second == res2.second && res2.second == res3.second);
    Scalar::Native s2 = res1.second;
    s2 = -s2;
    Scalar::Native s1 = res1.first+s2;
    WALLET_CHECK(s1 == nonce);
}

void TestSerializeFSM()
{
    cout << "\nTesting wallet's fsm serialization...\nsender\n";
    TestGateway gateway;
    
    {
        TxDescription tx = {};
        tx.m_amount = 6;
        wallet::Sender s{ gateway, createKeychain<TestKeyChain>(), tx};
        WALLET_CHECK(*(s.current_state()) == 0);
        s.start();
        WALLET_CHECK(*(s.current_state()) == 1);

        Serializer ser;
        ser & s;

        auto buffer = ser.buffer();

        Deserializer der;
        der.reset(buffer.first, buffer.second);

        wallet::Sender s2{ gateway, createKeychain<TestKeyChain>(), {} };
        WALLET_CHECK(*(s2.current_state()) == 0);
        der & s2;
        WALLET_CHECK(*(s2.current_state()) == 1);
        s2.process_event(wallet::Sender::TxInitCompleted{ wallet::ConfirmInvitation() });
        WALLET_CHECK(*(s2.current_state()) == 2);

        ser.reset();
        ser & s2;

        buffer = ser.buffer();
        der.reset(buffer.first, buffer.second);
        der & s;
        WALLET_CHECK(*(s.current_state()) == 2);
    }

    {
        wallet::InviteReceiver initData{};
        initData.m_amount = 100;
        TxDescription rtx = {};
        rtx.m_amount = 100;
        wallet::Receiver r{ gateway, createKeychain<TestKeyChain>(), rtx, initData };
        WALLET_CHECK(*(r.current_state()) == 0);
        r.start();
        WALLET_CHECK(*(r.current_state()) == 1);

        Serializer ser;
        ser & r;

        auto buffer = ser.buffer();

        Deserializer der;
        der.reset(buffer.first, buffer.second);

        wallet::Receiver r2{ gateway, createKeychain<TestKeyChain>(), {}, initData };
        LOG_DEBUG() << "state = " << *(r2.current_state());
        WALLET_CHECK(*(r2.current_state()) == 0);
        der & r2;
        LOG_DEBUG() << "state = " << *(r2.current_state());
        WALLET_CHECK(*(r2.current_state()) == 1);
        r2.process_event(wallet::Receiver::TxConfirmationCompleted{});
        LOG_DEBUG() << "state = " << *(r2.current_state());
        WALLET_CHECK(*(r2.current_state()) == 3);

        ser.reset();
        ser & r2;

        buffer = ser.buffer();
        der.reset(buffer.first, buffer.second);
        der & r;
        LOG_DEBUG() << "state = " << *(r2.current_state());
        WALLET_CHECK(*(r.current_state()) == 3);
    }

}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = beam::Logger::create(logLevel, logLevel);

    TestSplitKey();
    
    TestP2PWalletNegotiationST();
    TestWalletNegotiation(createKeychain<TestKeyChain>(), createKeychain<TestKeyChain2>());
    TestWalletNegotiation(createSenderKeychain(), createReceiverKeychain());
    TestRollback();
    TestFSM();
    TestSerializeFSM();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
