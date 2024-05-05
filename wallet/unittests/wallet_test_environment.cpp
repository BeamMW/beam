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

#include <tuple>
#include <boost/filesystem.hpp>

#include "http/http_client.h"
#include "core/treasury.h"
#include "keykeeper/local_private_key_keeper.h"
#include "wallet/core/simple_transaction.h"
#include "utility/test_helpers.h"
#include "core/unittest/mini_blockchain.h"

using namespace beam;
using namespace beam::wallet;
using namespace std;
using namespace ECC;
using json = nlohmann::json;

struct EmptyTestGateway : wallet::INegotiatorGateway
{
    void OnAsyncStarted() override {}
    void OnAsyncFinished() override {}
    void on_tx_completed(const TxID&) override {}
    void on_tx_failed(const TxID&) override {}
    void register_tx(const TxID&, const Transaction::Ptr&, const Merkle::Hash*, wallet::SubTxID) override {}
    void confirm_kernel(const TxID&, const Merkle::Hash&, wallet::SubTxID subTxID) override {}
    void confirm_kernel_ex(const Merkle::Hash& kernelID, IConfirmCallback::Ptr&& pCallback) override {}
    void confirm_asset(const TxID&, const PeerID&, SubTxID subTxID) override {}
    void confirm_asset(const TxID&, const Asset::ID, SubTxID subTxID) override {}
    void get_kernel(const TxID& txID, const Merkle::Hash& kernelID, wallet::SubTxID subTxID) override {}
    bool get_tip(Block::SystemState::Full& state) const override { return false; }
    void send_tx_params(const WalletID& peerID, const wallet::SetTxParameter&) override {}
    void get_shielded_list(const TxID&, TxoID startIndex, uint32_t count, ShieldedListCallback&& callback) override {}
    void UpdateOnNextTip(const TxID&) override {}
};

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

    // std::vector<Coin> selectCoins(ECC::Amount amount, Asset::ID assetId) override
    // {
    //     std::vector<Coin> res;
    //     ECC::Amount t = 0;
    //     for (auto& c : m_coins)
    //     {
    //         if (c.m_ID.m_AssetID != assetId) continue;
    //         t += c.m_ID.m_Value;
    //         c.m_status = Coin::Outgoing;
    //         res.push_back(c);
    //         if (t >= amount)
    //         {
    //             break;
    //         }
    //     }
    //     return res;
    // }

    uint64_t AllocateKidRange(uint64_t nCount) override
    {
        uint64_t ret = m_KeyIndex;
        m_KeyIndex += nCount;
        return ret;
    }
    bool findCoin(Coin& coin) override { return false; }
    std::vector<Coin> getCoinsCreatedByTx(const TxID& txId) const override { return {}; };
    std::vector<Coin> getCoinsByID(const CoinIDList& ids) const override { return {}; };
    void storeCoin(Coin&) override {}
    void storeCoins(std::vector<Coin>&) override {}
    void saveCoin(const Coin&) override {}
    void saveCoins(const std::vector<Coin>&) override {}
    void removeCoins(const std::vector<Coin::ID>&) override {}
    void visitCoins(std::function<bool(const Coin& coin)>) const override {}
    void setVarRaw(const char*, const void*, size_t) override {}
    bool getVarRaw(const char*, void*, int) const override { return false; }
    bool getBlob(const char* name, ByteBuffer& var) const override { return false; }
    Timestamp getLastUpdateTime() const override { return 0; }
    void setSystemStateID(const Block::SystemState::ID&) override {};
    bool getSystemStateID(Block::SystemState::ID&) const override { return false; };

    void Subscribe(IWalletDbObserver* observer) override {}
    void Unsubscribe(IWalletDbObserver* observer) override {}

    std::vector<TxDescription> getTxHistory(wallet::TxType, uint64_t, int) const override { return {}; };
    boost::optional<TxDescription> getTx(const TxID&) const override { return boost::optional<TxDescription>{}; };
    void saveTx(const TxDescription& p) override
    {
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::Amount, toByteBuffer(p.m_amount), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::Fee, toByteBuffer(p.m_fee), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::AssetID, toByteBuffer(p.m_assetId), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::AssetMetadata, toByteBuffer(p.m_assetMeta), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::MinHeight, toByteBuffer(p.m_minHeight), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::PeerAddr, toByteBuffer(p.m_peerAddr), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::MyAddr, toByteBuffer(p.m_myAddr), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::Message, toByteBuffer(p.m_message), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::CreateTime, toByteBuffer(p.m_createTime), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::ModifyTime, toByteBuffer(p.m_modifyTime), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::IsSender, toByteBuffer(p.m_sender), false);
        setTxParameter(p.m_txId, wallet::kDefaultSubTxID, wallet::TxParameterID::Status, toByteBuffer(p.m_status), false);
    };
    void deleteTx(const TxID&) override {};

    std::vector<WalletAddress> getAddresses(bool own, bool isLaser = false) const override { return {}; }

    WalletAddress m_LastAdddr;

    void saveAddress(const WalletAddress& wa, bool isLaser = false) override
    {
        m_LastAdddr = wa;
    }

    boost::optional<WalletAddress> getAddress(
        const WalletID& id, bool isLaser = false) const override
    {
        if (id == m_LastAdddr.m_BbsAddr)
            return m_LastAdddr;

        return boost::optional<WalletAddress>();
    }
    void deleteAddress(const WalletID&, bool isLaser = false) override {}

    Height getCurrentHeight() const override
    {
        return 134;
    }

    void rollbackConfirmedUtxo(Height /*minHeight*/) override
    {}

    void rollbackAssets(Height /*minHeight*/) override
    {}

    void clearCoins() override {}

    void changePassword(const SecString& password) override {}

    bool setTxParameter(const TxID& txID, wallet::SubTxID subTxID, wallet::TxParameterID paramID,
        const ByteBuffer& blob, bool shouldNotifyAboutChanges, bool allowModify = true) override
    {
        if (!allowModify)
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
    std::vector<Coin> m_coins;
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

const string SenderWalletDB = "sender_wallet.db";
const string ReceiverWalletDB = "receiver_wallet.db";
const string DBPassword = "pass123";

IWalletDB::Ptr createSqliteWalletDB(const string& path, bool separateDBForPrivateData, bool generateSeed)
{
    if (boost::filesystem::exists(path))
    {
        boost::filesystem::remove(path);
    }
    if (separateDBForPrivateData)
    {
        string privatePath = path + ".private";
        boost::filesystem::remove(privatePath);
    }

    ECC::NoLeak<ECC::uintBig> seed;
    if (generateSeed)
    {
        void* p = reinterpret_cast<void*>(&seed.V);
    	for (uint32_t i = 0; i < sizeof(seed.V); i++)
		    ((uint8_t*) p)[i] = (uint8_t) rand();
    }
    else
    {
        seed.V = Zero;
    }
               
    auto walletDB = WalletDB::init(path, DBPassword, seed, separateDBForPrivateData);
    return walletDB;
}

IWalletDB::Ptr createSenderWalletDBWithSeed(const std::string& fileName, bool generateSeed, bool separateDBForPrivateData = false, const AmountList& amounts = {5, 2, 1, 9})
{
    auto db = createSqliteWalletDB(fileName, separateDBForPrivateData, generateSeed);
    db->AllocateKidRange(100500); // make sure it'll get the address different from the receiver
    for (auto amount : amounts)
    {
        Coin coin = CreateAvailCoin(amount, 0);
        db->storeCoin(coin);
    }
    return db;
}

IWalletDB::Ptr createSenderWalletDB(bool separateDBForPrivateData = false, const AmountList& amounts = { 5, 2, 1, 9 })
{
    return createSenderWalletDBWithSeed(SenderWalletDB, false, separateDBForPrivateData, amounts);
}

IWalletDB::Ptr createSenderWalletDB(int count, Amount amount, bool separateDBForPrivateData = false, bool generateSeed = true)
{
    auto db = createSqliteWalletDB(SenderWalletDB, separateDBForPrivateData, generateSeed);
    db->AllocateKidRange(100500); // make sure it'll get the address different from the receiver
    for (int i = 0; i < count; ++i)
    {
        Coin coin = CreateAvailCoin(amount, 0);
        db->storeCoin(coin);
    }
    return db;
}

IWalletDB::Ptr createReceiverWalletDB(bool separateDBForPrivateData = false, bool generateSeed = false)
{
    return createSqliteWalletDB(ReceiverWalletDB, separateDBForPrivateData, generateSeed);
}

struct TestGateway : wallet::INegotiatorGateway
{
    void on_tx_completed(const TxID&) override
    {
        cout << __FUNCTION__ << "\n";
    }

    void on_tx_failed(const TxID&) override
    {
        cout << __FUNCTION__ << "\n";
    }

    void register_tx(const TxID&, const Transaction::Ptr&, const Merkle::Hash*, wallet::SubTxID) override
    {
        cout << "sent tx registration request\n";
    }

    void confirm_kernel(const TxID&, const Merkle::Hash&, wallet::SubTxID) override
    {
        cout << "confirm kernel\n";
    }

    bool get_tip(Block::SystemState::Full& state) const override
    {
        return true;
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

class OneTimeBbsEndpoint : public WalletNetworkViaBbs
{
public:
    OneTimeBbsEndpoint(IWalletMessageConsumer& wallet, std::shared_ptr<proto::FlyClient::INetwork> nodeEndpoint, const IWalletDB::Ptr& walletDB)
        : WalletNetworkViaBbs(wallet, nodeEndpoint, walletDB)
    {

    }
private:
    void OnIncomingMessage() override
    {
        io::Reactor::get_Current().stop();
    }

};

class TestWallet : public Wallet
{
public:
    TestWallet(IWalletDB::Ptr walletDB, TxCompletedAction&& action = TxCompletedAction(), UpdateCompletedAction&& updateCompleted = UpdateCompletedAction())
        : Wallet{ walletDB, std::move(action), std::move(updateCompleted)}
        , m_FlushTimer{ io::Timer::create(io::Reactor::get_Current()) }
    {

    }

    void SetBufferSize(size_t s)
    {
        FlushBuffer();
        m_Buffer.reserve(s);
    }
private:
    void register_tx(const TxID& txID, const Transaction::Ptr& tx, const Merkle::Hash* pParentCtx, SubTxID subTxID = kDefaultSubTxID) override
    {
        m_FlushTimer->cancel();
        m_FlushTimer->start(1000, false, [this]() {FlushBuffer(); });
        if (m_Buffer.capacity() == 0)
        {
            Wallet::SendTransactionToNode(txID, tx, pParentCtx, subTxID);
            return;
        }
        
        for (const auto& t : m_Buffer)
        {
            if (get<0>(t) == txID) return;
        }

        assert(m_Buffer.size() < m_Buffer.capacity());
        
        m_Buffer.push_back(std::make_tuple(txID, tx, subTxID));

        if (m_Buffer.size() == m_Buffer.capacity())
        {
            FlushBuffer();
        }
    }

    void FlushBuffer()
    {
        for (const auto& t : m_Buffer)
        {
            Wallet::SendTransactionToNode(std::get<0>(t), std::get<1>(t), nullptr, std::get<2>(t));
        }
        m_Buffer.clear();
    }

    vector<tuple<TxID, Transaction::Ptr, SubTxID>> m_Buffer;
    io::Timer::Ptr m_FlushTimer;
};

struct TestWalletRig
{
    enum Type
    {
        Regular,
        RegularWithoutPoWBbs,
        Offline
    };

    TestWalletRig(IWalletDB::Ptr walletDB, Wallet::TxCompletedAction&& action = Wallet::TxCompletedAction(), Type type = Type::Regular, bool oneTimeBbsEndpoint = false, uint32_t nodePollPeriod_ms = 0, io::Address nodeAddress = io::Address::localhost().port(32125))
        : m_WalletDB{ walletDB }

    {
        m_Wallet = std::make_shared<TestWallet>(m_WalletDB, move(action), Wallet::UpdateCompletedAction());

        WalletAddress wa;
        m_WalletDB->createAddress(wa);
        m_WalletDB->saveAddress(wa);
        m_BbsAddr = wa.m_BbsAddr;
        m_OwnID = wa.m_OwnID;
        m_WalletDB->get_Endpoint(m_Endpoint, m_OwnID);

        m_Wallet->ResumeAllTransactions();

        switch (type)
        {
        case Type::Regular:
            {
                auto nodeEndpoint = make_shared<proto::FlyClient::NetworkStd>(*m_Wallet);
                nodeEndpoint->m_Cfg.m_PollPeriod_ms = nodePollPeriod_ms;
                nodeEndpoint->m_Cfg.m_vNodes.push_back(nodeAddress);
                nodeEndpoint->Connect();
                if (oneTimeBbsEndpoint)
                {
                    m_messageEndpoint = make_shared<OneTimeBbsEndpoint>(*m_Wallet, nodeEndpoint, m_WalletDB);
                }
                else
                {
                    m_messageEndpoint = make_shared<WalletNetworkViaBbs>(*m_Wallet, nodeEndpoint, m_WalletDB);
                }
                m_Wallet->SetNodeEndpoint(nodeEndpoint);
                break;
            }
        case Type::RegularWithoutPoWBbs:
            {
                auto nodeEndpoint = make_shared<proto::FlyClient::NetworkStd>(*m_Wallet);
                nodeEndpoint->m_Cfg.m_PollPeriod_ms = nodePollPeriod_ms;
                nodeEndpoint->m_Cfg.m_vNodes.push_back(nodeAddress);
                nodeEndpoint->Connect();
                if (oneTimeBbsEndpoint)
                {
                    auto tmp = make_shared<OneTimeBbsEndpoint>(*m_Wallet, nodeEndpoint, m_WalletDB);

                    tmp->m_MineOutgoing = false;
                    m_messageEndpoint = tmp;
                }
                else
                {
                    auto tmp = make_shared<WalletNetworkViaBbs>(*m_Wallet, nodeEndpoint, m_WalletDB);

                    tmp->m_MineOutgoing = false;
                    m_messageEndpoint = tmp;
                }
                m_Wallet->SetNodeEndpoint(nodeEndpoint);
                break;
            }
        case Type::Offline:
            break;
        }

        if (m_messageEndpoint)
        {
            m_Wallet->AddMessageEndpoint(m_messageEndpoint);
        }
    }


    vector<Coin> GetCoins()
    {
        vector<Coin> coins;
        m_WalletDB->visitCoins([&coins](const Coin& c)->bool
        {
            coins.push_back(c);
            return true;
        });
        return coins;
    }

    WalletID m_BbsAddr;
    PeerID m_Endpoint;
    uint64_t m_OwnID;
    IWalletDB::Ptr m_WalletDB;
    std::shared_ptr<TestWallet> m_Wallet;
    IWalletMessageEndpoint::Ptr m_messageEndpoint;
};

struct TestWalletNetwork
    : public IWalletMessageEndpoint
    , public AsyncProcessor
{
    struct Entry
    {
        IWalletMessageConsumer* m_pSink;
        std::deque<std::pair<WalletID, wallet::SetTxParameter> > m_Msgs;
    };

    typedef std::map<WalletID, Entry> WalletMap;
    WalletMap m_Map;

    virtual void Send(const WalletID& peerID, const wallet::SetTxParameter& msg) override
    {
        WalletMap::iterator it = m_Map.find(peerID);
        WALLET_CHECK(m_Map.end() != it);

        it->second.m_Msgs.push_back(std::make_pair(peerID, msg));

        PostAsync();
    }

    virtual void SendRawMessage(const WalletID& peerID, const ByteBuffer& msg) override
    {
    }

    virtual void Proceed() override
    {
        for (WalletMap::iterator it = m_Map.begin(); m_Map.end() != it; ++it)
            for (Entry& v = it->second; !v.m_Msgs.empty(); v.m_Msgs.pop_front())
                v.m_pSink->OnWalletMessage(v.m_Msgs.front().first, v.m_Msgs.front().second);
    }
};

struct TestBlockchain
{
    MiniBlockChain m_mcm;

    UtxoTree m_Utxos;

    struct KrnPerBlock
    {
        std::vector<Merkle::Hash> m_vKrnIDs;
        std::vector<TxKernel::Ptr> m_Kernels;

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

    std::vector<Merkle::Hash> m_vCSA;

    struct MyEvaluator
        :public Block::SystemState::Evaluator
    {
        TestBlockchain& m_This;
        MyEvaluator(TestBlockchain& x) :m_This(x) {}

        Merkle::Hash m_hvKernels;
        Merkle::Hash m_hvCSA;

        bool get_Utxos(Merkle::Hash& hv) override {
            m_This.m_Utxos.get_Hash(hv);
            return true;
        }
        bool get_Kernels(Merkle::Hash& hv) override {
            hv = m_hvKernels;
            return true;
        }
        bool get_Logs(Merkle::Hash& hv) override {
            hv = Zero;
            return true;
        }
        bool get_Shielded(Merkle::Hash& hv) override {
            hv = Zero;
            return true;
        }
        bool get_Assets(Merkle::Hash& hv) override {
            hv = Zero;
            return true;
        }
        bool get_Contracts(Merkle::Hash& hv) override {
            hv = Zero;
            return true;
        }
        bool get_CSA(Merkle::Hash& hv) override {
            if (!Evaluator::get_CSA(hv))
                return false;

            m_hvCSA = hv;
            return true;
        }
    };

    void AddBlock()
    {
        MyEvaluator ev(*this);
        ev.m_Height = m_mcm.m_vStates.size() + 1;

        if (m_vBlockKernels.size() < ev.m_Height)
            m_vBlockKernels.emplace_back();
        assert(m_vBlockKernels.size() == ev.m_Height);

        KrnPerBlock::Mmr fmmr(m_vBlockKernels.back());
        fmmr.get_Hash(ev.m_hvKernels);

        ev.get_Live(m_mcm.m_hvLive);
        m_mcm.Add();

        auto& s = m_mcm.m_vStates.back().m_Hdr;

        if (ev.m_Height >= Rules::get().pForks[3].m_Height)
        {
            ev.get_Utxos(s.m_Kernels);
            m_vCSA.push_back(ev.m_hvCSA);
        }
        else
            ev.get_Kernels(s.m_Kernels);
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
		m_Utxos.OnDirty();

        if (bCreate)
            p->m_ID = 0;
        else
        {
            // protect again overflow attacks, though it's highly unlikely (Input::Count is currently limited to 32 bits, it'd take millions of blocks)
            Input::Count nCountInc = p->get_Count() + 1;
            if (!nCountInc)
                return false;

			m_Utxos.PushID(0, *p);
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
        t.m_pBound[0] = kMin.V.m_pData;
        t.m_pBound[1] = kMax.V.m_pData;

        if (m_Utxos.Traverse(t))
            return false;

        p = &(UtxoTree::MyLeaf&) cu.get_Leaf();

        d = p->m_Key;
        assert(d.m_Commitment == c);

        if (!p->IsExt())
            m_Utxos.Delete(cu);
        else
        {
            m_Utxos.PopID(*p);
            cu.InvalidateElement();
			m_Utxos.OnDirty();
        }

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

                ret.m_State.m_Count = v.get_Count();
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

        t.m_pBound[0] = kMin.V.m_pData;
        t.m_pBound[1] = kMax.V.m_pData;

        t.m_pTree->Traverse(t);
        t.m_Msg.m_Proofs.swap(msgOut.m_Proofs);
    }

    Height get_KrnProofInner(const Merkle::Hash& krnID, Merkle::Proof& proof, TxKernel** ppKrn = nullptr)
    {
        for (size_t iState = m_mcm.m_vStates.size(); iState--; )
        {
            const KrnPerBlock& kpb = m_vBlockKernels[iState];

            for (size_t i = 0; i < kpb.m_vKrnIDs.size(); i++)
            {
                if (kpb.m_vKrnIDs[i] == krnID)
                {
                    KrnPerBlock::Mmr fmmr(kpb);
                    Merkle::ProofBuilderStd bld;
                    fmmr.get_Proof(bld, i);

                    proof.swap(bld.m_Proof);

                    Height h = Rules::HeightGenesis + iState;
                    const Height hf3 = Rules::get().pForks[3].m_Height;
                    if (h >= hf3)
                    {
                        proof.emplace_back();
                        proof.back().first = true;
                        proof.back().second = Zero; // logs

                        proof.emplace_back();
                        proof.back().first = true;
                        proof.back().second = m_vCSA[h - hf3];

                        proof.emplace_back();
                        proof.back().first = false;

                        uint64_t nCount = h - Rules::HeightGenesis;
                        TemporarySwap<uint64_t>(nCount, m_mcm.m_Mmr.m_Count);
                        m_mcm.m_Mmr.get_Hash(proof.back().second);

                    }

                    if (ppKrn)
                        *ppKrn = kpb.m_Kernels[i].get();

                    return h;
                }
            }
        }

        return 0;
    }


    void GetProof(const proto::GetProofKernel& data, proto::ProofKernel& msgOut)
    {
        Height h = get_KrnProofInner(data.m_ID, msgOut.m_Proof.m_Inner);
        if (!h)
            return;
        size_t iState = h - Rules::HeightGenesis;

        msgOut.m_Proof.m_State = m_mcm.m_vStates[iState].m_Hdr;

        if (iState + 1 != m_mcm.m_vStates.size())
        {
            Merkle::ProofBuilderHard bld2;
            m_mcm.m_Mmr.get_Proof(bld2, iState);
            msgOut.m_Proof.m_Outer.swap(bld2.m_Proof);

            {
                MyEvaluator ev(*this);
                ev.m_Height = m_mcm.m_vStates.size() + 1;
                ev.get_Live(msgOut.m_Proof.m_Outer.emplace_back());
            }

            Block::SystemState::Full state = m_mcm.m_vStates[m_mcm.m_vStates.size() - 1].m_Hdr;
            WALLET_CHECK(state.IsValidProofKernel(data.m_ID, msgOut.m_Proof));
        }
    }

    void GetProof(const proto::GetProofKernel2& data, proto::ProofKernel2& msgOut)
    {
        TxKernel* pKrn = nullptr;
        Height h = get_KrnProofInner(data.m_ID, msgOut.m_Proof, &pKrn);
        if (!h)
            return;

        msgOut.m_Height = h;

        if (data.m_Fetch)
			pKrn->Clone(msgOut.m_Kernel);
        return;
    }


    void AddKernel(const TxKernel& krn)
    {
        if (m_vBlockKernels.size() <= m_mcm.m_vStates.size())
            m_vBlockKernels.emplace_back();

        KrnPerBlock& kpb = m_vBlockKernels.back();
        kpb.m_vKrnIDs.push_back(krn.m_Internal.m_ID);

		kpb.m_Kernels.emplace_back();
		krn.Clone(kpb.m_Kernels.back());
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

            for (List::iterator it = m_lst.begin(); m_lst.end() != it; ++it)
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
			v.m_Res.m_Value = proto::TxStatus::Ok;

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

        case Request::Type::Asset:
        {
            //proto::FlyClient::RequestAsset& v = static_cast<proto::FlyClient::RequestAsset&>(r);
            //m_Shared.m_Blockchain.GetProof(v.m_Msg, v.m_Res);
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
    using NewBlockFunc = std::function<void(Height)>;
    TestNode(NewBlockFunc func = NewBlockFunc(), Height height = 145, uint16_t port = 32125)
        : m_NewBlockFunc(func)
    {
        m_Server.Listen(io::Address::localhost().port(port));
        while (m_Blockchain.m_mcm.m_vStates.size() < height)
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
    std::vector<ECC::Point::Storage> m_vShieldedPool;

    void AddBlock()
    {
        m_Blockchain.AddBlock();

        for (ClientList::iterator it = m_lstClients.begin(); m_lstClients.end() != it; ++it)
        {
            Client& c = *it;
            if (c.IsSecureOut())
                c.SendTip();
        }

        if (m_NewBlockFunc)
        {
            m_NewBlockFunc(m_Blockchain.m_mcm.m_vStates.back().m_Hdr.m_Height);
        }
    }

    Height GetHeight() const
    {
        return m_Blockchain.m_mcm.m_vStates.back().m_Hdr.m_Height;
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

            SendLogin();

            SendTip();
        }

		void SetupLogin(proto::Login& msg) override
		{
			msg.m_Flags |=
				proto::LoginFlags::SpreadingTransactions |
				proto::LoginFlags::Bbs |
				proto::LoginFlags::SendPeers;
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

			proto::Status msg;
			msg.m_Value = proto::TxStatus::Ok;
			Send(msg);

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

            for (ClientList::iterator it = m_This.m_lstClients.begin(); m_This.m_lstClients.end() != it; ++it)
            {
                Client& c = *it;
                if ((&c != this) && c.m_Subscribed)
                    c.Send(msg);
            }
        }

        void OnMsg(proto::Ping&& msg) override
        {
            Send(proto::Pong());
        }

        void OnMsg(proto::GetStateSummary&& msg) override
        {
            proto::StateSummary msgOut;
            msgOut.m_ShieldedOuts = m_This.m_vShieldedPool.size();
            Send(msgOut);
        }

        void OnMsg(proto::GetShieldedOutputsAt&& msg) override
        {
            proto::ShieldedOutputsAt msgOut;
            Send(msgOut);
        }

        void OnMsg(proto::GetShieldedList&& msg) override
        {
            const std::vector<ECC::Point::Storage>& v = m_This.m_vShieldedPool; // alias

            proto::ShieldedList msgOut;
            auto nShieldedOuts = v.size();

            if (msg.m_Id0 < nShieldedOuts)
            {
                size_t n = nShieldedOuts - msg.m_Id0;
                std::setmin(n, msg.m_Count);
                std::setmin(n, Rules::get().Shielded.m_ProofMax.get_N() * 2);

                msgOut.m_Items.resize(n);
                std::copy(v.begin() + msg.m_Id0, v.begin() + msg.m_Id0 + n, msgOut.m_Items.begin());
            }

            Send(msgOut);
        }

        void OnMsg(proto::GetProofAsset&& msg) override
        {
            proto::ProofAsset msgOut;
            Send(msgOut);
        }

        void OnMsg(proto::GetBodyPack&& msg) override
        {
            if (msg.m_CountExtra)
            {
                Send(proto::BodyPack{});
            }
            else
            {
                Send(proto::Body{});
            }
        }


        void OnDisconnect(const DisconnectReason& r) override
        {
            switch (r.m_Type)
            {
            case DisconnectReason::Protocol:
            case DisconnectReason::ProcessingExc:
                BEAM_LOG_ERROR() << "Disconnect: " << r;
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
    NewBlockFunc m_NewBlockFunc;

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

class PerformanceRig
{
public:
    PerformanceRig(int txCount, int txPerCall = 1)
        : m_TxCount(txCount)
        , m_TxPerCall(txPerCall)
    {

    }

    void Run()
    {
        io::Reactor::Ptr mainReactor{ io::Reactor::create() };
        io::Reactor::Scope scope(*mainReactor);

        auto completedCount = 2 * m_TxCount;
        auto f = [&completedCount, mainReactor, count = 2 * m_TxCount](auto)
        {
            --completedCount;
            if (completedCount == 0)
            {
                mainReactor->stop();
                completedCount = count;
            }
        };

        TestNode node;
        TestWalletRig sender(createSenderWalletDB((int)m_TxCount, 6), f);
        TestWalletRig receiver(createReceiverWalletDB(), f);

        io::Timer::Ptr timer = io::Timer::create(*mainReactor);
        auto timestamp = GetTime_ms();
        m_MaxLatency = 0;

        io::AsyncEvent::Ptr accessEvent;
        accessEvent = io::AsyncEvent::create(*mainReactor, [&timestamp, this, &accessEvent]()
        {
            auto newTimestamp = GetTime_ms();
            auto latency = newTimestamp - timestamp;
            timestamp = newTimestamp;
            if (latency > 100)
            {
                cout << "Latency: " << float(latency) / 1000 << " s\n";
            }
            setmax(m_MaxLatency, latency);
            accessEvent->post();
        });
        accessEvent->post();

        helpers::StopWatch sw;
        sw.start();

        io::Timer::Ptr sendTimer = io::Timer::create(*mainReactor);

        size_t sendCount = m_TxCount;
        io::AsyncEvent::Ptr sendEvent;
        sendEvent = io::AsyncEvent::create(*mainReactor, [&sender, &receiver, &sendCount, &sendEvent, this]()
        {
            for (size_t i = 0; i < m_TxPerCall && sendCount; ++i)
            {
                if (sendCount--)
                {
                    sender.m_Wallet->StartTransaction(CreateSimpleTransactionParameters()
                        .SetParameter(TxParameterID::MyAddr, sender.m_BbsAddr)
                        .SetParameter(TxParameterID::PeerAddr, receiver.m_BbsAddr)
                        .SetParameter(TxParameterID::Amount, Amount(5))
                        .SetParameter(TxParameterID::Fee, Amount(1))
                        .SetParameter(TxParameterID::Lifetime, Height(10000))
                        .SetParameter(TxParameterID::PeerResponseTime, Height(10000)));
                }
            }
            if (sendCount)
            {
                sendEvent->post();
            }
        });
        sendEvent->post();

        mainReactor->run();
        sw.stop();
        m_TotalTime = sw.milliseconds();

        auto txHistory = sender.m_WalletDB->getTxHistory();
        
        size_t totalTxCount = m_TxCount * m_TxPerCall;
        WALLET_CHECK(txHistory.size() == totalTxCount);
        for (const auto& tx : txHistory)
        {
            WALLET_CHECK(tx.m_status == wallet::TxStatus::Completed);
        }
    }

    uint64_t GetTotalTime() const
    {
        return m_TotalTime;
    }

    uint32_t GetMaxLatency() const
    {
        return m_MaxLatency;
    }

    size_t GetTxCount() const
    {
        return m_TxCount;
    }

    size_t GetTxPerCall() const
    {
        return m_TxPerCall;
    }


private:
    size_t m_TxCount;
    size_t m_TxPerCall;
    uint32_t m_MaxLatency = 0;
    uint64_t m_TotalTime = 0;
};

