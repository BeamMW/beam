#include "wallet.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include "core/ecc_native.h"
#include "core/block_crypt.h"
#include "utility/logger.h"
#include "utility/helpers.h"
#include <algorithm>
#include <random>
#include "core/storage.h"
#include <iomanip>

namespace ECC {
	Initializer g_Initializer;
}

namespace
{
    template<typename T>
    struct Cleaner
    {
        Cleaner(T& t) : m_v{ t } {}
        ~Cleaner()
        {
            if (!m_v.empty())
            {
                m_v.clear();
            }
        }
        T& m_v;
    };

    const char* ReceiverPrefix = "[Receiver] ";
    const char* SenderPrefix = "[Sender] ";
}

namespace beam
{
    using namespace wallet;
    using namespace std;
    using namespace ECC;

    std::ostream& operator<<(std::ostream& os, const Uuid& uuid)
    {
        os << "[" << to_hex(uuid.data(), uuid.size()) << "]";
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const PrintableAmount& amount)
    {
        const string_view beams{" beams " };
        const string_view chattles{ " chattles " };
        auto width = os.width();
        if (amount.m_value >= Rules::Coin)
        {
            os << setw(width - beams.length()) << Amount(amount.m_value / Rules::Coin) << beams.data();
        }
        Amount c = amount.m_value % Rules::Coin;
        if (c > 0 || amount.m_value == 0)
        {
            os << setw(width - chattles.length()) << c << chattles.data();
        }
        return os;
    }

    namespace wallet
    {
        pair<Scalar::Native, Scalar::Native> splitKey(const Scalar::Native& key, uint64_t index)
        {
            pair<Scalar::Native, Scalar::Native> res;
            Hash::Value hv;
            Hash::Processor() << index >> hv;
            NoLeak<Scalar> s;
            s.V = key;
            res.second.GenerateNonce(s.V.m_Value, hv, nullptr);
            res.second = -res.second;
            res.first = key;
            res.first += res.second;

            return res;
        }

        Timestamp getTimestamp()
        {
            return beam::Timestamp(std::chrono::seconds(std::time(nullptr)).count());
        }
    }

    Wallet::Wallet(IKeyChain::Ptr keyChain, INetworkIO& network, TxCompletedAction&& action)
        : m_keyChain{ keyChain }
        , m_network{ network }
        , m_tx_completed_action{move(action)}
        , m_Definition{}
        , m_knownStateID{}
        , m_newStateID{}
        , m_syncing{0}
        , m_synchronized{false}
    {
        assert(keyChain);
        m_keyChain->getSystemStateID(m_knownStateID);
    }

    Wallet::~Wallet()
    {
        assert(m_peers.empty());
        assert(m_receivers.empty());
        assert(m_senders.empty());
        assert(m_reg_requests.empty());
        assert(m_removed_senders.empty());
        assert(m_removed_receivers.empty());
    }

    Uuid Wallet::transfer_money(PeerId to, Amount amount, ByteBuffer&& message)
    {
        Cleaner<std::vector<wallet::Sender::Ptr> > c{ m_removed_senders };
		boost::uuids::uuid id = boost::uuids::random_generator()();
        Uuid txId{};
        copy(id.begin(), id.end(), txId.begin());
        m_peers.emplace(txId, to);
        TxDescription tx{ txId, amount, to, move(message), wallet::getTimestamp(), true};
        resume_sender(tx);
        return txId;
    }

    void Wallet::resume_tx(const TxDescription& tx)
    {
        if (tx.m_sender)
        {
            resume_sender(tx);
        }
        else
        {
            resume_receiver(tx);
        }
    }

    void Wallet::resume_all_tx()
    {
        auto txs = m_keyChain->getTxHistory();
        for (auto& tx : txs)
        {
            resume_tx(tx);
        }
    }

    void Wallet::send_tx_invitation(const TxDescription& tx, InviteReceiver&& data)
    {
        m_network.send_tx_message(tx.m_peerId, move(data));
    }

    void Wallet::send_tx_confirmation(const TxDescription& tx, ConfirmTransaction&& data)
    {
        m_network.send_tx_message(tx.m_peerId, move(data));
    }

    void Wallet::on_tx_completed(const TxDescription& tx)
    {
        remove_sender(tx.m_txId);
        remove_receiver(tx.m_txId);

        // remove state machine from db
        TxDescription t{ tx };
        t.m_fsmState.clear();
        m_keyChain->saveTx(t);

        if (m_tx_completed_action)
        {
            m_tx_completed_action(tx.m_txId);
        }
        if (m_reg_requests.empty() && m_syncing == 0)
        {
            m_network.close_node_connection();
        }
    }


    void Wallet::send_tx_failed(const TxDescription& tx)
    {
        m_network.send_tx_message(tx.m_peerId, wallet::TxFailed{ tx.m_txId });
    }

    void Wallet::remove_sender(const Uuid& txId)
    {
        auto it = m_senders.find(txId);
        if (it != m_senders.end())
        {
            remove_peer(txId);
            m_removed_senders.push_back(move(it->second));
            m_senders.erase(it);
        }
    }

    void Wallet::remove_receiver(const Uuid& txId)
    {
        auto it = m_receivers.find(txId);
        if (it != m_receivers.end())
        {
            remove_peer(txId);
            m_removed_receivers.push_back(move(it->second));
            m_receivers.erase(it);
        }
    }

    void Wallet::send_tx_confirmation(const TxDescription& tx, ConfirmInvitation&& data)
    {
        m_network.send_tx_message(tx.m_peerId, move(data));
    }

    void Wallet::register_tx(const TxDescription& tx, Transaction::Ptr data)
    {
        register_tx(tx.m_txId, data);
    }

    void Wallet::send_tx_registered(const TxDescription& tx)
    {
        m_network.send_tx_message(tx.m_peerId, wallet::TxRegistered{ tx.m_txId, true });
    }

    void Wallet::handle_tx_message(PeerId from, InviteReceiver&& data)
    {
        auto it = m_receivers.find(data.m_txId);
        if (it == m_receivers.end())
        {
            LOG_VERBOSE() << ReceiverPrefix << "Received tx invitation " << data.m_txId;
            m_peers.emplace(data.m_txId, from);
            TxDescription tx{ data.m_txId, data.m_amount, from, {}, wallet::getTimestamp(), false };
            auto r = make_shared<Receiver>(*this, m_keyChain, tx, data);
            m_receivers.emplace(tx.m_txId, r);
            if (m_synchronized)
            {
                r->start();
            }
            else
            {
                m_pendingReceivers.emplace_back(r);
            }
        }
        else
        {
            LOG_DEBUG() << ReceiverPrefix << "Unexpected tx invitation " << data.m_txId;
        }
    }
    
    void Wallet::handle_tx_message(PeerId from, ConfirmTransaction&& data)
    {
        Cleaner<std::vector<wallet::Receiver::Ptr> > c{ m_removed_receivers };
        auto it = m_receivers.find(data.m_txId);
        if (it != m_receivers.end())
        {
            LOG_DEBUG() << ReceiverPrefix << "Received sender tx confirmation " << data.m_txId;
            it->second->process_event(Receiver::TxConfirmationCompleted{data});
        }
        else
        {
            LOG_DEBUG() << ReceiverPrefix << "Unexpected sender tx confirmation "<< data.m_txId;
            m_network.close_connection(from);
        }
    }

    void Wallet::handle_tx_message(PeerId /*from*/, ConfirmInvitation&& data)
    {
        Cleaner<std::vector<wallet::Sender::Ptr> > c{ m_removed_senders };
        auto it = m_senders.find(data.m_txId);
        if (it != m_senders.end())
        {
            LOG_VERBOSE() << SenderPrefix << "Received tx confirmation " << data.m_txId;
            it->second->process_event(Sender::TxInitCompleted{data});
        }
        else
        {
            LOG_DEBUG() << SenderPrefix << "Unexpected tx confirmation " << data.m_txId;
        }
    }

    void Wallet::handle_tx_message(PeerId from, wallet::TxRegistered&& data)
    {
        // TODO: change data structure
        auto cit = find_if(m_peers.cbegin(), m_peers.cend(), [from](const auto& p) {return p.second == from; });
        if (cit == m_peers.end())
        {
            return;
        }
        handle_tx_registered(cit->first, data.m_value);
    }

    void Wallet::handle_tx_message(PeerId /*from*/, wallet::TxFailed&& data)
    {
        LOG_DEBUG() << "tx " << data.m_txId << " failed";
        handle_tx_failed(data.m_txId);
    }

    bool Wallet::handle_node_message(proto::Boolean&& res)
    {
        if (m_reg_requests.empty())
        {
            LOG_DEBUG() << "Received unexpected tx registration confirmation";
            assert(m_receivers.empty() && m_senders.empty());
            return false;
        }
        auto txId = m_reg_requests.front().first;
        m_reg_requests.pop_front();
        handle_tx_registered(txId, res.m_Value);
        return true;
    }

    void Wallet::handle_tx_registered(const Uuid& txId, bool res)
    {
        LOG_DEBUG() << "tx " << txId << (res ? " has registered" : " has failed to register");
        Cleaner<std::vector<wallet::Receiver::Ptr> > cr{ m_removed_receivers };
        Cleaner<std::vector<wallet::Sender::Ptr> > cs{ m_removed_senders };
        if (res)
        {
            if (auto it = m_receivers.find(txId); it != m_receivers.end())
            {
                it->second->process_event(Receiver::TxRegistrationCompleted{ txId });
                return;
            }
            if (auto it = m_senders.find(txId); it != m_senders.end())
            {
                it->second->process_event(Sender::TxConfirmationCompleted());
                return;
            }
        }
        else
        {
            handle_tx_failed(txId);
        }
    }

    void Wallet::handle_tx_failed(const Uuid& txId)
    {
        if (auto it = m_senders.find(txId); it != m_senders.end())
        {
            it->second->process_event(Sender::TxFailed());
            return;
        }
        if (auto it = m_receivers.find(txId); it != m_receivers.end())
        {
            it->second->process_event(Receiver::TxFailed());
            return;
        }
    }

    bool Wallet::handle_node_message(proto::ProofUtxo&& utxoProof)
    {
        // TODO: handle the maturity of the several proofs (> 1)
        if (m_pendingProofs.empty())
        {
            LOG_DEBUG() << "Unexpected UTXO proof";
            return false;
        }

        Coin& coin = m_pendingProofs.front();
		Input input;
		input.m_Commitment = Commitment(m_keyChain->calcKey(coin), coin.m_amount);
        if (utxoProof.m_Proofs.empty())
        {
            LOG_ERROR() << "Got empty proof for: " << input.m_Commitment;

            if (coin.m_status == Coin::Locked)
            {
                coin.m_status = Coin::Spent;
                m_keyChain->update(vector<Coin>{coin});
            }
        }
        else
        {
            for (const auto& proof : utxoProof.m_Proofs)
            {
                if (coin.m_status == Coin::Unconfirmed)
                {

                    if (proof.IsValid(input, m_Definition))
                    {
                        LOG_ERROR() << "Got proof for: " << input.m_Commitment;
                        coin.m_status = Coin::Unspent;
                        coin.m_maturity = proof.m_Maturity;
                        if (coin.m_key_type == KeyType::Coinbase
                            || coin.m_key_type == KeyType::Comission)
                        {
                            LOG_INFO() << "Block reward received: " << PrintableAmount(coin.m_amount);
                            m_keyChain->store(coin);
                        }
                        else
                        {
                            m_keyChain->update(vector<Coin>{coin});
                        }
                    }
                    else
                    {
                        LOG_ERROR() << "Invalid proof provided";
                    }
                }
            }
        }

        m_pendingProofs.pop_front();

        return finishSync();
    }

    bool Wallet::handle_node_message(proto::NewTip&& msg)
    {
        // TODO: restore from wallet db 
        for (auto& r : m_pending_reg_requests)
        {
            register_tx(r.first, r.second);
        }

        m_pending_reg_requests.clear();

        if (!m_syncing)
        {
            ++m_syncing; // Hdr
        }

        if (msg.m_ID > m_knownStateID)
        {
            m_newStateID = msg.m_ID;
            m_synchronized = false;
            ++m_syncing; // Mined
            m_network.send_node_message(proto::GetMined{ m_knownStateID.m_Height });
        }
        return true;
    }

    bool Wallet::handle_node_message(proto::Hdr&& msg)
    {
		m_Definition = msg.m_Description.m_Definition;

        // TODO: do one kernel proof instead many per coin proofs
        vector<Coin> unconfirmed;
        m_keyChain->visit([&](const Coin& coin)
        {
            if (coin.m_status == Coin::Unconfirmed
             || coin.m_status == Coin::Locked)
            {
                unconfirmed.emplace_back(coin);
            }

            return true;
        });

        getUtxoProofs(unconfirmed);

        Block::SystemState::ID newID = {};
        msg.m_Description.get_ID(newID);
        m_newStateID = newID;
        return finishSync();
    }

    bool Wallet::handle_node_message(proto::Mined&& msg)
    {
        vector<Coin> mined;
        auto currentHeight = m_keyChain->getCurrentHeight();
        for (auto& minedCoin : msg.m_Entries)
        {
            if (minedCoin.m_Active && minedCoin.m_ID.m_Height >= currentHeight) // we store coins from active branch
            {
                // coinbase 
                mined.emplace_back(Rules::CoinbaseEmission
                                 , Coin::Unconfirmed
                                 , minedCoin.m_ID.m_Height
                                 , MaxHeight
                                 , KeyType::Coinbase);
                if (minedCoin.m_Fees > 0)
                {
                    mined.emplace_back(minedCoin.m_Fees
                                     , Coin::Unconfirmed
                                     , minedCoin.m_ID.m_Height
                                     , MaxHeight
                                     , KeyType::Comission);
                }
            }
        }

        if (!mined.empty())
        {
            getUtxoProofs(mined);
        }
        return finishSync();
    }

    void Wallet::handle_connection_error(PeerId from)
    {
        // TODO: change data structure, we need multi index here
        auto cit = find_if(m_peers.cbegin(), m_peers.cend(), [from](const auto& p) {return p.second == from; });
        if (cit == m_peers.end())
        {
            return;
        }
        Cleaner<std::vector<wallet::Receiver::Ptr> > cr{ m_removed_receivers };
        Cleaner<std::vector<wallet::Sender::Ptr> > cs{ m_removed_senders };
        if (auto it = m_receivers.find(cit->first); it != m_receivers.end())
        {
            it->second->process_event(Receiver::TxFailed());
            return;
        }
        if (auto it = m_senders.find(cit->first); it != m_senders.end())
        {
            it->second->process_event(Sender::TxFailed());
            return;
        }
    }

    void Wallet::stop_sync()
    {
        m_syncing = 0;
        copy(m_reg_requests.begin(), m_reg_requests.end(), back_inserter(m_pending_reg_requests));
        m_reg_requests.clear();
        m_pendingProofs.clear();
    }

    void Wallet::remove_peer(const Uuid& txId)
    {
        auto it = m_peers.find(txId);
        if (it != m_peers.end())
        {
            m_network.close_connection(it->second);
            m_peers.erase(it);
        }
    }

    void Wallet::getUtxoProofs(const vector<Coin>& coins)
    {
        for (auto& coin : coins)
        {
            ++m_syncing;
            m_pendingProofs.push_back(coin);
			Input input;
			input.m_Commitment = Commitment(m_keyChain->calcKey(coin), coin.m_amount);
            LOG_DEBUG() << "Get proof: " << input.m_Commitment;
            m_network.send_node_message(proto::GetProofUtxo{ input, 0 });
        }
    }

    bool Wallet::finishSync()
    {
        if (m_syncing)
        {
            --m_syncing;
            if (!m_syncing)
            {
                m_keyChain->setSystemStateID(m_newStateID);
                m_knownStateID = m_newStateID;
                if (!m_pendingSenders.empty())
                {
                    Cleaner<std::vector<wallet::Sender::Ptr> > cr{ m_removed_senders };
                    
                    for (auto& s : m_pendingSenders)
                    {
                        s->start();
                    }
                    m_pendingSenders.clear();
                }

                if (!m_pendingReceivers.empty())
                {
                    Cleaner<std::vector<wallet::Receiver::Ptr> > cr{ m_removed_receivers };
                    for (auto& r : m_pendingReceivers)
                    {
                        r->start();
                    }
                    m_pendingReceivers.clear();
                }
                m_synchronized = true;
            }
        }
        if (!m_syncing && m_reg_requests.empty())
        {
            m_network.close_node_connection();
            return false;
        }
        return true;
    }

    void Wallet::register_tx(const Uuid& txId, Transaction::Ptr data)
    {
        LOG_VERBOSE() << ReceiverPrefix << "sending tx for registration";
        TxBase::Context ctx;
        assert(data->IsValid(ctx));
        m_reg_requests.push_back(make_pair(txId, data));
        m_network.send_node_message(proto::NewTransaction{ data });
    }

    void Wallet::resume_sender(const TxDescription& tx)
    {
        auto s = make_shared<Sender>(*this, m_keyChain, tx);
        m_senders.emplace(tx.m_txId, s);
        if (m_synchronized)
        {
            s->start();
        }
        else
        {
            m_pendingSenders.emplace_back(s);
        }
    }

    void Wallet::resume_receiver(const TxDescription& tx, InviteReceiver&& data)
    {
        auto p = m_receivers.emplace(tx.m_txId, make_shared<Receiver>(*this, m_keyChain, tx, data));
        if (m_synchronized)
        {
            p.first->second->start();
        }
        else
        {
            m_pendingReceivers.emplace_back(p.first->second);
        }
    }
}
