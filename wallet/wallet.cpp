#include "wallet.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include "core/ecc_native.h"
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
        const string_view beams{" beams" };
        const string_view chattles{ " chattles" };
        auto width = os.width();
        if (amount.m_value > Block::Rules::Coin)
        {
            os << setw(width - beams.length()) << Amount(amount.m_value / Block::Rules::Coin) << beams.data();
        }
        else
        {
            os << setw(width - chattles.length()) << amount.m_value << chattles.data();
        }
        return os;
    }

    Scalar::Native generateNonce()
    {
        Scalar nonce;
        Scalar::Native n;
        random_device r;
        default_random_engine e{ r() };
        uniform_int_distribution<uint32_t> d;
        uint32_t *p = reinterpret_cast<uint32_t*>(nonce.m_Value.m_pData);
        constexpr size_t const count = sizeof(nonce.m_Value.m_pData) / sizeof(uint32_t);
        while (true)
        {
            generate(p, p + count, [&d, &e] { return d(e); });
            // better generator should be used
            //generate(nonce.m_Value.m_pData, nonce.m_Value.m_pData + sizeof(nonce.m_Value.m_pData), rand);
            if (!n.Import(nonce))
            {
                break;
            }
        }
        return n;
    }

    pair<Scalar::Native, Scalar::Native> split_key(const Scalar::Native& key, uint64_t index)
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

    Wallet::Wallet(IKeyChain::Ptr keyChain, INetworkIO& network, TxCompletedAction&& action)
        : m_keyChain{ keyChain }
        , m_network{ network }
        , m_tx_completed_action{move(action)}
        , m_syncing{1}
        , m_knownStateID{0}
    {
        assert(keyChain);
        m_keyChain->getSystemStateID(m_knownStateID);
    }

    Wallet::~Wallet()
    {
        assert(m_peers.empty());
        assert(m_receivers.empty());
        assert(m_senders.empty());
        assert(m_node_requests_queue.empty());
        assert(m_removed_senders.empty());
        assert(m_removed_receivers.empty());
    }

    void Wallet::transfer_money(PeerId to, Amount&& amount)
    {
        Cleaner<std::vector<wallet::Sender::Ptr> > c{ m_removed_senders };
		boost::uuids::uuid id = boost::uuids::random_generator()();
        Uuid txId;
        copy(id.begin(), id.end(), txId.begin());
        m_peers.emplace(txId, to);
        auto s = make_unique<Sender>(*this, m_keyChain, txId, amount);
        auto p = m_senders.emplace(txId, move(s));
        if (!m_syncing)
        {
            p.first->second->start();
        }
    }

    void Wallet::send_tx_invitation(sender::InvitationData::Ptr data)
    {
        send_tx_message(data->m_txId, [this, &data](auto peer_id) mutable
        {
            m_network.send_tx_message(peer_id, move(data));
        });
    }

    void Wallet::send_tx_confirmation(sender::ConfirmationData::Ptr data)
    {
        send_tx_message(data->m_txId, [this, &data](auto peer_id) mutable
        {
            m_network.send_tx_message(peer_id, move(data));
        });
    }

    void Wallet::on_tx_completed(const Uuid& txId)
    {
        remove_sender(txId);
        remove_receiver(txId);
        if (m_tx_completed_action)
        {
            m_tx_completed_action(txId);
        }
    }


    void Wallet::send_tx_failed(const Uuid& txId)
    {
        send_tx_message(txId, [this](auto peer_id)
        {
            m_network.send_tx_message(peer_id, wallet::TxRegisteredData{ false });
        });
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

    void Wallet::send_tx_confirmation(receiver::ConfirmationData::Ptr data)
    {
        send_tx_message(data->m_txId, [this, &data](auto peer_id) mutable
        {
            m_network.send_tx_message(peer_id, move(data));
        });
    }

    void Wallet::register_tx(const Uuid& txId, Transaction::Ptr data)
    {
        LOG_VERBOSE() << ReceiverPrefix << "sending tx for registration";
        m_node_requests_queue.push(txId);
        m_network.send_node_message(proto::NewTransaction{ move(data) });
    }

    void Wallet::send_tx_registered(UuidPtr&& txId)
    {
        send_tx_message(*txId, [this](auto peer_id)
        {
            m_network.send_tx_message(peer_id, wallet::TxRegisteredData{ true });
        });
    }

    void Wallet::handle_tx_message(PeerId from, sender::InvitationData::Ptr&& data)
    {
        auto it = m_receivers.find(data->m_txId);
        if (it == m_receivers.end())
        {
            LOG_VERBOSE() << ReceiverPrefix << "Received tx invitation " << data->m_txId;

            auto txId = data->m_txId;
            m_peers.emplace(txId, from);
            auto p = m_receivers.emplace(txId, make_unique<Receiver>(*this, m_keyChain, data));
            if (!m_syncing)
            {
                p.first->second->start();
            }
        }
        else
        {
            LOG_DEBUG() << ReceiverPrefix << "Unexpected tx invitation " << data->m_txId;
        }
    }
    
    void Wallet::handle_tx_message(PeerId from, sender::ConfirmationData::Ptr&& data)
    {
        Cleaner<std::vector<wallet::Receiver::Ptr> > c{ m_removed_receivers };
        auto it = m_receivers.find(data->m_txId);
        if (it != m_receivers.end())
        {
            LOG_DEBUG() << ReceiverPrefix << "Received sender tx confirmation " << data->m_txId;
            it->second->process_event(Receiver::TxConfirmationCompleted{data});
        }
        else
        {
            LOG_DEBUG() << ReceiverPrefix << "Unexpected sender tx confirmation "<< data->m_txId;
            m_network.close_connection(from);
        }
    }

    void Wallet::handle_tx_message(PeerId /*from*/, receiver::ConfirmationData::Ptr&& data)
    {
        Cleaner<std::vector<wallet::Sender::Ptr> > c{ m_removed_senders };
        auto it = m_senders.find(data->m_txId);
        if (it != m_senders.end())
        {
            LOG_VERBOSE() << SenderPrefix << "Received tx confirmation " << data->m_txId;
            it->second->process_event(Sender::TxInitCompleted{data});
        }
        else
        {
            LOG_DEBUG() << SenderPrefix << "Unexpected tx confirmation " << data->m_txId;
        }
    }

    void Wallet::handle_tx_message(PeerId from, wallet::TxRegisteredData&& data)
    {
        // TODO: change data structure
        auto cit = find_if(m_peers.cbegin(), m_peers.cend(), [from](const auto& p) {return p.second == from; });
        if (cit == m_peers.end())
        {
            return;
        }
        handle_tx_registered(cit->first, data.m_value);
    }

    void Wallet::handle_node_message(proto::Boolean&& res)
    {
        if (m_node_requests_queue.empty())
        {
            LOG_DEBUG() << "Received unexpected tx registration confirmation";
            assert(m_receivers.empty() && m_senders.empty());
            return;
        }
        auto txId = m_node_requests_queue.front();
        m_node_requests_queue.pop();
        handle_tx_registered(txId, res.m_Value);
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
    }

    void Wallet::handle_node_message(proto::ProofUtxo&& proof)
    {
        // TODO: handle the maturity of the several proofs (> 1)
        boost::optional<Coin> found;

        for (const auto& proof : proof.m_Proofs)
        {
            Input::Count count = proof.m_Count;
            m_keyChain->visit([&](const Coin& coin)->bool
            {
                if (coin.m_status == Coin::Unconfirmed)
                {
                    Input input{ Commitment(m_keyChain->calcKey(coin), coin.m_amount) };
                
                    if (proof.IsValid(input, m_Definition))
                    {
                        found = coin;
                        found->m_status = Coin::Unspent;
                        
                        if (--count)
                        {
                            return true;
                        }
                        return false;
                    }
                }

                return true;
            });
        }

        if (found)
        {
            m_keyChain->update(vector<Coin>{*found});
        }
        else
        {
            LOG_ERROR() << "Got empty proof";
        }
        finishSync();
    }

    void Wallet::handle_node_message(proto::NewTip&& msg)
    {
        // TODO: check if we're already waiting for the ProofUtxo,
        // don't send request if yes

        if (msg.m_ID > m_knownStateID)
        {
            m_newStateID = msg.m_ID;
            ++m_syncing;
            m_network.send_node_message(proto::GetMined{ m_knownStateID.m_Height });
        }
    }

    void Wallet::handle_node_message(proto::Hdr&& msg)
    {
		m_Definition = msg.m_Description.m_Definition;

        // TODO: do one kernel proof instead many per coin proofs
        m_keyChain->visit([&](const Coin& coin)
        {
            if (coin.m_status == Coin::Unconfirmed)
            {
                ++m_syncing;
                m_network.send_node_message(
                    proto::GetProofUtxo
                    {
                        Input{ Commitment(m_keyChain->calcKey(coin), coin.m_amount) }
                        , coin.m_height
                    });
            }

            return true;
        });

        Block::SystemState::ID newID = {0};
        msg.m_Description.get_ID(newID);
        m_newStateID = newID;
        finishSync();
    }

    void Wallet::handle_node_message(proto::Mined&& msg)
    {
        vector<Coin> mined;
        auto currentHeight = m_keyChain->getCurrentHeight();
        for (auto& minedCoin : msg.m_Entries)
        {
            if (minedCoin.m_Active && minedCoin.m_ID.m_Height >= currentHeight) // we store coins from active branch
            {
                Amount reward = Block::Rules::CoinbaseEmission;
                // coinbase 
                mined.emplace_back(Block::Rules::CoinbaseEmission, Coin::Unspent, minedCoin.m_ID.m_Height, KeyType::Coinbase);
                if (minedCoin.m_Fees > 0)
                {
                    reward += minedCoin.m_Fees;
                    mined.emplace_back(minedCoin.m_Fees, Coin::Unspent, minedCoin.m_ID.m_Height, KeyType::Comission);
                }
                LOG_INFO() << "Block reward received: " << PrintableAmount(reward);
            }
        }

		if (!mined.empty())
		{
			m_keyChain->store(mined);
		}
        finishSync();
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

    void Wallet::remove_peer(const Uuid& txId)
    {
        auto it = m_peers.find(txId);
        if (it != m_peers.end())
        {
            m_network.close_connection(it->second);
            m_peers.erase(it);
        }
    }

    void Wallet::finishSync()
    {
        if (m_syncing)
        {
            --m_syncing;
            if (!m_syncing)
            {
                m_keyChain->setSystemStateID(m_newStateID);
                m_knownStateID = m_newStateID;
                for (auto& s : m_senders)
                {
                    s.second->start();
                }
                for (auto& r : m_receivers)
                {
                    r.second->start();
                }
            }
        }
    }
}
