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

        if (amount.m_showPoint)
        {
            os << setw(width - beams.length() - 1) << Amount(amount.m_value / Rules::Coin)
                << "."
                << (amount.m_value % Rules::Coin)
                << beams.data();
            return os;
        }
        
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
			res.first = key;
			ExtractOffset(res.first, res.second, index);
			res.second = -res.second; // different convention
            return res;
        }
    }

    struct Wallet::StateFinder
    {
        StateFinder(Height newHeight)
            : m_first{ 0 }
            , m_last{newHeight}
            , m_count{int64_t(newHeight + 1)}
            , m_step{0}
        {

        }

        Height getSearchHeight()
        {
            m_step = (m_count >> 1);
            return m_first + m_step;
        }

        void moveBack()
        {
            m_count = m_step;
        }

        void moveForward()
        {
            m_first += m_step + 1;
            m_count -= m_step + 1;
        }

        Height m_first;
        Height m_last;
        int64_t m_count;
        int64_t m_step;
        Block::SystemState::ID m_id;
    };


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
        assert(m_negotiators.empty());
        assert(m_reg_requests.empty());
        assert(m_removedNegotiators.empty());
    }

    Uuid Wallet::transfer_money(PeerId to, Amount amount, Amount fee, bool sender, ByteBuffer&& message)
    {
		boost::uuids::uuid id = boost::uuids::random_generator()();
        Uuid txId{};
        copy(id.begin(), id.end(), txId.begin());
        TxDescription tx( txId, amount, fee, m_keyChain->getCurrentHeight(), to, move(message), getTimestamp(), sender);
        resume_negotiator(tx);
        return txId;
    }

    void Wallet::resume_tx(const TxDescription& tx)
    {
        resume_negotiator(tx);
    }

    void Wallet::resume_all_tx()
    {
        auto txs = m_keyChain->getTxHistory();
        for (auto& tx : txs)
        {
            resume_tx(tx);
        }
    }

    void Wallet::send_tx_invitation(const TxDescription& tx, Invite&& data)
    {
        m_network.send_tx_message(tx.m_peerId, move(data));
    }

    void Wallet::send_tx_confirmation(const TxDescription& tx, ConfirmTransaction&& data)
    {
        m_network.send_tx_message(tx.m_peerId, move(data));
    }

    void Wallet::on_tx_completed(const TxDescription& tx)
    {
        auto it = m_negotiators.find(tx.m_txId);
        if (it != m_negotiators.end())
        {
            m_removedNegotiators.push_back(move(it->second));
            m_negotiators.erase(it);
        }

        m_network.close_connection(tx.m_peerId);
        m_peers.erase(tx.m_peerId);
 
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

    void Wallet::handle_tx_message(PeerId from, Invite&& msg)
    {
        auto it = m_negotiators.find(msg.m_txId);
        if (it == m_negotiators.end())
        {
            LOG_VERBOSE() << "Received tx invitation " << msg.m_txId;
            bool sender = !msg.m_send;
            TxDescription tx{ msg.m_txId, msg.m_amount, msg.m_fee, msg.m_height, from, {}, getTimestamp(), sender };
            auto r = make_shared<Negotiator>(*this, m_keyChain, tx, msg);
            m_negotiators.emplace(tx.m_txId, r);
            m_peers.emplace(tx.m_peerId, r);
            Cleaner c{ m_removedNegotiators };
            if (m_synchronized)
            {
                r->start();
                r->process_event(events::TxInvited{});
            }
            else
            {
                m_pendingEvents.emplace_back([r]()
                {
                    r->start();
                    r->process_event(events::TxInvited{});
                });
            }
        }
        else
        {
            LOG_DEBUG() << ReceiverPrefix << "Unexpected tx invitation " << msg.m_txId;
        }
    }
    
    void Wallet::handle_tx_message(PeerId from, ConfirmTransaction&& data)
    {
        LOG_DEBUG() << ReceiverPrefix << "Received sender tx confirmation " << data.m_txId;
        if (!process_event(data.m_txId, events::TxConfirmationCompleted{ data }))
        {
            LOG_DEBUG() << ReceiverPrefix << "Unexpected sender tx confirmation " << data.m_txId;
            m_network.close_connection(from);
        }
    }

    void Wallet::handle_tx_message(PeerId /*from*/, ConfirmInvitation&& data)
    {
        LOG_VERBOSE() << SenderPrefix << "Received tx confirmation " << data.m_txId;
        if (!process_event(data.m_txId, events::TxInvitationCompleted{ data }))
        {
            LOG_DEBUG() << SenderPrefix << "Unexpected tx confirmation " << data.m_txId;
        }
    }

    void Wallet::handle_tx_message(PeerId from, wallet::TxRegistered&& data)
    {
        process_event(data.m_txId, events::TxRegistrationCompleted{});
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
            assert(m_negotiators.empty());
        }
        else
        {
            auto txId = m_reg_requests.front().first;
            m_reg_requests.pop_front();
            handle_tx_registered(txId, res.m_Value);
        }
        return close_node_connection();
    }

    void Wallet::handle_tx_registered(const Uuid& txId, bool res)
    {
        LOG_DEBUG() << "tx " << txId << (res ? " has registered" : " has failed to register");
        if (res)
        {
            process_event(txId, events::TxRegistrationCompleted{ });
        }
        else
        {
            handle_tx_failed(txId);
        }
    }

    void Wallet::handle_tx_failed(const Uuid& txId)
    {
        process_event(txId, events::TxFailed());
    }

    bool Wallet::handle_node_message(proto::ProofUtxo&& utxoProof)
    {
        // TODO: handle the maturity of the several proofs (> 1)
        if (m_pendingProofs.empty())
        {
            LOG_WARNING() << "Unexpected UTXO proof";
            return finish_sync();
        }

        Coin& coin = m_pendingProofs.front();
		Input input;
		input.m_Commitment = Commitment(m_keyChain->calcKey(coin), coin.m_amount);
        if (utxoProof.m_Proofs.empty())
        {
            LOG_WARNING() << "Got empty proof for: " << input.m_Commitment;

            if (coin.m_status == Coin::Locked)
            {
                coin.m_status = Coin::Spent;
                m_keyChain->update(coin);
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
                        LOG_INFO() << "Got proof for: " << input.m_Commitment;
                        coin.m_status = Coin::Unspent;
                        coin.m_maturity = proof.m_Maturity;
                        coin.m_confirmHeight = m_newStateID.m_Height;
                        coin.m_confirmHash = m_newStateID.m_Hash;
                        if (coin.m_key_type == KeyType::Coinbase
                            || coin.m_key_type == KeyType::Comission)
                        {
                            LOG_INFO() << "Block reward received: " << PrintableAmount(coin.m_amount);
                            m_keyChain->store(coin);
                        }
                        else
                        {
                            m_keyChain->update(coin);
                        }
                    }
                    else
                    {
                        LOG_ERROR() << "Invalid proof provided: " << input.m_Commitment;
                    }
                }
            }
        }

        m_pendingProofs.pop_front();

        return finish_sync();
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

        return true;
    }

    bool Wallet::handle_node_message(proto::Hdr&& msg)
    {
        if (!m_syncing)
        {
            ++m_syncing; // Hdr
        }
        Block::SystemState::ID newID = {};
        msg.m_Description.get_ID(newID);
        
        m_Definition = msg.m_Description.m_Definition;
        m_newStateID = newID;

        if (newID == m_knownStateID)
        {
            return finish_sync();
        }

        m_synchronized = false;

        if (m_knownStateID.m_Height <= Rules::HeightGenesis)
        {
            // cold start
            do_fast_forward();
        }
        else
        {
            ++m_syncing;
            m_network.send_node_message(proto::GetProofState{ m_knownStateID.m_Height });
        }

        return finish_sync();
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
                mined.emplace_back(Rules::get().CoinbaseEmission
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
        return finish_sync();
    }

    bool Wallet::handle_node_message(proto::Proof&& msg)
    {
        Merkle::Hash hv = m_knownStateID.m_Hash;
        Merkle::Interpret(hv, msg.m_Proof);

        if (hv != m_Definition)
        {
            // rollback
            // search for the latest valid known state
            if (!m_stateFinder || m_stateFinder->m_last < m_newStateID.m_Height)
            {
                // restart search
                if (!m_stateFinder)
                {
                    LOG_INFO() << "Last known state doesn't present on current branch. Rollback... ";
                }
                else
                {
                    LOG_INFO() << "Restarting rollback..."; 
                }
                m_stateFinder.reset(new StateFinder(m_newStateID.m_Height));
            }
            auto id = m_keyChain->getKnownStateID(m_stateFinder->getSearchHeight());
            Merkle::Hash hv = id.m_Hash;
            Merkle::Interpret(hv, msg.m_Proof);
            if (hv == m_Definition)
            {
                m_stateFinder->m_id = id;
                m_stateFinder->moveForward();
            }
            else
            {
                m_stateFinder->moveBack();
            }

            if (m_stateFinder->m_count > 0)
            {
                ++m_syncing;
                m_network.send_node_message(proto::GetProofState{ m_stateFinder->getSearchHeight() });
                return finish_sync();
            }
            else
            {
                if (m_stateFinder->m_id.m_Height != MaxHeight)
                {
                    m_keyChain->rollbackConfirmedUtxo(m_stateFinder->m_id.m_Height);
                    m_knownStateID = m_stateFinder->m_id;
                }
                else
                {
                    m_knownStateID = {};
                }
                m_stateFinder.reset();
                LOG_INFO() << "Rollback completed";
            }
        }

        do_fast_forward();

        return finish_sync();
    }

    void Wallet::handle_connection_error(PeerId from)
    {
        if (auto it = m_peers.find(from); it != m_peers.end())
        {
            it->second->process_event(events::TxFailed{});
        }
    }

    void Wallet::stop_sync()
    {
        m_syncing = 0;
        copy(m_reg_requests.begin(), m_reg_requests.end(), back_inserter(m_pending_reg_requests));
        m_reg_requests.clear();
        m_pendingProofs.clear();
    }

    void Wallet::do_fast_forward()
    {
        LOG_INFO() << "Sync up to " << m_newStateID.m_Height << "-" << m_newStateID.m_Hash;
        // fast-forward
        ++m_syncing; // Mined
        m_network.send_node_message(proto::GetMined{ m_knownStateID.m_Height });

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

    bool Wallet::finish_sync()
    {
        if (m_syncing)
        {
            --m_syncing;
            if (!m_syncing)
            {
                m_keyChain->setSystemStateID(m_newStateID);
                m_knownStateID = m_newStateID;
                if (!m_pendingEvents.empty())
                {
                    Cleaner c{ m_removedNegotiators };
                    for (auto& cb : m_pendingEvents)
                    {
                        cb();
                    }
                    m_pendingEvents.clear();
                }
                m_synchronized = true;
            }
        }
        return close_node_connection();
    }

    bool Wallet::close_node_connection()
    {
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

    void Wallet::resume_negotiator(const TxDescription& tx)
    {
        Cleaner c{ m_removedNegotiators };
        auto s = make_shared<Negotiator>(*this, m_keyChain, tx);
        m_negotiators.emplace(tx.m_txId, s);
        m_peers.emplace(tx.m_peerId, s);

        if (m_synchronized)
        {
            s->start();
            s->process_event(events::TxInitiated{});
        }
        else
        {
            m_pendingEvents.emplace_back([s]()
            {
                s->start();
                s->process_event(events::TxInitiated{});
            });
        }
    }
}
