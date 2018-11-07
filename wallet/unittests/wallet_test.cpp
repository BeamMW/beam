// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LOG_VERBOSE_ENABLED
    #define LOG_VERBOSE_ENABLED 0
#endif

#include "wallet/wallet_network.h"
#include "wallet/wallet.h"
#include "wallet/secstring.h"
#include "utility/test_helpers.h"
#include "../../core/radixtree.h"
#include "../../core/unittest/mini_blockchain.h"
#include <string_view>

#include "test_helpers.h"

#include <assert.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include "core/proto.h"
#include <boost/filesystem.hpp>
#include <boost/intrusive/list.hpp>

using namespace beam;
using namespace std;
using namespace ECC;

WALLET_TEST_INIT

namespace
{
    class BaseTestKeyChain : public IKeyChain
    {
		Key::IKdf::Ptr m_pKdf;
    public:

		BaseTestKeyChain()
		{
			std::shared_ptr<HKdf> pKdf(new HKdf);
			pKdf->m_Secret.V = 10U;
			m_pKdf = pKdf;
		}


		Key::IKdf::Ptr get_Kdf() const override
		{
			return m_pKdf;
		}

        ECC::Scalar::Native calcKey(const Coin& c) const override
        {
			ECC::Scalar::Native sk;
			m_pKdf->DeriveKey(sk, c.get_Kidv());
            return sk;
        }

		void get_IdentityKey(ECC::Scalar::Native& sk) const override
		{
			sk = Zero;
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

        virtual std::vector<beam::Coin> getCoinsCreatedByTx(const TxID& txId) override { return {}; };
        void store(beam::Coin& ) override {}
        void store(std::vector<beam::Coin>& ) override {}
        void update(const vector<beam::Coin>& coins) override {}
        void update(const beam::Coin& ) override {}
        void remove(const std::vector<beam::Coin>& ) override {}
        void remove(const beam::Coin& ) override {}
        void visit(std::function<bool(const beam::Coin& coin)> ) override {}
        void setVarRaw(const char* , const void* , size_t ) override {}
        int getVarRaw(const char* , void* ) const override { return 0; }
        bool getBlob(const char* name, ByteBuffer& var) const override { return false; }
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
        boost::optional<WalletAddress> getAddress(const WalletID& id) override
        {
            return boost::optional<WalletAddress>{};
        }
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

        void clear() override {}

		void changePassword(const SecString& password) override {}

        bool setTxParameter(const TxID& txID, wallet::TxParameterID paramID, const ByteBuffer& blob) override
        {
            if (paramID < wallet::TxParameterID::PrivateFirstParam)
            {
                auto p = m_params.emplace(paramID, blob);
                return p.second;
            }
            m_params[paramID] = blob;
            return true;
        }
        bool getTxParameter(const TxID& txID, wallet::TxParameterID paramID, ByteBuffer& blob) override
        {
            auto it = m_params.find(paramID);
            if (it != m_params.end())
            {
                blob = it->second;
                return true;
            }
            return false;
        }

    protected:
        std::vector<beam::Coin> m_coins;
        std::map<wallet::TxParameterID, ByteBuffer> m_params;
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
    IKeyChain::Ptr CreateKeychain()
    {
        return std::static_pointer_cast<IKeyChain>(std::make_shared<KeychainImpl>());
    }

    IKeyStore::Ptr CreateBbsKeystore(const string& path, const string& pass, WalletID& outWalletID)
    {
        boost::filesystem::remove_all(path);
        IKeyStore::Options options;
        options.flags = IKeyStore::Options::local_file | IKeyStore::Options::enable_all_keys;
        options.fileName = path;

        auto ks = IKeyStore::create(options, pass.c_str(), pass.size());
        //WalletID senderID = {};
        ks->gen_keypair(outWalletID);
        ks->save_keypair(outWalletID, true);
        return ks;
    }

    IKeyChain::Ptr createSqliteKeychain(const string& path)
    {
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = Zero;
        if (boost::filesystem::exists(path))
        {
            boost::filesystem::remove(path);
        }
               
        auto keychain = Keychain::init(path, string("pass123"), seed);
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
        void on_tx_completed(const TxID&) override
        {
            cout << __FUNCTION__ << "\n";
        }

        void register_tx(const TxID& , Transaction::Ptr) override
        {
            cout << "sent tx registration request\n";
        }

        void confirm_outputs(const vector<Coin>&) override
        {
            cout << "confirm outputs\n";
        }

        void confirm_kernel(const TxID&, const TxKernel&) override
        {
            cout << "confirm kernel\n";
        }

        bool get_tip(Block::SystemState::Full& state) const override
        {
            return true;
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
            if (m_uses == 1)
            {
                m_shutdown.store(false);
                m_cv.notify_all();
            }
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

        void step()
        {
            deque<Task> tasks;
            {
                unique_lock<mutex> lock(m_tasksMutex);
                if (!m_tasks.empty())
                {
                    tasks.swap(m_tasks);
                }
            }
            for (auto& task : tasks)
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
/*    struct TestNetworkBase : public NetworkIOBase
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
				InitHdr(*walletPeer);
        }

        virtual void InitHdr(proto::FlyClient& fc)
        {
			Block::SystemState::Full hdr = { 0 };
            hdr.m_Height = 134;
			fc.m_Hist[hdr.m_Height] = hdr;
			fc.OnNewTip();
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

        virtual void onSent()
        {

        }

        void set_node_address(io::Address nodeAddress) override {}

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

        void send_tx_message(const WalletID& to, beam::wallet::SetTxParameter&& data) override
        {
            cout << "SetTxParameter\n";
            send(1, to, move(data));
        }

        void send_node_message(proto::NewTransaction&& data) override
        {
            cout << "NewTransaction\n";

            for (const auto& input : data.m_Transaction->m_vInputs)
            {
                RemoveCommitment(input->m_Commitment);
            }
            for (const auto& output : data.m_Transaction->m_vOutputs)
            {
                AddCommitment(output->m_Commitment);
            }

            enqueueNetworkTask([this, data] {m_peers[0]->handle_node_message(proto::Boolean{ true }); });
        }

        set<ECC::Point> m_Commitments;

        void AddCommitment(const ECC::Point& c)
        {
            m_Commitments.insert(c);
        }

        void RemoveCommitment(const ECC::Point& c)
        {
            m_Commitments.erase(c);
        }

        bool HasCommitment(const ECC::Point& c)
        {
            return m_Commitments.find(c) != m_Commitments.end();
        }

        void send_node_message(proto::GetMined&& data) override
        {
            cout << "GetMined\n";
            enqueueNetworkTask([this] {m_peers[0]->handle_node_message(proto::Mined{ }); });
        }

        void send_node_message(proto::Recover&& data) override
        {
            cout << "Recover\n";
            enqueueNetworkTask([this] {m_peers[0]->handle_node_message(proto::Recovered{ }); });
        }

        void send_node_message(proto::GetProofUtxo&& msg) override
        {
            cout << "GetProofUtxo\n";

            Input::Proof proof = {};
            if (HasCommitment(msg.m_Utxo.m_Commitment))
            {
                proof.m_State.m_Maturity = 134 + 60;
                enqueueNetworkTask([this, proof = move(proof)]() {m_peers[0]->handle_node_message(proto::ProofUtxo({ proof })); });
            }
            else
            {
                enqueueNetworkTask([this, proof = move(proof)]() {m_peers[0]->handle_node_message(proto::ProofUtxo()); });
            }
        }

        void send_node_message(beam::proto::GetProofKernel&&) override
        {
            cout << "GetProofKernel\n";
            enqueueNetworkTask([this] {
                m_peers[0]->handle_node_message(proto::ProofKernel{});
            });
        }

        void connect_node() override
        {

        }

        void close_node_connection() override
        {
        }

        void new_own_address(const WalletID&) override
        {

        }

        void address_deleted(const WalletID& address) override
        {

        }
    };
*/
}

class AsyncProcessor
{
	io::Timer::Ptr m_pTimer;
	bool m_bPending = false;

public:
	virtual void Proceed() = 0;

	void PostAsync()
	{
		if (!m_bPending)
		{
			if (!m_pTimer)
				m_pTimer = io::Timer::create(io::Reactor::get_Current());

			m_bPending = true;
			m_pTimer->start(0, false, [this]() {
				assert(m_bPending);
				m_bPending = false;
				Proceed();
			});
		}
	}
};

struct TestWalletRig
{
    TestWalletRig(const string& name, IKeyChain::Ptr keychain, io::Reactor::Ptr reactor, Wallet::TxCompletedAction&& action = Wallet::TxCompletedAction())
        : m_Keychain{keychain}
        , m_BBSKeystore{ CreateBbsKeystore(name + "-bbs", "123", m_WalletID) }
		, m_Wallet{ m_Keychain, move(action) }
		, m_NodeNetwork(m_Wallet)
    {
    }

    vector<Coin> GetCoins()
    {
        vector<Coin> coins;
        m_Keychain->visit([&coins](const Coin& c)->bool
        {
            coins.push_back(c);
            return true;
        });
        return coins;
    }

    WalletID m_WalletID;
    IKeyChain::Ptr m_Keychain;
    IKeyStore::Ptr m_BBSKeystore;
    int m_CompletedCount{1};
	Wallet m_Wallet;
	proto::FlyClient::NetworkStd m_NodeNetwork;
};

struct TestWalletNetwork
	:public IWallet::INetwork
	, public AsyncProcessor
{
	struct Entry
	{
		IWallet* m_pSink;
		std::deque<std::pair<WalletID, wallet::SetTxParameter> > m_Msgs;
	};

	typedef std::map<WalletID, Entry> WalletMap;
	WalletMap m_Map;

	virtual void Send(const WalletID& peerID, wallet::SetTxParameter&& msg) override
	{
		WalletMap::iterator it = m_Map.find(peerID);
		WALLET_CHECK(m_Map.end() != it);

		it->second.m_Msgs.push_back(std::make_pair(peerID, std::move(msg)));

		PostAsync();
	}

	virtual void Proceed() override
	{
		for (WalletMap::iterator it = m_Map.begin(); m_Map.end() != it; it++)
			for (Entry& v = it->second; !v.m_Msgs.empty(); v.m_Msgs.pop_front())
				v.m_pSink->OnWalletMsg(v.m_Msgs.front().first, std::move(v.m_Msgs.front().second));
	}
};

struct TestNodeNetwork
	:public proto::FlyClient::INetwork
	,public AsyncProcessor
{
	typedef proto::FlyClient::Request Request;

	proto::FlyClient& m_Client;

	MiniBlockChain m_Mcm;

	TestNodeNetwork(proto::FlyClient& x) :m_Client(x) {}

	typedef std::deque<Request::Ptr> Queue;
	Queue m_queReqs;

	virtual void Connect() override {}
	virtual void Disconnect() override {}

	virtual void PostRequest(Request& r) override
	{
		if (!r.m_pTrg)
			r.m_pTrg = &m_Client;

		m_queReqs.push_back(&r);
		PostAsync();
	}

	virtual void Proceed() override
	{
		Queue q;
		q.swap(m_queReqs);

		for (; !q.empty(); q.pop_front())
		{
			Request& r = *q.front();
			PostProcess(r);
			m_Client.OnRequestComplete(r);
		}
	}

	virtual void PostProcess(Request& r)
	{
		switch (r.get_Type())
		{
		case Request::Type::Transaction:
			{
				proto::FlyClient::RequestTransaction& v = static_cast<proto::FlyClient::RequestTransaction&>(r);
				v.m_Res.m_Value = true;
			}
			break;

		case Request::Type::Kernel:
			{
				// kernel proofs are assumed to be valid in the client. It'll also check if the proof is empty
				proto::FlyClient::RequestKernel& v = static_cast<proto::FlyClient::RequestKernel&>(r);
				v.m_Res.m_Proof.push_back(Merkle::Node(false, Zero));
			}
			break;
		}
	}

	void AddTip()
	{
		m_Mcm.Add();
		m_Client.m_Hist[m_Mcm.m_vStates.back().m_Hdr.m_Height] = m_Mcm.m_vStates.back().m_Hdr;
		m_Client.OnNewTip();
	}
};

void TestWalletNegotiation(IKeyChain::Ptr senderKeychain, IKeyChain::Ptr receiverKeychain)
{
    cout << "\nTesting wallets negotiation...\n";

	io::Reactor::Ptr mainReactor{ io::Reactor::create() };
	io::Reactor::Scope scope(*mainReactor);

    WalletID receiver_id, sender_id;
    receiver_id = 4U;
    sender_id = 5U;

    int count = 0;
    auto f = [&count](const auto& /*id*/)
    {
		if (++count >= 2)
			io::Reactor::get_Current().stop();
    };

    Wallet sender(senderKeychain, f);
    Wallet receiver(receiverKeychain, f);

	TestWalletNetwork twn;
	TestNodeNetwork netNodeS(sender), netNodeR(receiver);

	sender.set_Network(netNodeS, twn);
	receiver.set_Network(netNodeR, twn);

	twn.m_Map[sender_id].m_pSink = &sender;
	twn.m_Map[receiver_id].m_pSink = &receiver;

	netNodeS.AddTip();
	netNodeR.AddTip();

    sender.transfer_money(sender_id, receiver_id, 6, 1, true, {});
	mainReactor->run();

	WALLET_CHECK(count == 2);
}

class TestNode
{
public:
    TestNode(io::Address address)
    {
        m_Server.Listen(address);
		while (m_mcm.m_vStates.size() < 145)
			AddBlock();
    }

    ~TestNode() {
        KillAll();
    }

    void KillAll()
    {
        while (!m_lstClients.empty())
            DeleteClient(&m_lstClients.front());
    }

	MiniBlockChain m_mcm;

	UtxoTree m_Utxos;
	RadixHashOnlyTree m_Kernels;

	void AddBlock()
	{
		Merkle::Hash hvUtxo, hvKrn;
		m_Utxos.get_Hash(hvUtxo);
		m_Kernels.get_Hash(hvKrn);

		Merkle::Interpret(m_mcm.m_hvLive, hvUtxo, hvKrn);
		m_mcm.Add();

		for (auto it = m_lstClients.begin(); m_lstClients.end() != it; it++)
		{
			Client& c = *it; // maybe this
			if (c.IsSecureOut())
			{
				proto::NewTip msg;
				msg.m_Description = m_mcm.m_vStates.back().m_Hdr;
				c.Send(msg);
			}
		}
	}

	void AddKernel(const TxKernel& krn)
	{
		Merkle::Hash hvKrn;
		krn.get_Hash(hvKrn);
		AddKernel(hvKrn);
	}

	void AddKernel(const Merkle::Hash& hvKrn)
	{
		RadixHashOnlyTree::Cursor cu;
		bool bCreate = true;
		m_Kernels.Find(cu, hvKrn, bCreate);
		WALLET_CHECK(bCreate);
	}

private:

    struct Client
        :public proto::NodeConnection
        ,public boost::intrusive::list_base_hook<>
    {
        TestNode& m_This;
        bool m_Subscribed;

        Client(TestNode& n)
            : m_This(n)
            , m_Subscribed(false)
        {
        }


        // protocol handler
		void OnConnectedSecure() override
		{
			ECC::Scalar::Native sk;
			sk = 23U;
			ProveID(sk, proto::IDType::Node);

			proto::Config msgCfg;
			msgCfg.m_CfgChecksum = Rules::get().Checksum;
			msgCfg.m_SpreadingTransactions = true;
			msgCfg.m_Bbs = true;
			msgCfg.m_SendPeers = true;
			Send(msgCfg);
		}

        void OnMsg(proto::NewTransaction&& data) override
        {
            for (const auto& input : data.m_Transaction->m_vInputs)
            {
                m_This.RemoveCommitment(input->m_Commitment);
            }
            for (const auto& output : data.m_Transaction->m_vOutputs)
            {
                m_This.AddCommitment(output->m_Commitment);
            }

            Send(proto::Boolean{ true });

			for (size_t i = 0; i < data.m_Transaction->m_vKernelsOutput.size(); i++)
				m_This.AddKernel(*data.m_Transaction->m_vKernelsOutput[i]);

			m_This.AddBlock();
        }

        void OnMsg(proto::GetProofUtxo&& data) override
        {
			struct Traveler :public UtxoTree::ITraveler
			{
				proto::ProofUtxo m_Msg;
				UtxoTree* m_pTree;
				Merkle::Hash m_hvHistory;
				Merkle::Hash m_hvKernels;

				virtual bool OnLeaf(const RadixTree::Leaf& x) override {

					const UtxoTree::MyLeaf& v = (UtxoTree::MyLeaf&) x;
					UtxoTree::Key::Data d;
					d = v.m_Key;

					m_Msg.m_Proofs.resize(m_Msg.m_Proofs.size() + 1);
					Input::Proof& ret = m_Msg.m_Proofs.back();

					ret.m_State.m_Count = v.m_Value.m_Count;
					ret.m_State.m_Maturity = d.m_Maturity;
					m_pTree->get_Proof(ret.m_Proof, *m_pCu);

					ret.m_Proof.reserve(ret.m_Proof.size() + 2);

					ret.m_Proof.resize(ret.m_Proof.size() + 1);
					ret.m_Proof.back().first = true;
					ret.m_Proof.back().second = m_hvKernels;

					ret.m_Proof.resize(ret.m_Proof.size() + 1);
					ret.m_Proof.back().first = false;
					ret.m_Proof.back().second = m_hvHistory;

					return m_Msg.m_Proofs.size() < Input::Proof::s_EntriesMax;
				}
			} t;

			t.m_pTree = &m_This.m_Utxos;
			m_This.m_Kernels.get_Hash(t.m_hvKernels);
			m_This.m_mcm.m_Mmr.get_Hash(t.m_hvHistory);

			UtxoTree::Cursor cu;
			t.m_pCu = &cu;

			// bounds
			UtxoTree::Key kMin, kMax;

			UtxoTree::Key::Data d;
			d.m_Commitment = data.m_Utxo.m_Commitment;
			d.m_Maturity = data.m_MaturityMin;
			kMin = d;
			d.m_Maturity = Height(-1);
			kMax = d;

			t.m_pBound[0] = kMin.m_pArr;
			t.m_pBound[1] = kMax.m_pArr;

			t.m_pTree->Traverse(t);

			Send(t.m_Msg);
        }

        void OnMsg(proto::GetProofKernel&& data) override
        {
			proto::ProofKernel msgOut;

			RadixHashOnlyTree& t = m_This.m_Kernels;

			RadixHashOnlyTree::Cursor cu;
			bool bCreate = false;
			if (t.Find(cu, data.m_ID, bCreate))
			{
				t.get_Proof(msgOut.m_Proof, cu);
				msgOut.m_Proof.reserve(msgOut.m_Proof.size() + 2);

				msgOut.m_Proof.resize(msgOut.m_Proof.size() + 1);
				msgOut.m_Proof.back().first = false;
				m_This.m_Utxos.get_Hash(msgOut.m_Proof.back().second);

				msgOut.m_Proof.resize(msgOut.m_Proof.size() + 1);
				msgOut.m_Proof.back().first = false;
				m_This.m_mcm.m_Mmr.get_Hash(msgOut.m_Proof.back().second);
			}

            Send(msgOut);
        }

        void OnMsg(proto::Config&& /*data*/) override
        {
            proto::NewTip msg;

			msg.m_Description = m_This.m_mcm.m_vStates.back().m_Hdr;
            Send(move(msg));
        }

        void OnMsg(proto::GetMined&&) override
        {
            Send(proto::Mined{});
        }

        void OnMsg(proto::Recover&&) override
        {
            Send(proto::Recovered{});
        }

        void OnMsg(proto::GetProofState&&) override
        {
            Send(proto::ProofState{});
        }

		void OnMsg(proto::GetProofChainWork&& msg) override
		{
			proto::ProofChainWork msgOut;
			msgOut.m_Proof.m_LowerBound = msg.m_LowerBound;
			msgOut.m_Proof.m_hvRootLive = m_This.m_mcm.m_hvLive;
			msgOut.m_Proof.Create(m_This.m_mcm.m_Source, m_This.m_mcm.m_vStates.back().m_Hdr);

			Send(msgOut);
		}

        void OnMsg(proto::BbsSubscribe&& msg) override
        {
            if (m_Subscribed)
                return;
            m_Subscribed = true;

            for (const auto& m : m_This.m_bbs)
            {
                Send(m);
            }
        }

        void OnMsg(proto::BbsMsg&& msg) override
        {
            m_This.m_bbs.push_back(msg);

            for (ClientList::iterator it = m_This.m_lstClients.begin(); m_This.m_lstClients.end() != it; it++)
            {
                if (it.pointed_node() != this)
                {
                    Client& c = *it;
                    if (c.m_Subscribed)
                    {
                        c.Send(msg);
                    }
                }
            }
        }

        void OnDisconnect(const DisconnectReason& r) override
        {
            switch (r.m_Type)
            {
            case DisconnectReason::Protocol:
            case DisconnectReason::ProcessingExc:
                LOG_ERROR() << "Disconnect: " << r;
                g_failureCount++;

            default: // suppress warning
                break;
            }

            m_This.DeleteClient(this);
        }
    };

    typedef boost::intrusive::list<Client> ClientList;
    ClientList m_lstClients;

    std::vector<proto::BbsMsg> m_bbs;

    void DeleteClient(Client* client)
    {
        m_lstClients.erase(ClientList::s_iterator_to(*client));
        delete client;
    }

    bool AddCommitment(const ECC::Point& c)
    {
		UtxoTree::Key::Data d;
		d.m_Commitment = c;
		d.m_Maturity = m_mcm.m_vStates.back().m_Hdr.m_Height;

		UtxoTree::Key key;
		key = d;

		UtxoTree::Cursor cu;
		bool bCreate = true;
		UtxoTree::MyLeaf* p = m_Utxos.Find(cu, key, bCreate);

		cu.Invalidate();

		if (bCreate)
			p->m_Value.m_Count = 1;
		else
		{
			// protect again overflow attacks, though it's highly unlikely (Input::Count is currently limited to 32 bits, it'd take millions of blocks)
			Input::Count nCountInc = p->m_Value.m_Count + 1;
			if (!nCountInc)
				return false;

			p->m_Value.m_Count = nCountInc;
		}

		return true;
	}

    bool RemoveCommitment(const ECC::Point& c)
    {
		UtxoTree::Cursor cu;
		UtxoTree::MyLeaf* p;
		UtxoTree::Key::Data d;
		d.m_Commitment = c;

		struct Traveler :public UtxoTree::ITraveler {
			virtual bool OnLeaf(const RadixTree::Leaf& x) override {
				return false; // stop iteration
			}
		} t;


		UtxoTree::Key kMin, kMax;

		d.m_Maturity = 0;
		kMin = d;
		d.m_Maturity = m_mcm.m_vStates.back().m_Hdr.m_Height;
		kMax = d;

		t.m_pCu = &cu;
		t.m_pBound[0] = kMin.m_pArr;
		t.m_pBound[1] = kMax.m_pArr;

		if (m_Utxos.Traverse(t))
			return false;

		p = &(UtxoTree::MyLeaf&) cu.get_Leaf();

		d = p->m_Key;
		assert(d.m_Commitment == c);
		assert(p->m_Value.m_Count); // we don't store zeroes

		if (!--p->m_Value.m_Count)
			m_Utxos.Delete(cu);
		else
			cu.Invalidate();

		return true;
	}

    struct Server
        :public proto::NodeConnection::Server
    {
        IMPLEMENT_GET_PARENT_OBJ(TestNode, m_Server)

        void OnAccepted(io::TcpStream::Ptr&& newStream, int errorCode) override
        {
            if (newStream)
            {
                Client* p = new Client(get_ParentObj());
                get_ParentObj().m_lstClients.push_back(*p);

                p->Accept(std::move(newStream));
                p->SecureConnect();
            }
        }
    } m_Server;
};
//
//void TestTxToHimself()
//{
//    cout << "\nTesting Tx to himself...\n";
//
//    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
//    io::Reactor::Scope scope(*mainReactor);
//
//    string keystorePass = "123";
//    WalletID senderID = {};
//    auto senderBbsKeys = CreateBbsKeystore("sender-bbs", keystorePass, senderID);
//
//    WalletID receiverID = {};
//    senderBbsKeys->gen_keypair(receiverID);
//
//    auto senderKeychain = createSqliteKeychain("sender_wallet.db");
//
//    // add own address
//    WalletAddress own_address = {};
//    own_address.m_walletID = receiverID;
//    own_address.m_label = "test label";
//    own_address.m_category = "test category";
//    own_address.m_createTime = beam::getTimestamp();
//    own_address.m_duration = 23;
//    own_address.m_own = true;
//
//    senderKeychain->saveAddress(own_address);
//
//    // add coin with keyType - Coinbase
//    beam::Amount coin_amount = 40;
//    Coin coin(coin_amount);
//    coin.m_maturity = 0;
//    coin.m_status = Coin::Unspent;
//    coin.m_key_type = Key::Type::Coinbase;
//    senderKeychain->store(coin);
//
//    auto coins = senderKeychain->selectCoins(24, false);
//    WALLET_CHECK(coins.size() == 1);
//    WALLET_CHECK(coins[0].m_key_type == Key::Type::Coinbase);
//    WALLET_CHECK(coins[0].m_status == Coin::Unspent);
//    WALLET_CHECK(senderKeychain->getTxHistory().empty());
//
//    auto nodeAddress = io::Address::localhost().port(32125);
//    TestNode node{ nodeAddress };
//    auto sender_io = make_shared<WalletNetworkIO>(nodeAddress, senderKeychain, senderBbsKeys, mainReactor, 1000, 2000);
//    TestWallet sender{ senderKeychain, sender_io, true, [sender_io](auto) { sender_io->stop(); } };
//    helpers::StopWatch sw;
//
//    sw.start();
//    TxID txId = sender.transfer_money(senderID, receiverID, 24, 2);
//    mainReactor->run();
//    sw.stop();
//
//    cout << "Transfer elapsed time: " << sw.milliseconds() << " ms\n";
//
//    // check Tx
//    auto txHistory = senderKeychain->getTxHistory();
//    WALLET_CHECK(txHistory.size() == 1);
//    WALLET_CHECK(txHistory[0].m_txId == txId);
//    WALLET_CHECK(txHistory[0].m_amount == 24);
//    WALLET_CHECK(txHistory[0].m_change == 14);
//    WALLET_CHECK(txHistory[0].m_fee == 2);
//    WALLET_CHECK(txHistory[0].m_status == TxStatus::Completed);
//
//    // check coins
//    vector<Coin> newSenderCoins;
//    senderKeychain->visit([&newSenderCoins](const Coin& c)->bool
//    {
//        newSenderCoins.push_back(c);
//        return true;
//    });
//
//    WALLET_CHECK(newSenderCoins.size() == 3);
//    WALLET_CHECK(newSenderCoins[0].m_key_type == Key::Type::Coinbase);
//    WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
//
//    WALLET_CHECK(newSenderCoins[1].m_key_type == Key::Type::Regular);
//    WALLET_CHECK(newSenderCoins[1].m_status == Coin::Unspent);
//    WALLET_CHECK(newSenderCoins[1].m_amount == 14);
//
//    WALLET_CHECK(newSenderCoins[2].m_key_type == Key::Type::Regular);
//    WALLET_CHECK(newSenderCoins[2].m_status == Coin::Unspent);
//    WALLET_CHECK(newSenderCoins[2].m_amount == 24);
//
//    cout << "\nFinish of testing Tx to himself...\n";
//}
//
void TestP2PWalletNegotiationST()
{
    cout << "\nTesting p2p wallets negotiation single thread...\n";

    auto nodeAddress = io::Address::localhost().port(32125);

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    int completedCount = 2;
    auto f = [&completedCount, mainReactor](auto)
    {
        --completedCount;
        if (completedCount == 0)
        {
            mainReactor->stop();
            completedCount = 2;
        }
    };

    TestWalletRig sender("sender", createSenderKeychain(), mainReactor, f);
    TestWalletRig receiver("receiver", createReceiverKeychain(), mainReactor, f);

	TestWalletNetwork twn;
	sender.m_NodeNetwork.m_Cfg.m_vNodes.push_back(nodeAddress);
	receiver.m_NodeNetwork.m_Cfg.m_vNodes.push_back(nodeAddress);

	twn.m_Map[sender.m_WalletID].m_pSink = &sender.m_Wallet;
	twn.m_Map[receiver.m_WalletID].m_pSink = &receiver.m_Wallet;


	sender.m_Wallet.set_Network(sender.m_NodeNetwork, twn);
	receiver.m_Wallet.set_Network(receiver.m_NodeNetwork, twn);

    WALLET_CHECK(sender.m_Keychain->selectCoins(6, false).size() == 2);
    WALLET_CHECK(sender.m_Keychain->getTxHistory().empty());
    WALLET_CHECK(receiver.m_Keychain->getTxHistory().empty());

    helpers::StopWatch sw;
    TestNode node{ nodeAddress };
    sw.start();

	sender.m_NodeNetwork.Connect();
	receiver.m_NodeNetwork.Connect();

    TxID txId = sender.m_Wallet.transfer_money(sender.m_WalletID, receiver.m_WalletID, 4, 2);

    mainReactor->run();
    sw.stop();
    cout << "First transfer elapsed time: " << sw.milliseconds() << " ms\n";

    // check coins
    vector<Coin> newSenderCoins = sender.GetCoins();
    vector<Coin> newReceiverCoins = receiver.GetCoins();

    WALLET_CHECK(newSenderCoins.size() == 4);
    WALLET_CHECK(newReceiverCoins.size() == 1);
    WALLET_CHECK(newReceiverCoins[0].m_amount == 4);
    WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Unspent);
    WALLET_CHECK(newReceiverCoins[0].m_key_type == Key::Type::Regular);

    WALLET_CHECK(newSenderCoins[0].m_amount == 5);
    WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
    WALLET_CHECK(newSenderCoins[0].m_key_type == Key::Type::Regular);

    WALLET_CHECK(newSenderCoins[1].m_amount == 2);
    WALLET_CHECK(newSenderCoins[1].m_status == Coin::Unspent);
    WALLET_CHECK(newSenderCoins[1].m_key_type == Key::Type::Regular);

    WALLET_CHECK(newSenderCoins[2].m_amount == 1);
    WALLET_CHECK(newSenderCoins[2].m_status == Coin::Spent);
    WALLET_CHECK(newSenderCoins[2].m_key_type == Key::Type::Regular);

    WALLET_CHECK(newSenderCoins[3].m_amount == 9);
    WALLET_CHECK(newSenderCoins[3].m_status == Coin::Unspent);
    WALLET_CHECK(newSenderCoins[3].m_key_type == Key::Type::Regular);

    // Tx history check
    auto sh = sender.m_Keychain->getTxHistory();
    WALLET_CHECK(sh.size() == 1);
    auto rh = receiver.m_Keychain->getTxHistory();
    WALLET_CHECK(rh.size() == 1);
    auto stx = sender.m_Keychain->getTx(txId);
    WALLET_CHECK(stx.is_initialized());
    auto rtx = receiver.m_Keychain->getTx(txId);
    WALLET_CHECK(rtx.is_initialized());

    WALLET_CHECK(stx->m_txId == rtx->m_txId);
    WALLET_CHECK(stx->m_amount == rtx->m_amount);
    WALLET_CHECK(stx->m_status == TxStatus::Completed);
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
    txId = sender.m_Wallet.transfer_money(sender.m_WalletID, receiver.m_WalletID, 6);
    mainReactor->run();
    sw.stop();
    cout << "Second transfer elapsed time: " << sw.milliseconds() << " ms\n";

    // check coins
    newSenderCoins = sender.GetCoins();
    newReceiverCoins = receiver.GetCoins();

    WALLET_CHECK(newSenderCoins.size() == 5);
    WALLET_CHECK(newReceiverCoins.size() == 2);

    WALLET_CHECK(newReceiverCoins[0].m_amount == 4);
    WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Unspent);
    WALLET_CHECK(newReceiverCoins[0].m_key_type == Key::Type::Regular);

    WALLET_CHECK(newReceiverCoins[1].m_amount == 6);
    WALLET_CHECK(newReceiverCoins[1].m_status == Coin::Unspent);
    WALLET_CHECK(newReceiverCoins[1].m_key_type == Key::Type::Regular);


    WALLET_CHECK(newSenderCoins[0].m_amount == 5);
    WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
    WALLET_CHECK(newSenderCoins[0].m_key_type == Key::Type::Regular);

    WALLET_CHECK(newSenderCoins[1].m_amount == 2);
    WALLET_CHECK(newSenderCoins[1].m_status == Coin::Unspent);
    WALLET_CHECK(newSenderCoins[1].m_key_type == Key::Type::Regular);

    WALLET_CHECK(newSenderCoins[2].m_amount == 1);
    WALLET_CHECK(newSenderCoins[2].m_status == Coin::Spent);
    WALLET_CHECK(newSenderCoins[2].m_key_type == Key::Type::Regular);

    WALLET_CHECK(newSenderCoins[3].m_amount == 9);
    WALLET_CHECK(newSenderCoins[3].m_status == Coin::Spent);
    WALLET_CHECK(newSenderCoins[3].m_key_type == Key::Type::Regular);

    WALLET_CHECK(newSenderCoins[4].m_amount == 3);
    WALLET_CHECK(newSenderCoins[4].m_status == Coin::Unspent);
    WALLET_CHECK(newSenderCoins[4].m_key_type == Key::Type::Regular);

    // Tx history check
    sh = sender.m_Keychain->getTxHistory();
    WALLET_CHECK(sh.size() == 2);
    rh = receiver.m_Keychain->getTxHistory();
    WALLET_CHECK(rh.size() == 2);
    stx = sender.m_Keychain->getTx(txId);
    WALLET_CHECK(stx.is_initialized());
    rtx = receiver.m_Keychain->getTx(txId);
    WALLET_CHECK(rtx.is_initialized());

    WALLET_CHECK(stx->m_txId == rtx->m_txId);
    WALLET_CHECK(stx->m_amount == rtx->m_amount);
    WALLET_CHECK(stx->m_status == TxStatus::Completed);
    WALLET_CHECK(stx->m_message == rtx->m_message);
    WALLET_CHECK(stx->m_createTime <= rtx->m_createTime);
    WALLET_CHECK(stx->m_status == rtx->m_status);
    WALLET_CHECK(stx->m_fsmState.empty());
    WALLET_CHECK(rtx->m_fsmState.empty());
    WALLET_CHECK(stx->m_sender == true);
    WALLET_CHECK(rtx->m_sender == false);


    // third transfer. no enough money should appear
    sw.start();
    completedCount = 1;// only one wallet takes part in tx
    txId = sender.m_Wallet.transfer_money(sender.m_WalletID, receiver.m_WalletID, 6);
    mainReactor->run();
    sw.stop();
    cout << "Third transfer elapsed time: " << sw.milliseconds() << " ms\n";
    
    // check coins
    newSenderCoins = sender.GetCoins();
    newReceiverCoins = receiver.GetCoins();

    // no coins 
    WALLET_CHECK(newSenderCoins.size() == 5);
    WALLET_CHECK(newReceiverCoins.size() == 2);

    // Tx history check. New failed tx should be added to sender
    sh = sender.m_Keychain->getTxHistory();
    WALLET_CHECK(sh.size() == 3);
    rh = receiver.m_Keychain->getTxHistory();
    WALLET_CHECK(rh.size() == 2);
    stx = sender.m_Keychain->getTx(txId);
    WALLET_CHECK(stx.is_initialized());
    rtx = receiver.m_Keychain->getTx(txId);
    WALLET_CHECK(!rtx.is_initialized());

    WALLET_CHECK(stx->m_amount == 6);
    WALLET_CHECK(stx->m_status == TxStatus::Failed);
    WALLET_CHECK(stx->m_fsmState.empty());
    WALLET_CHECK(stx->m_sender == true);
}
//
//void TestP2PWalletReverseNegotiationST()
//{
//    cout << "\nTesting p2p wallets negotiation (reverse version)...\n";
//
//    auto nodeAddress = io::Address::localhost().port(32125);
//
//    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
//    io::Reactor::Scope scope(*mainReactor);
//
//    int completedCount = 2;
//    auto f = [&completedCount, mainReactor](auto)
//    {
//        --completedCount;
//        if (completedCount == 0)
//        {
//            mainReactor->stop();
//            completedCount = 2;
//        }
//    };
//
//    TestWalletRig sender("sender", createSenderKeychain(), mainReactor, f);
//    TestWalletRig receiver("receiver", createReceiverKeychain(), mainReactor, f);
//  
//    WALLET_CHECK(sender.m_Keychain->selectCoins(6, false).size() == 2);
//    WALLET_CHECK(sender.m_Keychain->getTxHistory().empty());
//    WALLET_CHECK(receiver.m_Keychain->getTxHistory().empty());
//
//    helpers::StopWatch sw;
//    sw.start();
//    TestNode node{ nodeAddress };
//
//    TxID txId = receiver.m_Wallet.transfer_money(receiver.m_WalletID, sender.m_WalletID, 4, 2, false);
//
//    mainReactor->run();
//    sw.stop();
//    cout << "First transfer elapsed time: " << sw.milliseconds() << " ms\n";
//
//    // check coins
//    vector<Coin> newSenderCoins = sender.GetCoins();
//    vector<Coin> newReceiverCoins = receiver.GetCoins();
//
//    WALLET_CHECK(newSenderCoins.size() == 4);
//    WALLET_CHECK(newReceiverCoins.size() == 1);
//    WALLET_CHECK(newReceiverCoins[0].m_amount == 4);
//    WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Unspent);
//    WALLET_CHECK(newReceiverCoins[0].m_key_type == Key::Type::Regular);
//
//    WALLET_CHECK(newSenderCoins[0].m_amount == 5);
//    WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
//    WALLET_CHECK(newSenderCoins[0].m_key_type == Key::Type::Regular);
//
//    WALLET_CHECK(newSenderCoins[1].m_amount == 2);
//    WALLET_CHECK(newSenderCoins[1].m_status == Coin::Unspent);
//    WALLET_CHECK(newSenderCoins[1].m_key_type == Key::Type::Regular);
//
//    WALLET_CHECK(newSenderCoins[2].m_amount == 1);
//    WALLET_CHECK(newSenderCoins[2].m_status == Coin::Spent);
//    WALLET_CHECK(newSenderCoins[2].m_key_type == Key::Type::Regular);
//
//    WALLET_CHECK(newSenderCoins[3].m_amount == 9);
//    WALLET_CHECK(newSenderCoins[3].m_status == Coin::Unspent);
//    WALLET_CHECK(newSenderCoins[3].m_key_type == Key::Type::Regular);
//
//    // Tx history check
//    auto sh = sender.m_Keychain->getTxHistory();
//    WALLET_CHECK(sh.size() == 1);
//    auto rh = receiver.m_Keychain->getTxHistory();
//    WALLET_CHECK(rh.size() == 1);
//    auto stx = sender.m_Keychain->getTx(txId);
//    WALLET_CHECK(stx.is_initialized());
//    auto rtx = receiver.m_Keychain->getTx(txId);
//    WALLET_CHECK(rtx.is_initialized());
//
//    WALLET_CHECK(stx->m_txId == rtx->m_txId);
//    WALLET_CHECK(stx->m_amount == rtx->m_amount);
//    WALLET_CHECK(stx->m_status == TxStatus::Completed);
//    WALLET_CHECK(stx->m_fee == rtx->m_fee);
//    WALLET_CHECK(stx->m_message == rtx->m_message);
//    WALLET_CHECK(stx->m_createTime >= rtx->m_createTime);
//    WALLET_CHECK(stx->m_status == rtx->m_status);
//    WALLET_CHECK(stx->m_fsmState.empty());
//    WALLET_CHECK(rtx->m_fsmState.empty());
//    WALLET_CHECK(stx->m_sender == true);
//    WALLET_CHECK(rtx->m_sender == false);
//
//    // second transfer
//    sw.start();
//    txId = receiver.m_Wallet.transfer_money(receiver.m_WalletID, sender.m_WalletID, 6, 0, false);
//    mainReactor->run();
//    sw.stop();
//    cout << "Second transfer elapsed time: " << sw.milliseconds() << " ms\n";
//
//    // check coins
//    newSenderCoins = sender.GetCoins();
//    newReceiverCoins = receiver.GetCoins();
//
//    WALLET_CHECK(newSenderCoins.size() == 5);
//    WALLET_CHECK(newReceiverCoins.size() == 2);
//
//    WALLET_CHECK(newReceiverCoins[0].m_amount == 4);
//    WALLET_CHECK(newReceiverCoins[0].m_status == Coin::Unspent);
//    WALLET_CHECK(newReceiverCoins[0].m_key_type == Key::Type::Regular);
//
//    WALLET_CHECK(newReceiverCoins[1].m_amount == 6);
//    WALLET_CHECK(newReceiverCoins[1].m_status == Coin::Unspent);
//    WALLET_CHECK(newReceiverCoins[1].m_key_type == Key::Type::Regular);
//
//
//    WALLET_CHECK(newSenderCoins[0].m_amount == 5);
//    WALLET_CHECK(newSenderCoins[0].m_status == Coin::Spent);
//    WALLET_CHECK(newSenderCoins[0].m_key_type == Key::Type::Regular);
//
//    WALLET_CHECK(newSenderCoins[1].m_amount == 2);
//    WALLET_CHECK(newSenderCoins[1].m_status == Coin::Unspent);
//    WALLET_CHECK(newSenderCoins[1].m_key_type == Key::Type::Regular);
//
//    WALLET_CHECK(newSenderCoins[2].m_amount == 1);
//    WALLET_CHECK(newSenderCoins[2].m_status == Coin::Spent);
//    WALLET_CHECK(newSenderCoins[2].m_key_type == Key::Type::Regular);
//
//    WALLET_CHECK(newSenderCoins[3].m_amount == 9);
//    WALLET_CHECK(newSenderCoins[3].m_status == Coin::Spent);
//    WALLET_CHECK(newSenderCoins[3].m_key_type == Key::Type::Regular);
//
//    WALLET_CHECK(newSenderCoins[4].m_amount == 3);
//    WALLET_CHECK(newSenderCoins[4].m_status == Coin::Unspent);
//    WALLET_CHECK(newSenderCoins[4].m_key_type == Key::Type::Regular);
//
//    // Tx history check
//    sh = sender.m_Keychain->getTxHistory();
//    WALLET_CHECK(sh.size() == 2);
//    rh = receiver.m_Keychain->getTxHistory();
//    WALLET_CHECK(rh.size() == 2);
//    stx = sender.m_Keychain->getTx(txId);
//    WALLET_CHECK(stx.is_initialized());
//    rtx = receiver.m_Keychain->getTx(txId);
//    WALLET_CHECK(rtx.is_initialized());
//
//    WALLET_CHECK(stx->m_txId == rtx->m_txId);
//    WALLET_CHECK(stx->m_amount == rtx->m_amount);
//    WALLET_CHECK(stx->m_status == TxStatus::Completed);
//    WALLET_CHECK(stx->m_message == rtx->m_message);
//    WALLET_CHECK(stx->m_createTime >= rtx->m_createTime);
//    WALLET_CHECK(stx->m_status == rtx->m_status);
//    WALLET_CHECK(stx->m_fsmState.empty());
//    WALLET_CHECK(rtx->m_fsmState.empty());
//    WALLET_CHECK(stx->m_sender == true);
//    WALLET_CHECK(rtx->m_sender == false);
//
//
//    // third transfer. no enough money should appear
//    sw.start();
//
//    txId = receiver.m_Wallet.transfer_money(receiver.m_WalletID, sender.m_WalletID, 6, 0, false);
//    mainReactor->run();
//    sw.stop();
//    cout << "Third transfer elapsed time: " << sw.milliseconds() << " ms\n";
//    // check coins
//    newSenderCoins = sender.GetCoins();
//    newReceiverCoins = receiver.GetCoins();
//
//    // no coins 
//    WALLET_CHECK(newSenderCoins.size() == 5);
//    WALLET_CHECK(newReceiverCoins.size() == 2);
//
//    // Tx history check. New failed tx should be added to sender and receiver
//    sh = sender.m_Keychain->getTxHistory();
//    WALLET_CHECK(sh.size() == 3);
//    rh = receiver.m_Keychain->getTxHistory();
//    WALLET_CHECK(rh.size() == 3);
//    stx = sender.m_Keychain->getTx(txId);
//    WALLET_CHECK(stx.is_initialized());
//    rtx = receiver.m_Keychain->getTx(txId);
//    WALLET_CHECK(rtx.is_initialized());
//
//    WALLET_CHECK(rtx->m_amount == 6);
//    WALLET_CHECK(rtx->m_status == TxStatus::Failed);
//    WALLET_CHECK(rtx->m_fsmState.empty());
//    WALLET_CHECK(rtx->m_sender == false);
//
//
//    WALLET_CHECK(stx->m_amount == 6);
//    WALLET_CHECK(stx->m_status == TxStatus::Failed);
//    WALLET_CHECK(stx->m_fsmState.empty());
//    WALLET_CHECK(stx->m_sender == true);
//}
//
//void TestSwapTransaction()
//{
//    cout << "\nTesting atomic swap transaction...\n";
//
//    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
//    io::Reactor::Scope scope(*mainReactor);
//
//    int completedCount = 2;
//    auto f = [&completedCount, mainReactor](auto)
//    {
//        --completedCount;
//        if (completedCount == 0)
//        {
//            mainReactor->stop();
//            completedCount = 2;
//        }
//    };
//    TestWalletRig sender("sender", createSenderKeychain(), mainReactor, f);
//    TestWalletRig receiver("receiver", createReceiverKeychain(), mainReactor, f);
//    TestNode node{ sender.m_NodeAddress };
//
//    /*TxID txID =*/ sender.m_Wallet.swap_coins(sender.m_WalletID, receiver.m_WalletID, 4, 1, wallet::AtomicSwapCoin::Bitcoin, 2);
//
//    auto receiverCoins = receiver.GetCoins();
//    WALLET_CHECK(receiverCoins.empty());
//
//    mainReactor->run();
//
//    receiverCoins = receiver.GetCoins();
//    WALLET_CHECK(receiverCoins.size() == 1);
//    WALLET_CHECK(receiverCoins[0].m_amount == 4);
//}

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

struct MyMmr : public Merkle::Mmr
{
    typedef std::vector<Merkle::Hash> HashVector;
    typedef std::unique_ptr<HashVector> HashVectorPtr;

    std::vector<HashVectorPtr> m_vec;

    Merkle::Hash& get_At(const Merkle::Position& pos)
    {
        if (m_vec.size() <= pos.H)
            m_vec.resize(pos.H + 1);

        HashVectorPtr& ptr = m_vec[pos.H];
        if (!ptr)
            ptr.reset(new HashVector);


        HashVector& vec = *ptr;
        if (vec.size() <= pos.X)
            vec.resize(pos.X + 1);

        return vec[pos.X];
    }

    virtual void LoadElement(Merkle::Hash& hv, const Merkle::Position& pos) const override
    {
        hv = ((MyMmr*)this)->get_At(pos);
    }

    virtual void SaveElement(const Merkle::Hash& hv, const Merkle::Position& pos) override
    {
        get_At(pos) = hv;
    }
};

//struct RollbackIO : public TestNetwork
//{
//    RollbackIO(IOLoop& mainLoop, MiniBlockChain& mcm, Height branch, unsigned step)
//        : TestNetwork(mainLoop)
//        , m_mcm(mcm)
//        , m_branch(branch)
//        , m_step(step)
//    {
//
//
//    }
//
//    void InitHdr(proto::FlyClient& fc) override
//    {
//		size_t i = 0;
//		for ( ; i < m_mcm.m_vStates.size(); i++)
//		{
//			const Block::SystemState::Full& s = m_mcm.m_vStates[i].m_Hdr;
//			if (s.m_Height >= m_branch)
//				break;
//
//			fc.m_Hist[s.m_Height] = s;
//		}
//
//		fc.OnRolledBack();
//
//		for (; i < m_mcm.m_vStates.size(); i++)
//		{
//			const Block::SystemState::Full& s = m_mcm.m_vStates[i].m_Hdr;
//			fc.m_Hist[s.m_Height] = s;
//		}
//
//		fc.OnNewTip();
//    }
//
//    void send_node_message(proto::GetMined&& data) override
//    {
//		WALLET_CHECK(data.m_HeightMin < m_branch);
//		WALLET_CHECK(data.m_HeightMin >= m_step * Height((m_branch - 1) / m_step));
//
//        TestNetwork::send_node_message(move(data));
//    }
//
//    void close_node_connection() override
//    {
//        shutdown();
//    }
//
//    MiniBlockChain& m_mcm;
//    Height m_branch;
//    unsigned m_step;
//};
//
//void TestRollback(Height branch, Height current, unsigned step = 1)
//{
//    cout << "\nRollback from " << current << " to " << branch << " step: " << step <<'\n';
//    auto db = createSqliteKeychain("wallet.db");
//    
//    MiniBlockChain mcmOld, mcmNew;
//
//    for (Height i = Rules::HeightGenesis; i <= current; ++i)
//    {
//        mcmOld.Add();
//
//        if (i == branch)
//            mcmNew.m_hvLive = 1U; // branching
//        mcmNew.Add();
//
//        if (i % step == 0)
//        {
//            Coin coin1 = { 5, Coin::Unspent, 0, 0, Key::Type::Regular, i };
//            mcmOld.m_vStates.back().m_Hdr.get_Hash(coin1.m_confirmHash);
//
//            db->store(coin1);
//        }
//    }
//
//    Block::SystemState::ID id;
//    mcmOld.m_vStates.back().m_Hdr.get_ID(id);
//    db->setSystemStateID(id);
//
//    IOLoop mainLoop;
//    auto network = make_shared<RollbackIO>(mainLoop, mcmNew, branch, step);
//
//    Wallet sender(db, network);
//    
//    network->registerPeer(&sender, true);
//    
//    mainLoop.run();
//}
//
//void TestRollback()
//{
//    cout << "\nTesting wallet rollback...\n";
//    Height s = 10;
//    for (Height i = 1; i <= s; ++i)
//    {
//        TestRollback(i, s);
//        TestRollback(i, s, 2);
//    }
//    s = 11;
//    for (Height i = 1; i <= s; ++i)
//    {
//        TestRollback(i, s);
//        TestRollback(i, s, 2);
//    }
//    
//    TestRollback(1, 1);
//    TestRollback(2, 50);
//    TestRollback(2, 51);
//    TestRollback(93, 120);
//    TestRollback(93, 120, 6);
//    TestRollback(93, 120, 7);
//    TestRollback(99, 100);
//}

int main()
{
    int logLevel = LOG_LEVEL_DEBUG;
#if LOG_VERBOSE_ENABLED
    logLevel = LOG_LEVEL_VERBOSE;
#endif
    auto logger = beam::Logger::create(logLevel, logLevel);

	Rules::get().FakePoW = true;
	Rules::get().UpdateChecksum();

    TestSplitKey();
    TestP2PWalletNegotiationST();
    //TestP2PWalletReverseNegotiationST();

    TestWalletNegotiation(CreateKeychain<TestKeyChain>(), CreateKeychain<TestKeyChain2>());
    TestWalletNegotiation(createSenderKeychain(), createReceiverKeychain());

    //TestSwapTransaction();

    //TestTxToHimself();

    //TestRollback();

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}
