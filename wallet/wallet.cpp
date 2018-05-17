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
            m_network.send_tx_invitation(peer_id, move(data));
        });
    }

    void Wallet::send_tx_confirmation(sender::ConfirmationData::Ptr data)
    {
        send_tx_message(data->m_txId, [this, &data](auto peer_id) mutable
        {
            m_bool_requests_queue.push(data->m_txId);
            m_network.send_tx_confirmation(peer_id, move(data));
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

    void Wallet::send_output_confirmation(const Coin& coin)
    {
        m_network.send_output_confirmation(
            OutputConfirmationData
            {
                Input {Commitment(m_keyChain->calcKey(coin.m_id), coin.m_amount)}
              , coin.m_height
            });
    }

    void Wallet::send_tx_failed(const Uuid& txId)
    {
        send_tx_message(txId, [this](auto peer_id)
        {
            m_network.send_tx_result(peer_id, false);
        });
    }

    void Wallet::remove_sender(const Uuid& txId)
    {
        auto it = m_senders.find(txId);
        if (it != m_senders.end())
        {
            m_removedSenders.push_back(move(it->second));
            m_senders.erase(txId);
        }
    }

    void Wallet::remove_receiver(const Uuid& txId)
    {
        auto it = m_receivers.find(txId);
        if (it != m_receivers.end())
        {
            m_removedReceivers.push_back(move(it->second));
            m_receivers.erase(it);
        }
    }

    void Wallet::send_tx_confirmation(receiver::ConfirmationData::Ptr data)
    {
        send_tx_message(data->m_txId, [this, &data](auto peer_id) mutable
        {
            m_network.send_tx_confirmation(peer_id, move(data));
        });
    }

    void Wallet::register_tx(const Uuid& txId, Transaction::Ptr data)
    {
        LOG_DEBUG() << "[Receiver] sending tx for registration";
        m_bool_requests_queue.push(txId);
        m_network.register_tx(move(data));
    }

    void Wallet::send_tx_registered(UuidPtr&& txId)
    {
        send_tx_message(*txId, [this](auto peer_id)
        {
            m_network.send_tx_result(peer_id, true);
        });
    }

    void Wallet::handle_tx_invitation(PeerId from, sender::InvitationData::Ptr&& data)
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
    
    void Wallet::handle_tx_confirmation(PeerId from, sender::ConfirmationData::Ptr&& data)
    {
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

    void Wallet::handle_output_confirmation(proto::ProofUtxo&& proof)
    {
        LOG_DEBUG() << "Received tx output confirmation ";
        // TODO: this code is for test only, it should be rewrited
        if (!m_receivers.empty())
        {
            m_receivers.begin()->second->process_event(Receiver::TxOutputConfirmCompleted());
            return;
        }
        if (!m_senders.empty())
        {
            m_senders.begin()->second->process_event(Sender::TxOutputConfirmCompleted());
            return;
        }
        LOG_DEBUG() << "Unexpected tx output confirmation ";
    }
   
    void Wallet::handle_tx_confirmation(PeerId from, receiver::ConfirmationData::Ptr&& data)
    {
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

    void Wallet::handle_tx_result(bool&& res)
    {
        if (m_bool_requests_queue.empty())
        {
            LOG_DEBUG() << "Received unexpected tx registration confirmation";
            assert(m_receivers.empty() && m_senders.empty());
            return;
        }
        auto txId = m_bool_requests_queue.front();
        m_bool_requests_queue.pop();

        LOG_DEBUG() << "tx " << txId << (res ? " has registered" : " has failed to register");
        
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

    void Wallet::handle_tx_result(PeerId from, bool&& res)
    {
        handle_tx_result(move(res));
    }

    void Wallet::handle_tx_failed(PeerId from, UuidPtr&& txId)
    {
        if (auto it = m_senders.find(*txId); it != m_senders.end())
        {
            it->second->process_event(Sender::TxFailed());
            return;
        }
        if (auto it = m_receivers.find(*txId); it != m_receivers.end())
        {
            it->second->process_event(Receiver::TxFailed());
            return;
        }
        // TODO: log unexpected TxConfirmation
    }
}
