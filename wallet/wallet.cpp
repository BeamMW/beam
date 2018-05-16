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
        , m_node_id{ 0 }
        , m_tx_completed_action{move(action)}
#ifndef NDEBUG
        , m_tid{this_thread::get_id()}
#endif
    {
    }

    void Wallet::send_money(PeerId to, Amount&& amount)
    {
        check_thread();
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
        check_thread();
        if (auto it = m_peers.find(data->m_txId); it != m_peers.end()) {
            m_network.send_tx_invitation(it->second, move(data));
        }
        else {
            assert(false && "no peers");
        }
        
    }

    void Wallet::send_tx_confirmation(sender::ConfirmationData::Ptr data)
    {
        check_thread();
        if (auto it = m_peers.find(data->m_txId); it != m_peers.end()) {
            m_network.send_tx_confirmation(it->second, move(data));
        }
        else {
            assert(false && "no peers");
        }
    }

    void Wallet::on_tx_completed(const Uuid& txId)
    {
        check_thread();
        remove_sender(txId);
        remove_receiver(txId);
        if (m_tx_completed_action) {
            m_tx_completed_action(txId);
        }
    }

    void Wallet::send_output_confirmation()
    {
        check_thread();
        m_network.send_output_confirmation(m_node_id, None{});
    }

    void Wallet::remove_sender(const Uuid& txId)
    {
        check_thread();
        auto it = m_senders.find(txId);
        if (it != m_senders.end()) {
            m_removedSenders.push_back(move(it->second));
            m_senders.erase(txId);
        }
    }

    void Wallet::remove_receiver(const Uuid& txId)
    {
        check_thread();
        auto it = m_receivers.find(txId);
        if (it != m_receivers.end()) {
            m_removedReceivers.push_back(move(it->second));
            m_receivers.erase(it);
        }
    }

    void Wallet::send_tx_confirmation(receiver::ConfirmationData::Ptr data)
    {
        check_thread();
        if (auto it = m_peers.find(data->m_txId); it != m_peers.end()) {
            m_network.send_tx_confirmation(it->second, move(data));
        }
        else {
            assert(false && "no peers");
        }
    }

    void Wallet::register_tx(const Uuid& txId, Transaction::Ptr data)
    {
        check_thread();
        m_registration_queue.push(txId);
        m_network.register_tx(move(data));
    }

    void Wallet::send_tx_registered(UuidPtr&& txId)
    {
        check_thread();
        if (auto it = m_peers.find(*txId); it != m_peers.end()) {
            m_network.send_tx_registered(it->second, move(txId));
        }
        else {
            assert(false && "no peers");
        }
    }

    void Wallet::handle_tx_invitation(PeerId from, sender::InvitationData::Ptr&& data)
    {
        check_thread();
        auto it = m_receivers.find(data->m_txId);
        if (it == m_receivers.end())
        {
            LOG_DEBUG() << "[Sender] Got tx invitation " << data->m_txId;
            auto txId = data->m_txId;
            m_peers.emplace(txId, from);
            auto p = m_receivers.emplace(txId, make_unique<Receiver>(*this, m_keyChain, data));
            p.first->second->start();
        }
        else
        {
            LOG_DEBUG() << "[Sender] Unexpected tx invitation " << data->m_txId;
        }
    }
    
    void Wallet::handle_tx_confirmation(PeerId from, sender::ConfirmationData::Ptr&& data)
    {
        check_thread();
        auto it = m_receivers.find(data->m_txId);
        if (it != m_receivers.end())
        {
            it->second->processEvent(Receiver::TxConfirmationCompleted{data});
        }
        else
        {
            LOG_DEBUG() << "Unexpected sender tx confirmation "<< data->m_txId;
        }
    }

    void Wallet::handle_output_confirmation(PeerId from, None&&)
    {
        check_thread();
        // TODO: this code is for test only, it should be rewrited
        if (!m_receivers.empty()) {
            m_receivers.begin()->second->processEvent(Receiver::TxOutputConfirmCompleted());
            return;
        }
        if (!m_senders.empty()) {
            m_senders.begin()->second->processEvent(Sender::TxOutputConfirmCompleted());
            return;
        }
    }
   
    void Wallet::handle_tx_confirmation(PeerId from, receiver::ConfirmationData::Ptr&& data)
    {
        check_thread();
        auto it = m_senders.find(data->m_txId);
        if (it != m_senders.end())
        {
            LOG_DEBUG() << "[Receiver] Got tx confirmation " << data->m_txId;
            it->second->processEvent(Sender::TxInitCompleted{data});
        }
        else
        {
            LOG_DEBUG() << "[Receiver] Unexpected tx confirmation " << data->m_txId;
        }
    }

    void Wallet::handle_tx_registration(bool&& res)
    {
        check_thread();
        if (m_registration_queue.empty())
        {
            LOG_DEBUG() << "Received unexpected tx registration confirmation";
            assert(m_receivers.empty() && m_senders.empty());
            return;
        }
        auto txId = m_registration_queue.front();
        m_registration_queue.pop();

        LOG_DEBUG() << "tx " << txId << (res ? "registered" : "failed to register");
        
        if (res)
        {
            if (auto it = m_receivers.find(txId); it != m_receivers.end())
            {
                it->second->processEvent(Receiver::TxRegistrationCompleted{ txId });
                return;
            }
            if (auto it = m_senders.find(txId); it != m_senders.end())
            {
                it->second->processEvent(Sender::TxConfirmationCompleted());
                return;
            }
        }
        else
        {
            if (auto it = m_senders.find(txId); it != m_senders.end())
            {
                it->second->processEvent(Sender::TxFailed());
                return;
            }
            if (auto it = m_receivers.find(txId); it != m_receivers.end())
            {
                it->second->processEvent(Receiver::TxFailed());
                return;
            }
        }
    }

    void Wallet::handle_tx_failed(PeerId from, UuidPtr&& txId)
    {
        check_thread();
        if (auto it = m_senders.find(*txId); it != m_senders.end()) {
            it->second->processEvent(Sender::TxFailed());
            return;
        }
        if (auto it = m_receivers.find(*txId); it != m_receivers.end()) {
            it->second->processEvent(Receiver::TxFailed());
            return;
        }
        // TODO: log unexpected TxConfirmation
    }
}
