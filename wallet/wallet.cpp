#include "wallet.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include "core/ecc_native.h"
#include <algorithm>
#include <random>

namespace beam
{
    using namespace wallet;
    using namespace std;
    using namespace ECC;

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
    {

    }

    Coin::Coin(const Scalar& key, const Amount& amount, Status status, const Height& height, bool isCoinbase)
        : m_key{key}
        , m_amount{amount}
        , m_status{status}
        , m_height{height}
        , m_isCoinbase{isCoinbase}
    {

    } 
    
    Coin::Coin(const ECC::Scalar& key, const ECC::Amount& amount)
        : Coin(key, amount, Coin::Unspent, 0, false)
    {

    }

    // temporary impl of WalletToNetwork interface
    struct WalletToNetworkDummyImpl : public Wallet::ToNode
    {
        virtual void sendTransaction(const Transaction& tx)
        {
            // serealize tx and post and to the Node TX pool
            Serializer ser;
            ser & tx;

            auto buffer = ser.buffer();

            // and send buffer to other side
        }
    };

    Wallet::Wallet(IKeyChain::Ptr keyChain, INetworkIO& network)
        : m_keyChain{ keyChain }
        , m_network{ network }
    {
    }

    void Wallet::sendDummyTransaction()
    {
        // create dummy transaction here
        Transaction tx;
        m_net->sendTransaction(tx);
    }

    void Wallet::send_money(PeerId to, Amount amount)
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
        if (auto it = m_peers.find(data->m_txId); it != m_peers.end()) {
            m_network.send_tx_invitation(it->second, data);
        }
        else {
            // log
        }
        
    }

    void Wallet::send_tx_confirmation(sender::ConfirmationData::Ptr data)
    {
        if (auto it = m_peers.find(data->m_txId); it != m_peers.end()) {
            m_network.send_tx_confirmation(it->second, data);
        }
        else {
            // log
        }
    }

    void Wallet::sendChangeOutputConfirmation()
    {
        m_network.sendChangeOutputConfirmation(PeerId());
    }

    void Wallet::remove_sender(const Uuid& txId)
    {
        auto it = m_senders.find(txId);
        assert(it != m_senders.end());
        if (it != m_senders.end())
        {
            m_removedSenders.push_back(move(it->second));
            m_senders.erase(txId);
        }
    }

    void Wallet::send_tx_confirmation(receiver::ConfirmationData::Ptr data)
    {
        if (auto it = m_peers.find(data->m_txId); it != m_peers.end()) {
            m_network.send_tx_confirmation(it->second, data);
        }
    }

    void Wallet::register_tx(receiver::RegisterTxData::Ptr data)
    {
        if (auto it = m_peers.find(data->m_txId); it != m_peers.end()) {
            m_network.register_tx(it->second, data);
        }
    }

    void Wallet::send_tx_registered(UuidPtr&& txId)
    {
        if (auto it = m_peers.find(*txId); it != m_peers.end()) {
            m_network.send_tx_registered(it->second, move(txId));
        }
    }

    void Wallet::remove_receiver(const Uuid& txId)
    {
        auto it = m_receivers.find(txId);
        assert(it != m_receivers.end());
        if (it != m_receivers.end())
        {
            m_removedReceivers.push_back(move(it->second));
            m_receivers.erase(it);
        }
    }

    void Wallet::handle_tx_invitation(PeerId from, sender::InvitationData::Ptr data)
    {
        auto it = m_receivers.find(data->m_txId);
        if (it == m_receivers.end())
        {
            auto txId = data->m_txId;
            auto p = m_receivers.emplace(txId, make_unique<Receiver>(*this, m_keyChain, data));
            p.first->second->start();
        }
        else
        {
            // TODO: log unexpected TxInitation
        }
    }
    
    void Wallet::handle_tx_confirmation(PeerId from, sender::ConfirmationData::Ptr data)
    {
        auto it = m_receivers.find(data->m_txId);
        if (it != m_receivers.end())
        {
            it->second->processEvent(Receiver::TxConfirmationCompleted{data});
        }
        else
        {
            // TODO: log unexpected TxConfirmation
        }
    }

    void Wallet::handleOutputConfirmation(PeerId from)
    {
        // TODO: this code is for test only, it should be rewrited
        if (!m_receivers.empty())
        {
            m_receivers.begin()->second->processEvent(Receiver::TxOutputConfirmCompleted());
            return;
        }
        if (!m_senders.empty())
        {
            m_senders.begin()->second->processEvent(Sender::TxOutputConfirmCompleted());
            return;
        }
    }
   
    void Wallet::handle_tx_confirmation(PeerId from, receiver::ConfirmationData::Ptr data)
    {
        auto it = m_senders.find(data->m_txId);
        if (it != m_senders.end())
        {
            it->second->processEvent(Sender::TxInitCompleted{data});
        }
        else
        {
            // TODO: log unexpected TxConfirmation
        }
    }

    void Wallet::handle_tx_registration(PeerId from, UuidPtr&& txId)
    {
        if (auto it = m_receivers.find(*txId); it != m_receivers.end())
        {
            it->second->processEvent(Receiver::TxRegistrationCompleted{ *txId });
            return;
        }
        if (auto it = m_senders.find(*txId); it != m_senders.end())
        {
            it->second->processEvent(Sender::TxConfirmationCompleted());
            return;
        }
    }

    void Wallet::handle_tx_failed(PeerId from, UuidPtr&& txId)
    {
        if (auto it = m_senders.find(*txId); it != m_senders.end())
        {
            it->second->processEvent(Sender::TxFailed());
            return;
        }
        if (auto it = m_receivers.find(*txId); it != m_receivers.end())
        {
            it->second->processEvent(Receiver::TxFailed());
            return;
        }
        // TODO: log unexpected TxConfirmation
    }
}
