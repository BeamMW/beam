#define LOG_VERBOSE_ENABLED 0

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

#include "core/storage.h"

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

        std::vector<beam::Coin> selectCoins(const ECC::Amount& amount, bool /*lock*/) override
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
        void update(const vector<beam::Coin>& coins) override {}
        void update(const beam::Coin& ) override {}
        void remove(const std::vector<beam::Coin>& ) override {}
        void remove(const beam::Coin& ) override {}
        void visit(std::function<bool(const beam::Coin& coin)> ) override {}
		void setVarRaw(const char* , const void* , int ) override {}
		int getVarRaw(const char* , void* ) const override { return 0; }
        Timestamp getLastUpdateTime() const override { return 0; }
		void setSystemStateID(const Block::SystemState::ID& ) override {};
		bool getSystemStateID(Block::SystemState::ID& ) const override { return false; };

		void subscribe(IKeyChainObserver* observer) override {}
		void unsubscribe(IKeyChainObserver* observer) override {}

        std::vector<TxDescription> getTxHistory(uint64_t , int ) override { return {}; };
        boost::optional<TxDescription> getTx(const TxID& ) override { return boost::optional<TxDescription>{}; };
        void saveTx(const TxDescription &) override {};
        void deleteTx(const TxID& ) override {};
        void rollbackTx(const TxID&) override {}

        std::vector<TxPeer> getPeers() override { return {}; };
        void addPeer(const TxPeer&) override {}
        boost::optional<TxPeer> getPeer(const WalletID&) override { return boost::optional<TxPeer>{}; }
		void clearPeers() override {}

        std::vector<WalletAddress> getAddresses(bool own) override { return {}; }
        void saveAddress(const WalletAddress&) override {}
        void deleteAddress(const WalletID&) override {}

        Height getCurrentHeight() const override
        {
            return 134;
        }

        uint64_t getKnownStateCount() const override
        {
            return 0;
        }

        Block::SystemState::ID getKnownStateID(Height height) override
        {
            return {};
        }

        void rollbackConfirmedUtxo(Height /*minHeight*/) override
        {}

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
        auto db = createSqliteKeychain("sender_wallet.db");
        for (auto amount : { 5, 2, 1, 9 })
        {
            Coin coin(amount);
            coin.m_maturity = 0;
            db->store(coin);
        }
        return db;
    }

    IKeyChain::Ptr createReceiverKeychain()
    {
        return createSqliteKeychain("receiver_wallet.db");
    }

    struct TestGateway : wallet::INegotiatorGateway
    {
        void send_tx_invitation(const TxDescription& , wallet::Invite&&) override
        {
            cout << "sent tx initiation message\n";
        }

        void send_tx_confirmation(const TxDescription& , wallet::ConfirmTransaction&&) override
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

        void send_tx_confirmation(const TxDescription& , wallet::ConfirmInvitation&&) override
        {
            cout << "sent recever's tx confirmation message\n";
        }

        void register_tx(const TxDescription& , Transaction::Ptr) override
        {
            cout << "sent tx registration request\n";
        }

        void send_tx_registered(const TxDescription& ) override
        {
            cout << "sent tx registration completed \n";
        }
    };

    struct IOLoop
    {
        using Task = function<void()>;
        IOLoop()
          : m_shutdown{ false }
          , m_uses{0}
        {

        }


        void addRef()
        {
            ++m_uses;
        }

        void release()
        {
            if (--m_uses == 0)
            {
                shutdown();
            }
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
        atomic<int> m_uses;
    };

    //
    // Test impl of the network io. The second thread isn't really needed, though this is much close to reality
    //
    struct TestNetworkBase : public NetworkIOBase
    {
        using Task = function<void()>;
        TestNetworkBase(IOLoop& mainLoop)
            : m_peerCount{0}
            , m_mainLoop(mainLoop)
            , m_thread{ [this] { m_networkLoop.run(); } }
        {
            mainLoop.addRef();
        }

        void shutdown()
        {
            m_networkLoop.shutdown();
            m_mainLoop.enqueueTask([this]()
            {
                m_thread.join();
                m_mainLoop.release();
            });
        }

        void registerPeer(IWallet* walletPeer, bool main)
        {
            m_peers.push_back(walletPeer);

            if (main)
            {
                walletPeer->handle_node_message(proto::NewTip{});

                proto::Hdr msg = {};
                InitHdr(msg);
                walletPeer->handle_node_message(move(msg));
            }
        }

        virtual void InitHdr(proto::Hdr& msg)
        {
            msg.m_Description.m_Height = 134;
        }

        void enqueueNetworkTask(Task&& task)
        {
            m_networkLoop.enqueueTask([this, t = std::move(task)]()
            {
                m_mainLoop.enqueueTask([t = std::move(t)](){t(); });
            });
        }

        template<typename Msg>
        void send(size_t peerId, const WalletID& to, Msg&& msg)
        {
            Serializer s;
            s & msg;
            ByteBuffer buf;
            s.swap_buf(buf);
            enqueueNetworkTask([this, peerId, to, buf = move(buf)]()
            {
                Deserializer d;
                d.reset(&buf[0], buf.size());
                Msg msg;
                d & msg;
                m_peers[peerId]->handle_tx_message(to, move(msg));
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

        void send_tx_message(const WalletID& to, wallet::Invite&& data) override
        {
            cout << "[Sender] send_tx_invitation\n";
            ++m_peerCount;
            WALLET_CHECK(data.m_amount == 6);
            send(1, to, move(data));
        }

        void send_tx_message(const WalletID& to, wallet::ConfirmTransaction&& data) override
        {
            cout << "[Sender] send_tx_confirmation\n";
            send(1, to, move(data));
        }

        void send_tx_message(const WalletID& to, wallet::ConfirmInvitation&& data) override
        {
            cout << "[Receiver] send_tx_confirmation\n";
            ++m_peerCount;
            send(1, to, move(data));
        }

        void send_tx_message(const WalletID& to, wallet::TxRegistered&& data) override
        {
            cout << "[Receiver] send_tx_registered\n";
            send(1, to, move(data));
        }

        void send_tx_message(const WalletID& to, beam::wallet::TxFailed&& data) override
        {
            cout << "TxFailed\n";
            send(1, to, move(data));
        }

        void send_node_message(proto::NewTransaction&& data) override
        {
            cout << "NewTransaction\n";
            enqueueNetworkTask([this, data] {m_peers[0]->handle_node_message(proto::Boolean{ true }); });
        }

        void send_node_message(proto::GetMined&& data) override
        {
            cout << "GetMined\n";
            enqueueNetworkTask([this] {m_peers[0]->handle_node_message(proto::Mined{ }); });
        }

        void send_node_message(proto::GetProofUtxo&&) override
        {
            cout << "GetProofUtxo\n";

            enqueueNetworkTask([this] {m_peers[0]->handle_node_message(proto::ProofUtxo()); });
        }

        void send_node_message(proto::GetHdr&&) override
        {
            cout << "GetHdr request chain header\n";
        }

        void send_node_message(beam::proto::GetProofState &&) override
        {
            cout << "GetProofState\n";
            enqueueNetworkTask([this] {m_peers[0]->handle_node_message(proto::Proof{}); });
        }

        void close_connection(const beam::WalletID&) override
        {
        }

        void connect_node() override
        {

        }

        void close_node_connection() override
        {
        }
    };
}

void TestWalletNegotiation(IKeyChain::Ptr senderKeychain, IKeyChain::Ptr receiverKeychain)
{
    cout << "\nTesting wallets negotiation...\n";

    WalletID receiver_id = {};
    receiver_id = unsigned(4);
    IOLoop mainLoop;
    auto network = make_shared<TestNetwork >(mainLoop);
    auto network2 = make_shared<TestNetwork>(mainLoop);

    int count = 0;
    auto f = [&count, network, network2](const auto& /*id*/)
    {
        if (++count >= (network->m_peerCount + network2->m_peerCount))
        {
            network->shutdown();
            network2->shutdown();
        }
    };

    Wallet sender(senderKeychain, network, f);
    Wallet receiver(receiverKeychain, network2, f);

    network->registerPeer(&sender, true);
    network->registerPeer(&receiver, false);

    network2->registerPeer(&receiver, true);
    network2->registerPeer(&sender, false);

    sender.transfer_money(receiver_id, 6, 0, true, {});
    mainLoop.run();
}

void TestFSM()
{
    cout << "\nTesting wallet's fsm...\nsender\n";
    TestGateway gateway;

    TxDescription stx = {};
    stx.m_amount = 6;
    wallet::Negotiator s{ gateway, createKeychain<TestKeyChain>(), stx};
    s.process_event(wallet::events::TxInitiated());
    WALLET_CHECK(*(s.current_state()) == 2);
    s.start();
    WALLET_CHECK(*(s.current_state()) == 0);
    s.process_event(wallet::events::TxInitiated());
    WALLET_CHECK(*(s.current_state()) == 2);
    s.stop();
    WALLET_CHECK(*(s.current_state()) == 2);
    s.start();
    WALLET_CHECK(*(s.current_state()) == 0);

    s.process_event(wallet::events::TxInitiated());
    WALLET_CHECK(*(s.current_state()) == 2);
    s.process_event(wallet::events::TxInitiated());
    WALLET_CHECK(*(s.current_state()) == 2);
    s.process_event(wallet::events::TxInvitationCompleted{});
    WALLET_CHECK(*(s.current_state()) == 3);
    s.process_event(wallet::events::TxConfirmationCompleted{});
    WALLET_CHECK(*(s.current_state()) == 3);
}

enum NodeNetworkMessageCodes : uint8_t
{
    NewTipCode = 1,
    NewTransactionCode = 23,
    BooleanCode = 5,
    HdrCode = 3,
    GetUtxoProofCode = 10,
    ProofUtxoCode = 12,
    ConfigCode = 20,
    GetMinedCode = 15,
    MinedCode = 16,
    GetProofStateCode = 8,
    ProofCode = 11
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
        m_protocol.add_message_handler<TestNode, proto::GetMined,       &TestNode::on_message>(GetMinedCode, this, 1, 2000);
        m_protocol.add_message_handler<TestNode, proto::GetProofState,  &TestNode::on_message>(GetProofStateCode, this, 1, 2000);
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

    bool on_message(uint64_t connectionId, proto::GetMined&&)
    {
        send(connectionId, MinedCode, proto::Mined{});
        return true;
    }

    bool on_message(uint64_t connectionId, proto::GetProofState&&)
    {
        send(connectionId, ProofCode, proto::Proof{});
        return true;
    }

    template <typename T>
    void send(uint64_t to, MsgType type, T&& data)
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

    void on_connection_error(uint64_t /*fromStream*/, io::ErrorCode /*errorCode*/) override
    {
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

    TxPeer receiverPeer = {};
    receiverPeer.m_walletID = uint64_t(12345678912345);
    receiverPeer.m_address = receiver_address.str();
    
    senderKeychain->addPeer(receiverPeer);

    TxPeer senderPeer = {};
    senderPeer.m_walletID = uint64_t(43412345678912345);
    senderPeer.m_address = sender_address.str();

    receiverKeychain->addPeer(senderPeer);


    WALLET_CHECK(senderKeychain->selectCoins(6, false).size() == 2);

    WALLET_CHECK(senderKeychain->getTxHistory().empty());
    WALLET_CHECK(receiverKeychain->getTxHistory().empty());

    helpers::StopWatch sw;
    TestNode node{ node_address, main_reactor };
    auto sender_io = make_shared<WalletNetworkIO>( sender_address, node_address, false, senderKeychain, main_reactor );
    auto receiver_io = make_shared<WalletNetworkIO>( receiver_address, node_address, true, receiverKeychain, main_reactor, 1000, 5000, 100 );


    Wallet sender{senderKeychain, sender_io, [sender_io](auto) { sender_io->stop(); } };
    Wallet receiver{ receiverKeychain, receiver_io };

    // unknown peer
    sender.transfer_money(senderPeer.m_walletID, 6);
    main_reactor->run();
    auto sh = senderKeychain->getTxHistory();
    WALLET_CHECK(sh.size() == 1);
    WALLET_CHECK(sh[0].m_status == TxDescription::Failed);
    senderKeychain->deleteTx(sh[0].m_txId);

    sw.start();

    TxID txId = sender.transfer_money(receiverPeer.m_walletID, 4, 2);

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

    WALLET_CHECK(newSenderCoins.size() == 4);
    WALLET_CHECK(newReceiverCoins.size() == 1);
    WALLET_CHECK(newReceiverCoins[0].m_amount == 4);
    WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Unconfirmed);
    WALLET_CHECK(newReceiverCoins[0].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[0].m_amount == 5);
    WALLET_CHECK(newSenderCoins[0].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[0].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[1].m_amount == 2);
    WALLET_CHECK(newSenderCoins[1].m_status == Coin::Unspent);
    WALLET_CHECK(newSenderCoins[1].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[2].m_amount == 1);
    WALLET_CHECK(newSenderCoins[2].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[2].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[3].m_amount == 9);
    WALLET_CHECK(newSenderCoins[3].m_status == Coin::Unspent);
    WALLET_CHECK(newSenderCoins[3].m_key_type == KeyType::Regular);
    
    // Tx history check
    sh = senderKeychain->getTxHistory();
    WALLET_CHECK(sh.size() == 1);
    auto rh = receiverKeychain->getTxHistory();
    WALLET_CHECK(rh.size() == 1);
    auto stx = senderKeychain->getTx(txId);
    WALLET_CHECK(stx.is_initialized());
    auto rtx = receiverKeychain->getTx(txId);
    WALLET_CHECK(rtx.is_initialized());

    WALLET_CHECK(stx->m_txId == rtx->m_txId);
    WALLET_CHECK(stx->m_amount == rtx->m_amount);
    WALLET_CHECK(stx->m_fee == rtx->m_fee);
    WALLET_CHECK(stx->m_message == rtx->m_message);
    WALLET_CHECK(stx->m_createTime <= rtx->m_createTime);
    WALLET_CHECK(stx->m_status == rtx->m_status);
    WALLET_CHECK(stx->m_fsmState.empty());
    WALLET_CHECK(rtx->m_fsmState.empty());
    WALLET_CHECK(stx->m_sender == true);
    WALLET_CHECK(rtx->m_sender == false);

    // second transfer
    sw.start();
    txId = sender.transfer_money(receiverPeer.m_walletID, 6);
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

    WALLET_CHECK(newSenderCoins.size() == 5);
    WALLET_CHECK(newReceiverCoins.size() == 2);

    WALLET_CHECK(newReceiverCoins[0].m_amount == 4);
    WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Unconfirmed);
    WALLET_CHECK(newReceiverCoins[0].m_key_type == KeyType::Regular);
    
    WALLET_CHECK(newReceiverCoins[1].m_amount == 6);
    WALLET_CHECK(newReceiverCoins[1].m_status == Coin::Unconfirmed);
    WALLET_CHECK(newReceiverCoins[1].m_key_type == KeyType::Regular);


    WALLET_CHECK(newSenderCoins[0].m_amount == 5);
    WALLET_CHECK(newSenderCoins[0].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[0].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[1].m_amount == 2);
    WALLET_CHECK(newSenderCoins[1].m_status == Coin::Unspent);
    WALLET_CHECK(newSenderCoins[1].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[2].m_amount == 1);
    WALLET_CHECK(newSenderCoins[2].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[2].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[3].m_amount == 9);
    WALLET_CHECK(newSenderCoins[3].m_status == Coin::Locked);
    WALLET_CHECK(newSenderCoins[3].m_key_type == KeyType::Regular);

    WALLET_CHECK(newSenderCoins[4].m_amount == 3);
    WALLET_CHECK(newSenderCoins[4].m_status == Coin::Unconfirmed);
    WALLET_CHECK(newSenderCoins[4].m_key_type == KeyType::Regular);

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
    txId = sender.transfer_money(receiverPeer.m_walletID, 6);
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
    WALLET_CHECK(newSenderCoins.size() == 5);
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

 void TestP2PWalletReverseNegotiationST()
 {
     cout << "\nTesting p2p wallets negotiation (reverse version)...\n";

     auto node_address = io::Address::localhost().port(32125);
     auto receiver_address = io::Address::localhost().port(32124);
     auto sender_address = io::Address::localhost().port(32123);

     io::Reactor::Ptr main_reactor{ io::Reactor::create() };
     io::Reactor::Scope scope(*main_reactor);

     auto senderKeychain = createSenderKeychain();
     auto receiverKeychain = createReceiverKeychain();

     TxPeer receiverPeer = {};
     receiverPeer.m_walletID = uint64_t(12345678912345);
     receiverPeer.m_address = receiver_address.str();

     senderKeychain->addPeer(receiverPeer);

     TxPeer senderPeer = {};
     senderPeer.m_walletID = uint64_t(43412345678912345);
     senderPeer.m_address = sender_address.str();

     receiverKeychain->addPeer(senderPeer);

     WALLET_CHECK(senderKeychain->selectCoins(6, false).size() == 2);
     WALLET_CHECK(senderKeychain->getTxHistory().empty());
     WALLET_CHECK(receiverKeychain->getTxHistory().empty());

     helpers::StopWatch sw;
     sw.start();
     TestNode node{ node_address, main_reactor };
     auto sender_io = make_shared<WalletNetworkIO>(sender_address, node_address, true, receiverKeychain, main_reactor);
     auto receiver_io = make_shared<WalletNetworkIO>(receiver_address, node_address, false, receiverKeychain, main_reactor, 1000, 5000, 100);


     Wallet sender{ senderKeychain, sender_io };
     Wallet receiver{ receiverKeychain, receiver_io, [receiver_io](auto) { receiver_io->stop(); } };

     TxID txId = receiver.transfer_money(senderPeer.m_walletID, 4, 2, false);

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

     WALLET_CHECK(newSenderCoins.size() == 4);
     WALLET_CHECK(newReceiverCoins.size() == 1);
     WALLET_CHECK(newReceiverCoins[0].m_amount == 4);
     WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Unconfirmed);
     WALLET_CHECK(newReceiverCoins[0].m_key_type == KeyType::Regular);

     WALLET_CHECK(newSenderCoins[0].m_amount == 5);
     WALLET_CHECK(newSenderCoins[0].m_status == Coin::Locked);
     WALLET_CHECK(newSenderCoins[0].m_key_type == KeyType::Regular);

     WALLET_CHECK(newSenderCoins[1].m_amount == 2);
     WALLET_CHECK(newSenderCoins[1].m_status == Coin::Unspent);
     WALLET_CHECK(newSenderCoins[1].m_key_type == KeyType::Regular);

     WALLET_CHECK(newSenderCoins[2].m_amount == 1);
     WALLET_CHECK(newSenderCoins[2].m_status == Coin::Locked);
     WALLET_CHECK(newSenderCoins[2].m_key_type == KeyType::Regular);

     WALLET_CHECK(newSenderCoins[3].m_amount == 9);
     WALLET_CHECK(newSenderCoins[3].m_status == Coin::Unspent);
     WALLET_CHECK(newSenderCoins[3].m_key_type == KeyType::Regular);

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
     WALLET_CHECK(stx->m_createTime >= rtx->m_createTime);
     WALLET_CHECK(stx->m_status == rtx->m_status);
     WALLET_CHECK(stx->m_fsmState.empty());
     WALLET_CHECK(rtx->m_fsmState.empty());
     WALLET_CHECK(stx->m_sender == true);
     WALLET_CHECK(rtx->m_sender == false);

     // second transfer
     sw.start();
     txId = receiver.transfer_money(senderPeer.m_walletID, 6, 0, false);
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

     WALLET_CHECK(newSenderCoins.size() == 5);
     WALLET_CHECK(newReceiverCoins.size() == 2);

     WALLET_CHECK(newReceiverCoins[0].m_amount == 4);
     WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Unconfirmed);
     WALLET_CHECK(newReceiverCoins[0].m_key_type == KeyType::Regular);

     WALLET_CHECK(newReceiverCoins[1].m_amount == 6);
     WALLET_CHECK(newReceiverCoins[1].m_status == Coin::Unconfirmed);
     WALLET_CHECK(newReceiverCoins[1].m_key_type == KeyType::Regular);


     WALLET_CHECK(newSenderCoins[0].m_amount == 5);
     WALLET_CHECK(newSenderCoins[0].m_status == Coin::Locked);
     WALLET_CHECK(newSenderCoins[0].m_key_type == KeyType::Regular);

     WALLET_CHECK(newSenderCoins[1].m_amount == 2);
     WALLET_CHECK(newSenderCoins[1].m_status == Coin::Unspent);
     WALLET_CHECK(newSenderCoins[1].m_key_type == KeyType::Regular);

     WALLET_CHECK(newSenderCoins[2].m_amount == 1);
     WALLET_CHECK(newSenderCoins[2].m_status == Coin::Locked);
     WALLET_CHECK(newSenderCoins[2].m_key_type == KeyType::Regular);

     WALLET_CHECK(newSenderCoins[3].m_amount == 9);
     WALLET_CHECK(newSenderCoins[3].m_status == Coin::Locked);
     WALLET_CHECK(newSenderCoins[3].m_key_type == KeyType::Regular);

     WALLET_CHECK(newSenderCoins[4].m_amount == 3);
     WALLET_CHECK(newSenderCoins[4].m_status == Coin::Unconfirmed);
     WALLET_CHECK(newSenderCoins[4].m_key_type == KeyType::Regular);

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
     WALLET_CHECK(stx->m_createTime >= rtx->m_createTime);
     WALLET_CHECK(stx->m_status == rtx->m_status);
     WALLET_CHECK(stx->m_fsmState.empty());
     WALLET_CHECK(rtx->m_fsmState.empty());
     WALLET_CHECK(stx->m_sender == true);
     WALLET_CHECK(rtx->m_sender == false);


     // third transfer. no enough money should appear
     sw.start();
     txId = receiver.transfer_money(senderPeer.m_walletID, 6, 0, false);
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
     WALLET_CHECK(newSenderCoins.size() == 5);
     WALLET_CHECK(newReceiverCoins.size() == 2);

     // Tx history check. New failed tx should be added to sender and receiver
     sh = senderKeychain->getTxHistory();
     WALLET_CHECK(sh.size() == 3);
     rh = receiverKeychain->getTxHistory();
     WALLET_CHECK(rh.size() == 3);
     stx = senderKeychain->getTx(txId);
     WALLET_CHECK(stx.is_initialized());
     rtx = receiverKeychain->getTx(txId);
     WALLET_CHECK(rtx.is_initialized());

     WALLET_CHECK(rtx->m_amount == 6);
     WALLET_CHECK(rtx->m_status == TxDescription::Failed);
     WALLET_CHECK(rtx->m_fsmState.empty());
     WALLET_CHECK(rtx->m_sender == false);


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

    beam::TxID id = { 3, 65, 70 };
    TxDescription tx = {};
    tx.m_txId = id;
    tx.m_amount = 6;
    wallet::Negotiator s{ gateway, createKeychain<TestKeyChain>(), tx};
    WALLET_CHECK(*(s.current_state()) == 0);
    s.start();
    s.process_event(wallet::events::TxInitiated{});
    WALLET_CHECK(*(s.current_state()) == 2);

    Serializer ser;
    ser & s;

    auto buffer = ser.buffer();

    Deserializer der;
    der.reset(buffer.first, buffer.second);

    wallet::Negotiator s2{ gateway, createKeychain<TestKeyChain>(), {} };
    WALLET_CHECK(*(s2.current_state()) == 0);
    der & s2;
    WALLET_CHECK(*(s2.current_state()) == 2);
    s2.process_event(wallet::events::TxInvitationCompleted{ wallet::ConfirmInvitation() });
    WALLET_CHECK(*(s2.current_state()) == 3);

    ser.reset();
    ser & s2;

    buffer = ser.buffer();
    der.reset(buffer.first, buffer.second);
    der & s;
    WALLET_CHECK(*(s.current_state()) == 3);
}

struct MyMmr : public Merkle::Mmr
{
    typedef std::vector<Merkle::Hash> HashVector;
    typedef std::unique_ptr<HashVector> HashVectorPtr;

    std::vector<HashVectorPtr> m_vec;

    Merkle::Hash& get_At(size_t nIdx, uint8_t nHeight)
    {
        if (m_vec.size() <= nHeight)
            m_vec.resize(nHeight + 1);

        HashVectorPtr& ptr = m_vec[nHeight];
        if (!ptr)
            ptr.reset(new HashVector);


        HashVector& vec = *ptr;
        if (vec.size() <= nIdx)
            vec.resize(nIdx + 1);

        return vec[nIdx];
    }

    virtual void LoadElement(Merkle::Hash& hv, uint64_t nIdx, uint8_t nHeight) const override
    {
        hv = ((MyMmr*)this)->get_At(nIdx, nHeight);
    }

    virtual void SaveElement(const Merkle::Hash& hv, uint64_t nIdx, uint8_t nHeight) override
    {
        get_At(nIdx, nHeight) = hv;
    }
};

struct RollbackIO : public TestNetwork
{
    RollbackIO(IOLoop& mainLoop, const MyMmr& mmr, Height branch, Height current, unsigned step)
        : TestNetwork(mainLoop)
        , m_mmr(mmr)
        , m_branch(branch)
        , m_current(current)
        , m_step(step)
    {


    }

    void InitHdr(proto::Hdr& msg) override
    {
        msg.m_Description.m_Height = m_current;
        m_mmr.get_Hash(msg.m_Description.m_Definition);
    }

    void send_node_message(beam::proto::GetProofState&& msg) override
    {
        cout << "Rollback. GetProofState Height=" << msg.m_Height << "\n";
        Merkle::Proof proof;
        m_mmr.get_Proof(proof, msg.m_Height);
        enqueueNetworkTask([this, proof]{ m_peers[0]->handle_node_message(proto::Proof{proof}); });
    }

    void send_node_message(proto::GetMined&& data) override
    {
        Height h = m_step > 1 ? m_step * Height((m_branch - 1) / m_step) : m_branch - 1;
        assert(data.m_HeightMin == Rules::HeightGenesis || data.m_HeightMin == h);
        WALLET_CHECK(data.m_HeightMin == Rules::HeightGenesis || data.m_HeightMin == h);
        TestNetwork::send_node_message(move(data));
    }

    void close_node_connection() override
    {
        shutdown();
    }

    const MyMmr& m_mmr;
    Height m_branch;
    Height m_current;
    unsigned m_step;
};

void TestRollback(Height branch, Height current, unsigned step = 1)
{
    cout << "\nRollback from " << current << " to " << branch << " step: " << step <<'\n';
    auto db = createSqliteKeychain("wallet.db");
    
    MyMmr mmrNew, mmrOld;

    for (Height i = 0; i <= current; ++i)
    {
        Coin coin1 = { 5, Coin::Unspent, 1, 10, KeyType::Regular, i };
        Merkle::Hash hash = {};
        ECC::Hash::Processor() << i >> hash;
        coin1.m_confirmHash = hash;
        mmrOld.Append(hash);
        if (i < branch)
        {
            mmrNew.Append(hash);
        }
        else // change history
        {
            ECC::Hash::Processor() << (i + current + 1) >> hash;
            mmrNew.Append(hash);
        }
        if (i % step == 0)
        {
            db->store(coin1);
        }
    }

    Merkle::Hash newStateDefinition;
    mmrNew.get_Hash(newStateDefinition);

    Merkle::Hash oldStateDefinition;
    mmrOld.get_Hash(oldStateDefinition);

    WALLET_CHECK(newStateDefinition != oldStateDefinition);

    beam::Block::SystemState::ID id = {};
    id.m_Height = current;
    ECC::Hash::Processor() << current >> id.m_Hash;

    db->setSystemStateID(id);

    for (Height i = branch; i <= current ; ++i)
    {
        Merkle::Proof proof;
        mmrNew.get_Proof(proof, i);
        Merkle::Hash hash = {};
        ECC::Hash::Processor() << (i + current + 1) >> hash;
        Merkle::Interpret(hash, proof);
        WALLET_CHECK(hash == newStateDefinition);
    }

    for (Height i = 0; i < branch; ++i)
    {
        Merkle::Proof proof;
        mmrNew.get_Proof(proof, i);
        Merkle::Hash hash = {};
        ECC::Hash::Processor() << i >> hash;
        Merkle::Interpret(hash, proof);
        WALLET_CHECK(hash == newStateDefinition);
    }

    for (Height i = 0; i < current; ++i)
    {
        Merkle::Proof proof;
        mmrOld.get_Proof(proof, i);
        Merkle::Hash hash = {};
        ECC::Hash::Processor() << i >> hash;
        
        Merkle::Interpret(hash, proof);
        WALLET_CHECK(hash == oldStateDefinition);
    }

    IOLoop mainLoop;
    auto network = make_shared<RollbackIO>(mainLoop, mmrNew, branch, current, step);

    Wallet sender(db, network);
    
    network->registerPeer(&sender, true);
    
    mainLoop.run();
}

void TestRollback()
{
    cout << "\nTesting wallet rollback...\n";
    Height s = 10;
    for (Height i = 1; i <= s; ++i)
    {
        TestRollback(i, s);
        TestRollback(i, s, 2);
    }
    s = 11;
    for (Height i = 1; i <= s; ++i)
    {
        TestRollback(i, s);
        TestRollback(i, s, 2);
    }
    
    TestRollback(0, 1);
    TestRollback(2, 50);
    TestRollback(2, 51);
    TestRollback(93, 120);
    TestRollback(93, 120, 6);
    TestRollback(93, 120, 7);
    TestRollback(99, 100);
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
    TestP2PWalletReverseNegotiationST();
    TestWalletNegotiation(createKeychain<TestKeyChain>(), createKeychain<TestKeyChain2>());
    TestWalletNegotiation(createSenderKeychain(), createReceiverKeychain());
    TestFSM();
    TestSerializeFSM();
    TestRollback();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
