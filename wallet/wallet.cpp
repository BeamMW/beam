#include "wallet.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include "core/ecc_native.h"
#include "utility/logger.h"
#include "utility/helpers.h"
#include <algorithm>
#include <random>

// Valdo's point generator of elliptic curve
namespace ECC {
	Context g_Ctx;
	const Context& Context::get() { return g_Ctx; }
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

    Coin::Coin()
		: m_status(Unspent)
    {

    }

    Coin::Coin(uint64_t id, const Amount& amount, Status status, const Height& height, bool isCoinbase)
        : m_id{id}
        , m_amount{amount}
        , m_status{status}
        , m_height{height}
        , m_isCoinbase{isCoinbase}
    {

    } 

    Wallet::Wallet(IKeyChain::Ptr keyChain, INetworkIO& network, TxCompletedAction&& action)
        : m_keyChain{ keyChain }
        , m_network{ network }
        , m_tx_completed_action{move(action)}
    {
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
        boost::uuids::uuid id = boost::uuids::random_generator()();
        Uuid txId;
        Height height = 0;
        copy(id.begin(), id.end(), txId.begin());
        m_peers.emplace(txId, to);
        auto s = make_unique<Sender>(*this, m_keyChain, txId, amount, height);
        auto p = m_senders.emplace(txId, move(s));
        p.first->second->start();
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

    void Wallet::send_output_confirmation(const Uuid& txId, const Coin& coin)
    {
        m_node_requests_queue.push(txId);
        m_network.send_node_message(
            proto::GetProofUtxo
            {
                Input {Commitment(m_keyChain->calcKey(coin.m_id), coin.m_amount)}
              , coin.m_height
            });
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
        LOG_DEBUG() << "[Receiver] sending tx for registration";
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
            LOG_DEBUG() << "[Receiver] Received tx invitation " << data->m_txId;
            auto txId = data->m_txId;
            m_peers.emplace(txId, from);
            auto p = m_receivers.emplace(txId, make_unique<Receiver>(*this, m_keyChain, data));
            p.first->second->start();
        }
        else
        {
            LOG_DEBUG() << "[Receiver] Unexpected tx invitation " << data->m_txId;
        }
    }
    
    void Wallet::handle_tx_message(PeerId from, sender::ConfirmationData::Ptr&& data)
    {
        Cleaner c{ m_removed_receivers };
        auto it = m_receivers.find(data->m_txId);
        if (it != m_receivers.end())
        {
            LOG_DEBUG() << "[Receiver] Received sender tx confirmation " << data->m_txId;
            it->second->process_event(Receiver::TxConfirmationCompleted{data});
        }
        else
        {
            LOG_DEBUG() << "[Receiver] Unexpected sender tx confirmation "<< data->m_txId;
        }
    }

    void Wallet::handle_tx_message(PeerId from, receiver::ConfirmationData::Ptr&& data)
    {
        Cleaner c{ m_removed_senders };
        auto it = m_senders.find(data->m_txId);
        if (it != m_senders.end())
        {
            LOG_DEBUG() << "[Sender] Received tx confirmation " << data->m_txId;
            it->second->process_event(Sender::TxInitCompleted{data});
        }
        else
        {
            LOG_DEBUG() << "[Sender] Unexpected tx confirmation " << data->m_txId;
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
        Cleaner cr{ m_removed_receivers };
        Cleaner cs{ m_removed_senders };
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
        LOG_DEBUG() << "Received tx output confirmation ";
        if (m_node_requests_queue.empty())
        {
            LOG_DEBUG() << "Received unexpected utxo proof";
            assert(m_receivers.empty() && m_senders.empty());
            return;
        }
        auto txId = m_node_requests_queue.front();
        m_node_requests_queue.pop();
        Cleaner cr{ m_removed_receivers };
        Cleaner cs{ m_removed_senders };
        if (auto it = m_receivers.find(txId); it != m_receivers.end())
        {
            m_receivers.begin()->second->process_event(Receiver::TxOutputConfirmCompleted());
            return;
        }
        if (auto it = m_senders.find(txId); it != m_senders.end())
        {
            m_senders.begin()->second->process_event(Sender::TxOutputConfirmCompleted());
            return;
        }
        LOG_DEBUG() << "Unexpected tx output confirmation ";
    }

    void Wallet::handle_connection_error(PeerId from)
    {
        // TODO: change data structure
        auto cit = find_if(m_peers.cbegin(), m_peers.cend(), [from](const auto& p) {return p.second == from; });
        if (cit == m_peers.end())
        {
            return;
        }
        Cleaner cr{ m_removed_receivers };
        Cleaner cs{ m_removed_senders };
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
}
