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

#include "nlohmann/json.hpp"

using namespace beam;
using namespace std;
using namespace ECC;
using json = nlohmann::json;

Coin CreateAvailCoin(Amount amount, Height maturity = 10)
{
    Coin c(amount);
    c.m_maturity = maturity;
    c.m_confirmHeight = maturity;
    return c;
}

class BaseTestWalletDB : public IWalletDB
{
    Key::IKdf::Ptr m_pKdf;
    Block::SystemState::HistoryMap m_Hist;
    uint64_t m_KeyIndex = 1;
public:

    BaseTestWalletDB()
    {
        uintBig seed;
        seed = 10U;
        HKdf::Create(m_pKdf, seed);
    }


    Key::IKdf::Ptr get_MasterKdf() const override
    {
        return m_pKdf;
    }

    std::vector<beam::Coin> selectCoins(ECC::Amount amount) override
    {
        std::vector<beam::Coin> res;
        ECC::Amount t = 0;
        for (auto& c : m_coins)
        {
            t += c.m_ID.m_Value;
            c.m_status = Coin::Outgoing;
            res.push_back(c);
            if (t >= amount)
            {
                break;
            }
        }
        return res;
    }

    uint64_t AllocateKidRange(uint64_t nCount) override
    {
        uint64_t ret = m_KeyIndex;
        m_KeyIndex += nCount;
        return ret;
    }
    bool find(Coin& coin) override { return false; }
    std::vector<beam::Coin> getCoinsCreatedByTx(const TxID& txId) override { return {}; };
    std::vector<Coin> getCoinsByID(const CoinIDList& ids) override { return {}; };
    void store(beam::Coin&) override {}
    void store(std::vector<beam::Coin>&) override {}
    void save(const beam::Coin&) override {}
    void save(const std::vector<beam::Coin>&) override {}
    void remove(const std::vector<beam::Coin::ID>&) override {}
    void remove(const beam::Coin::ID&) override {}
    void visit(std::function<bool(const beam::Coin& coin)>) override {}
    void setVarRaw(const char*, const void*, size_t) override {}
    bool getVarRaw(const char*, void*, int) const override { return false; }
    bool getBlob(const char* name, ByteBuffer& var) const override { return false; }
    Timestamp getLastUpdateTime() const override { return 0; }
    void setSystemStateID(const Block::SystemState::ID&) override {};
    bool getSystemStateID(Block::SystemState::ID&) const override { return false; };

    void subscribe(IWalletDbObserver* observer) override {}
    void unsubscribe(IWalletDbObserver* observer) override {}

    std::vector<TxDescription> getTxHistory(uint64_t, int) override { return {}; };
    boost::optional<TxDescription> getTx(const TxID&) override { return boost::optional<TxDescription>{}; };
    void saveTx(const TxDescription& p) override
    {
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::Amount, wallet::toByteBuffer(p.m_amount), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::Fee, wallet::toByteBuffer(p.m_fee), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::Change, wallet::toByteBuffer(p.m_change), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::MinHeight, wallet::toByteBuffer(p.m_minHeight), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::PeerID, wallet::toByteBuffer(p.m_peerId), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::MyID, wallet::toByteBuffer(p.m_myId), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::Message, wallet::toByteBuffer(p.m_message), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::CreateTime, wallet::toByteBuffer(p.m_createTime), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::ModifyTime, wallet::toByteBuffer(p.m_modifyTime), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::IsSender, wallet::toByteBuffer(p.m_sender), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::Status, wallet::toByteBuffer(p.m_status), false);
    };
    void deleteTx(const TxID&) override {};
    void rollbackTx(const TxID&) override {}

    std::vector<WalletAddress> getAddresses(bool own) const override { return {}; }

    WalletAddress m_LastAdddr;

    void saveAddress(const WalletAddress& wa) override
    {
        m_LastAdddr = wa;
    }

    void setNeverExpirationForAll() override {};
    boost::optional<WalletAddress> getAddress(const WalletID& id) override
    {
        if (id == m_LastAdddr.m_walletID)
            return m_LastAdddr;

        return boost::optional<WalletAddress>();
    }
    void deleteAddress(const WalletID&) override {}

    Height getCurrentHeight() const override
    {
        return 134;
    }

    void rollbackConfirmedUtxo(Height /*minHeight*/) override
    {}

    void clear() override {}

    void changePassword(const SecString& password) override {}

    bool setTxParameter(const TxID& txID, wallet::SubTxID subTxID, wallet::TxParameterID paramID,
        const ByteBuffer& blob, bool shouldNotifyAboutChanges) override
    {
        if (paramID < wallet::TxParameterID::PrivateFirstParam)
        {
            auto p = m_params.emplace(paramID, blob);
            return p.second;
        }
        m_params[paramID] = blob;
        return true;
    }
    bool getTxParameter(const TxID& txID, wallet::SubTxID subTxID, wallet::TxParameterID paramID, ByteBuffer& blob) const override
    {
        auto it = m_params.find(paramID);
        if (it != m_params.end())
        {
            blob = it->second;
            return true;
        }
        return false;
    }

    Block::SystemState::IHistory& get_History() override { return m_Hist; }
    void ShrinkHistory() override {}

protected:
    std::vector<beam::Coin> m_coins;
    std::map<wallet::TxParameterID, ByteBuffer> m_params;
};

class TestWalletDB : public BaseTestWalletDB
{
public:
    TestWalletDB()
    {
        m_coins.emplace_back(5);
        m_coins.emplace_back(2);
        m_coins.emplace_back(3);
    }
};

class TestWalletDB2 : public BaseTestWalletDB
{
public:
    TestWalletDB2()
    {
        m_coins.emplace_back(1);
        m_coins.emplace_back(3);
    }
};

template<typename T>
IWalletDB::Ptr CreateWalletDB()
{
    return std::static_pointer_cast<IWalletDB>(std::make_shared<T>());
}

IWalletDB::Ptr createSqliteWalletDB(const string& path)
{
    ECC::NoLeak<ECC::uintBig> seed;
    seed.V = Zero;
    if (boost::filesystem::exists(path))
    {
        boost::filesystem::remove(path);
    }

    auto walletDB = WalletDB::init(path, string("pass123"), seed);
    //beam::Block::SystemState::ID id = {};
    //id.m_Height = 134;
    //walletDB->setSystemStateID(id);
    return walletDB;
}

IWalletDB::Ptr createSenderWalletDB()
{
    auto db = createSqliteWalletDB("sender_wallet.db");
    db->AllocateKidRange(100500); // make sure it'll get the address different from the receiver
    for (auto amount : { 5, 2, 1, 9 })
    {
        Coin coin = CreateAvailCoin(amount, 0);
        db->store(coin);
    }
    return db;
}

IWalletDB::Ptr createSenderWalletDB(int count, Amount amount)
{
    auto db = createSqliteWalletDB("sender_wallet.db");
    db->AllocateKidRange(100500); // make sure it'll get the address different from the receiver
    for (int i = 0; i < count; ++i)
    {
        Coin coin = CreateAvailCoin(amount, 0);
        db->store(coin);
    }
    return db;
}

IWalletDB::Ptr createReceiverWalletDB()
{
    return createSqliteWalletDB("receiver_wallet.db");
}

struct TestGateway : wallet::INegotiatorGateway
{
    void on_tx_completed(const TxID&) override
    {
        cout << __FUNCTION__ << "\n";
    }

    void register_tx(const TxID&, Transaction::Ptr) override
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

    void confirm_kernel(const TxID&, const Merkle::Hash&) override
    {
        cout << "confirm kernel\n";
    }

    bool get_tip(Block::SystemState::Full& state) const override
    {
        return true;
    }

    BitcoinRPC::Ptr get_bitcoin_rpc() const override
    {
        return nullptr;
    }
};

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
    TestWalletRig(const string& name, IWalletDB::Ptr walletDB, Wallet::TxCompletedAction&& action = Wallet::TxCompletedAction())
        : m_WalletDB{ walletDB }
        , m_Wallet{ m_WalletDB, move(action) }
        , m_NodeNetwork(m_Wallet)
        , m_WalletNetworkViaBbs(m_Wallet, m_NodeNetwork, m_WalletDB)
    {
        WalletAddress wa = wallet::createAddress(*m_WalletDB);
        m_WalletDB->saveAddress(wa);
        m_WalletID = wa.m_walletID;

        m_Wallet.set_Network(m_NodeNetwork, m_WalletNetworkViaBbs);

        m_NodeNetwork.m_Cfg.m_vNodes.push_back(io::Address::localhost().port(32125));
        m_NodeNetwork.Connect();

        m_WalletNetworkViaBbs.AddOwnAddress(wa);
    }

    vector<Coin> GetCoins()
    {
        vector<Coin> coins;
        m_WalletDB->visit([&coins](const Coin& c)->bool
        {
            coins.push_back(c);
            return true;
        });
        return coins;
    }

    WalletID m_WalletID;
    IWalletDB::Ptr m_WalletDB;
    int m_CompletedCount{ 1 };
    Wallet m_Wallet;
    proto::FlyClient::NetworkStd m_NodeNetwork;
    WalletNetworkViaBbs m_WalletNetworkViaBbs;
};

struct TestWalletNetwork
    : public IWalletNetwork
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
                v.m_pSink->OnWalletMessage(v.m_Msgs.front().first, std::move(v.m_Msgs.front().second));
    }
};

struct TestBlockchain
{
    MiniBlockChain m_mcm;

    UtxoTree m_Utxos;

    struct KrnPerBlock
    {
        std::vector<Merkle::Hash> m_vKrnIDs;
        std::vector<shared_ptr<TxKernel>> m_Kernels;

        struct Mmr :public Merkle::FlyMmr
        {
            const Merkle::Hash* m_pHashes;

            Mmr(const KrnPerBlock& kpb)
                :Merkle::FlyMmr(kpb.m_vKrnIDs.size())
            {
                m_pHashes = kpb.m_vKrnIDs.empty() ? NULL : &kpb.m_vKrnIDs.front();
            }

            virtual void LoadElement(Merkle::Hash& hv, uint64_t n) const override {
                hv = m_pHashes[n];
            }
        };

    };
    std::vector<KrnPerBlock> m_vBlockKernels;

    void AddBlock()
    {
        m_Utxos.get_Hash(m_mcm.m_hvLive);
        m_mcm.Add();

        if (m_vBlockKernels.size() < m_mcm.m_vStates.size())
            m_vBlockKernels.emplace_back();
        assert(m_vBlockKernels.size() == m_mcm.m_vStates.size());

        KrnPerBlock::Mmr fmmr(m_vBlockKernels.back());
        fmmr.get_Hash(m_mcm.m_vStates.back().m_Hdr.m_Kernels);
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

        cu.InvalidateElement();

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
            cu.InvalidateElement();

        return true;
    }


    void GetProof(const proto::GetProofUtxo& data, proto::ProofUtxo& msgOut)
    {
        struct Traveler :public UtxoTree::ITraveler
        {
            proto::ProofUtxo m_Msg;
            UtxoTree* m_pTree;
            Merkle::Hash m_hvHistory;

            virtual bool OnLeaf(const RadixTree::Leaf& x) override {

                const UtxoTree::MyLeaf& v = (UtxoTree::MyLeaf&) x;
                UtxoTree::Key::Data d;
                d = v.m_Key;

                m_Msg.m_Proofs.resize(m_Msg.m_Proofs.size() + 1);
                Input::Proof& ret = m_Msg.m_Proofs.back();

                ret.m_State.m_Count = v.m_Value.m_Count;
                ret.m_State.m_Maturity = d.m_Maturity;
                m_pTree->get_Proof(ret.m_Proof, *m_pCu);

                ret.m_Proof.emplace_back();
                ret.m_Proof.back().first = false;
                ret.m_Proof.back().second = m_hvHistory;

                return m_Msg.m_Proofs.size() < Input::Proof::s_EntriesMax;
            }
        } t;

        t.m_pTree = &m_Utxos;
        m_mcm.m_Mmr.get_Hash(t.m_hvHistory);

        UtxoTree::Cursor cu;
        t.m_pCu = &cu;

        // bounds
        UtxoTree::Key kMin, kMax;

        UtxoTree::Key::Data d;
        d.m_Commitment = data.m_Utxo;
        d.m_Maturity = data.m_MaturityMin;
        kMin = d;
        d.m_Maturity = Height(-1);
        kMax = d;

        t.m_pBound[0] = kMin.m_pArr;
        t.m_pBound[1] = kMax.m_pArr;

        t.m_pTree->Traverse(t);
        t.m_Msg.m_Proofs.swap(msgOut.m_Proofs);
    }

    void GetProof(const proto::GetProofKernel& data, proto::ProofKernel& msgOut)
    {
        for (size_t iState = m_mcm.m_vStates.size(); iState--; )
        {
            const KrnPerBlock& kpb = m_vBlockKernels[iState];

            for (size_t i = 0; i < kpb.m_vKrnIDs.size(); i++)
            {
                if (kpb.m_vKrnIDs[i] == data.m_ID)
                {
                    KrnPerBlock::Mmr fmmr(kpb);
                    Merkle::ProofBuilderStd bld;
                    fmmr.get_Proof(bld, i);

                    msgOut.m_Proof.m_Inner.swap(bld.m_Proof);
                    msgOut.m_Proof.m_State = m_mcm.m_vStates[iState].m_Hdr;

                    if (iState + 1 != m_mcm.m_vStates.size())
                    {
                        Merkle::ProofBuilderHard bld2;
                        m_mcm.m_Mmr.get_Proof(bld2, iState);
                        msgOut.m_Proof.m_Outer.swap(bld2.m_Proof);
                    }

                    return;
                }
            }
        }
    }

    void GetProof(const proto::GetProofKernel2& data, proto::ProofKernel2& msgOut)
    {
        for (size_t iState = m_mcm.m_vStates.size(); iState--; )
        {
            const KrnPerBlock& kpb = m_vBlockKernels[iState];

            for (size_t i = 0; i < kpb.m_vKrnIDs.size(); i++)
            {
                if (kpb.m_vKrnIDs[i] == data.m_ID)
                {
                    KrnPerBlock::Mmr fmmr(kpb);
                    Merkle::ProofBuilderStd bld;
                    fmmr.get_Proof(bld, i);

                    msgOut.m_Proof.swap(bld.m_Proof);
                    msgOut.m_Height = iState;

                    if (data.m_Fetch)
                    {
                        msgOut.m_Kernel.reset(new TxKernel);
                        *msgOut.m_Kernel = *kpb.m_Kernels[i];
                    }
                    return;
                }
            }
        }
    }

    void AddKernel(const TxKernel& krn)
    {
        Merkle::Hash hvKrn;
        krn.get_Hash(hvKrn);

        if (m_vBlockKernels.size() <= m_mcm.m_vStates.size())
            m_vBlockKernels.emplace_back();

        KrnPerBlock& kpb = m_vBlockKernels.back();
        kpb.m_vKrnIDs.push_back(hvKrn);

        auto ptr = make_shared<TxKernel>();
        *ptr = krn;
        kpb.m_Kernels.push_back(ptr);
    }

    void HandleTx(const proto::NewTransaction& data)
    {
        for (const auto& input : data.m_Transaction->m_vInputs)
            RemoveCommitment(input->m_Commitment);
        for (const auto& output : data.m_Transaction->m_vOutputs)
            AddCommitment(output->m_Commitment);
        for (size_t i = 0; i < data.m_Transaction->m_vKernels.size(); i++)
            AddKernel(*data.m_Transaction->m_vKernels[i]);
    }
};

struct TestNodeNetwork
    :public proto::FlyClient::INetwork
    , public AsyncProcessor
    , public boost::intrusive::list_base_hook<>
{
    typedef boost::intrusive::list<TestNodeNetwork> List;
    typedef proto::FlyClient::Request Request;

    proto::FlyClient& m_Client;

    struct Shared
    {
        TestBlockchain m_Blockchain;
        List m_lst;

        void AddBlock()
        {
            m_Blockchain.AddBlock();

            for (List::iterator it = m_lst.begin(); m_lst.end() != it; it++)
            {
                proto::FlyClient& c = it->m_Client;
                c.get_History().AddStates(&m_Blockchain.m_mcm.m_vStates.back().m_Hdr, 1);
                c.OnNewTip();
            }
        }
    };

    Shared& m_Shared;

    TestNodeNetwork(Shared& shared, proto::FlyClient& x)
        :m_Client(x)
        , m_Shared(shared)
    {
        m_Shared.m_lst.push_back(*this);
    }

    ~TestNodeNetwork()
    {
        m_Shared.m_lst.erase(List::s_iterator_to(*this));
    }

    typedef std::deque<Request::Ptr> Queue;
    Queue m_queReqs;

    virtual void Connect() override {}
    virtual void Disconnect() override {}

    virtual void PostRequestInternal(Request& r) override
    {
        assert(r.m_pTrg);

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
            if (r.m_pTrg)
                r.m_pTrg->OnComplete(r);
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

            m_Shared.m_Blockchain.HandleTx(v.m_Msg);
            m_Shared.AddBlock();
        }
        break;

        case Request::Type::Kernel:
        {
            proto::FlyClient::RequestKernel& v = static_cast<proto::FlyClient::RequestKernel&>(r);
            m_Shared.m_Blockchain.GetProof(v.m_Msg, v.m_Res);
        }
        break;

        case Request::Type::Kernel2:
        {
            proto::FlyClient::RequestKernel2& v = static_cast<proto::FlyClient::RequestKernel2&>(r);
            m_Shared.m_Blockchain.GetProof(v.m_Msg, v.m_Res);
        }
        break;

        case Request::Type::Utxo:
        {
            proto::FlyClient::RequestUtxo& v = static_cast<proto::FlyClient::RequestUtxo&>(r);
            m_Shared.m_Blockchain.GetProof(v.m_Msg, v.m_Res);
        }
        break;

        default:
            break; // suppess warning
        }
    }
};

class TestNode
{
public:
    TestNode()
    {
        m_Server.Listen(io::Address::localhost().port(32125));
        while (m_Blockchain.m_mcm.m_vStates.size() < 145)
            m_Blockchain.AddBlock();
    }

    ~TestNode() {
        KillAll();
    }

    void KillAll()
    {
        while (!m_lstClients.empty())
            DeleteClient(&m_lstClients.front());
    }

    TestBlockchain m_Blockchain;

    void AddBlock()
    {
        m_Blockchain.AddBlock();

        for (ClientList::iterator it = m_lstClients.begin(); m_lstClients.end() != it; it++)
        {
            Client& c = *it;
            if (c.IsSecureOut())
                c.SendTip();
        }
    }
private:

    struct Client
        :public proto::NodeConnection
        , public boost::intrusive::list_base_hook<>
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

            proto::Login msg;
            msg.m_CfgChecksum = Rules::get().Checksum;
            msg.m_Flags =
                proto::LoginFlags::Extension1 |
                proto::LoginFlags::SpreadingTransactions |
                proto::LoginFlags::Bbs |
                proto::LoginFlags::SendPeers;
            Send(msg);

            SendTip();
        }

        void SendTip()
        {
            proto::NewTip msg;
            msg.m_Description = m_This.m_Blockchain.m_mcm.m_vStates.back().m_Hdr;
            Send(msg);
        }

        void OnMsg(proto::NewTransaction&& data) override
        {
            m_This.m_Blockchain.HandleTx(data);

            Send(proto::Boolean{ true });
            m_This.AddBlock();
        }

        void OnMsg(proto::GetProofUtxo&& data) override
        {
            proto::ProofUtxo msgOut;
            m_This.m_Blockchain.GetProof(data, msgOut);
            Send(msgOut);
        }

        void OnMsg(proto::GetProofKernel&& data) override
        {
            proto::ProofKernel msgOut;
            m_This.m_Blockchain.GetProof(data, msgOut);
            Send(msgOut);
        }

        void OnMsg(proto::GetProofKernel2&& data) override
        {
            proto::ProofKernel2 msgOut;
            m_This.m_Blockchain.GetProof(data, msgOut);
            Send(msgOut);
        }

        void OnMsg(proto::Login&& /*data*/) override
        {
        }

        void OnMsg(proto::GetProofState&&) override
        {
            Send(proto::ProofState{});
        }

        void OnMsg(proto::GetProofChainWork&& msg) override
        {
            proto::ProofChainWork msgOut;
            msgOut.m_Proof.m_LowerBound = msg.m_LowerBound;
            msgOut.m_Proof.m_hvRootLive = m_This.m_Blockchain.m_mcm.m_hvLive;
            msgOut.m_Proof.Create(m_This.m_Blockchain.m_mcm.m_Source, m_This.m_Blockchain.m_mcm.m_vStates.back().m_Hdr);

            Send(msgOut);
        }

        void OnMsg(proto::BbsSubscribe&& msg) override
        {
            if (m_Subscribed)
                return;
            m_Subscribed = true;

            for (const auto& m : m_This.m_bbs)
                Send(m);
        }

        void OnMsg(proto::BbsMsg&& msg) override
        {
            m_This.m_bbs.push_back(msg);

            for (ClientList::iterator it = m_This.m_lstClients.begin(); m_This.m_lstClients.end() != it; it++)
            {
                Client& c = *it;
                if ((&c != this) && c.m_Subscribed)
                    c.Send(msg);
            }
        }

        void OnMsg(proto::Ping&& msg) override
        {
            proto::Pong msgOut(Zero);
            Send(msgOut);
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

class TestBitcoinWallet
{
public:

    struct Options
    {
        string m_rawAddress = "2NB9nqKnHgThByiSzVEVDg5cYC2HwEMBcEK";
        string m_privateKey = "cTZEjMtL96FyC43AxEvUxbs3pinad2cH8wvLeeCYNUwPURqeknkG";
        string m_rawLockTx = "";
        string m_signLockTx = "";
        string m_lockTx = "";
        string m_refundTx = "";
        string m_lockTxId = "";
    };

public:

    TestBitcoinWallet(io::Reactor& reactor, const io::Address& addr, const Options& options)
        : m_reactor(reactor)
        , m_httpClient(reactor)
        , m_msgCreator(1000)
        , m_lastId(0)
        , m_options(options)
    {
        m_server = io::TcpServer::create(
            m_reactor,
            addr,
            BIND_THIS_MEMFN(onStreamAccepted)
        );
    }

    void addPeer(const io::Address& addr)
    {
        m_peers.push_back(addr);
    }

private:

    void onStreamAccepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode)
    {
        if (errorCode == 0)
        {
            LOG_DEBUG() << "Stream accepted";
            uint64_t peerId = m_lastId++;
            m_connections[peerId] = std::make_unique<HttpConnection>(
                peerId,
                BaseConnection::inbound,
                BIND_THIS_MEMFN(onRequest),
                10000,
                1024,
                std::move(newStream)
                );
        }
        else
        {
            LOG_ERROR() << "Server error " << io::error_str(errorCode);
            //g_stopEvent();
        }
    }

    bool onRequest(uint64_t peerId, const HttpMsgReader::Message& msg)
    {
        const char* message = "OK";
        static const HeaderPair headers[] =
        {
            {"Server", "BitcoinHttpServer"}
        };
        io::SharedBuffer body = generateResponse(msg);
        io::SerializedMsg serialized;

        if (m_connections[peerId] && m_msgCreator.create_response(
            serialized, 200, message, headers, sizeof(headers) / sizeof(HeaderPair),
            1, "text/plain", body.size))
        {
            serialized.push_back(body);
            m_connections[peerId]->write_msg(serialized);
            m_connections[peerId]->shutdown();
        }
        else
        {
            LOG_ERROR() << "Cannot create response";
            //g_stopEvent();
        }

        m_connections.erase(peerId);

        return false;
    }

    io::SharedBuffer generateResponse(const HttpMsgReader::Message& msg)
    {
        size_t sz = 0;
        const void* rawReq = msg.msg->get_body(sz);
        std::string result;
        if (sz > 0 && rawReq)
        {
            std::string req(static_cast<const char*>(rawReq), sz);
            json j = json::parse(req);
            if (j["method"] == "fundrawtransaction")
            {
                result = R"({"result":{"hex":")" + m_options.m_lockTx + R"(", "fee": 0, "changepos": 0},"error":null,"id":null})";
            }
            else if (j["method"] == "dumpprivkey")
            {
                result = R"({"result":")" + m_options.m_privateKey + R"(","error":null,"id":null})";
            }
            else if (j["method"] == "signrawtransactionwithwallet")
            {
                result = R"({"result": {"hex": ")" + m_options.m_signLockTx + R"(", "complete": true},"error":null,"id":null})";
            }
            else if (j["method"] == "decoderawtransaction")
            {
                result = R"({"result": {"txid": ")" + m_options.m_lockTxId + R"("},"error":null,"id":null})";
            }
            else if (j["method"] == "createrawtransaction")
            {
                result = R"({"result": ")" + m_options.m_refundTx + R"(","error":null,"id":null})";
            }
            else if (j["method"] == "getrawchangeaddress")
            {
                result = R"( {"result":")" + m_options.m_rawAddress + R"(","error":null,"id":null})";
            }
            else if (j["method"] == "sendrawtransaction")
            {
                std::string tx = j["params"][0];
                if (std::find(m_rawTransactions.begin(), m_rawTransactions.end(), tx) == m_rawTransactions.end())
                {
                    m_rawTransactions.push_back(tx);
                    sendRawTransaction(req);
                }
                result = R"( {"result":")" + m_options.m_lockTxId + R"(","error":null,"id":null})";
            }
            else if (j["method"] == "gettxout")
            {
                std::string txid = j["params"][0];
                if (m_txConfirmations.find(txid) == m_txConfirmations.end())
                {
                    m_txConfirmations[txid] = 0; // for test
                }

                ++m_txConfirmations[txid];

                result = R"( {"result":{"confirmations":)" + std::to_string(m_txConfirmations[txid]) + R"(},"error":null,"id":null})";
            }
        }
        else
        {
            LOG_ERROR() << "Request is wrong";
            //g_stopEvent();
        }

        io::SharedBuffer body;

        body.assign(result.data(), result.size());
        return body;
    }

    void sendRawTransaction(const string& msg)
    {
        for (const auto& peer : m_peers)
        {
            HttpClient::Request request;

            request.address(peer)
                .connectTimeoutMsec(2000)
                .pathAndQuery("/")
                //.headers(headers)
                //.numHeaders(1)
                .method("POST")
                .body(msg.c_str(), msg.size());

            request.callback([](uint64_t, const HttpMsgReader::Message&) -> bool {
                return false;
            });

            m_httpClient.send_request(request);
        }
    }

private:
    io::Reactor& m_reactor;
    io::TcpServer::Ptr m_server;
    HttpClient m_httpClient;
    std::map<uint64_t, HttpConnection::Ptr> m_connections;
    HttpMsgCreator m_msgCreator;
    uint64_t m_lastId;
    Options m_options;
    std::vector<io::Address> m_peers;
    std::vector<std::string> m_rawTransactions;
    std::map<std::string, int> m_txConfirmations;
};
