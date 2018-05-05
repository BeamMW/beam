#include "wallet.h"
#include "core/serialization_adapters.h"
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
		: m_status(Unspent)
    {

    }

    Coin::Coin(uint64_t id, Amount amount)
		: m_id(id)
        , m_amount(amount)
		, m_status(Unspent)
    {
        //m_key = Scalar::Native(key);
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

    Wallet::Wallet(IKeyChain::Ptr keyChain, NetworkIO& network)
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

    void Wallet::sendMoney(const Peer& locator, const Amount& amount)
    {
        boost::uuids::uuid id = boost::uuids::random_generator()();
        Uuid txId;
        copy(id.begin(), id.end(), txId.begin());
        auto s = make_unique<Sender>(*this, m_keyChain, txId, amount );
        auto p = m_senders.emplace(txId, move(s));
        p.first->second->start();
    }

    void Wallet::sendTxInitiation(sender::InvitationData::Ptr data)
    {
        m_network.sendTxInitiation(Peer(), data);
    }

    void Wallet::sendTxConfirmation(sender::ConfirmationData::Ptr data)
    {
        m_network.sendTxConfirmation(Peer(), data);
    }

    void Wallet::sendChangeOutputConfirmation()
    {
        m_network.sendChangeOutputConfirmation(Peer());
    }

    void Wallet::removeSender(const Uuid& txId)
    {
        auto it = m_senders.find(txId);
        assert(it != m_senders.end());
        if (it != m_senders.end())
        {
            m_removedSenders.push_back(move(it->second));
            m_senders.erase(txId);
        }
    }

    void Wallet::sendTxConfirmation(receiver::ConfirmationData::Ptr data)
    {
        m_network.sendTxConfirmation(Peer(), data);
    }

    void Wallet::registerTx(const Uuid& txId, TransactionPtr transaction)
    {
        m_network.registerTx(Peer(), txId, transaction);
    }

    void Wallet::sendTxRegistered(const Uuid& txId)
    {
        m_network.sendTxRegistered(Peer(), txId);
    }

    void Wallet::removeReceiver(const Uuid& txId)
    {
        auto it = m_receivers.find(txId);
        assert(it != m_receivers.end());
        if (it != m_receivers.end())
        {
            m_removedReceivers.push_back(move(it->second));
            m_receivers.erase(it);
        }
    }

    void Wallet::handleTxInitiation(sender::InvitationData::Ptr data)
    {
        auto it = m_receivers.find(data->m_txId);
        if (it == m_receivers.end())
        {
            auto p = m_receivers.emplace(data->m_txId, make_unique<Receiver>(*this, m_keyChain, data));
            p.first->second->start();
        }
        else
        {
            // TODO: log unexpected TxInitation
        }
    }
    
    void Wallet::handleTxConfirmation(sender::ConfirmationData::Ptr data)
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

    void Wallet::handleOutputConfirmation(const Peer& peer)
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
   
    void Wallet::handleTxConfirmation(receiver::ConfirmationData::Ptr data)
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

    void Wallet::handleTxRegistration(const Uuid& txId)
    {
        {
            auto it = m_receivers.find(txId);
            if (it != m_receivers.end())
            {
                it->second->processEvent(Receiver::TxRegistrationCompleted{ txId });
                return;
            }
        }
        {
            auto it = m_senders.find(txId);
            if (it != m_senders.end())
            {
                it->second->processEvent(Sender::TxConfirmationCompleted());
                return;
            }
        }
    }

    void Wallet::handleTxFailed(const Uuid& txId)
    {
        auto sit = m_senders.find(txId);
        if (sit != m_senders.end())
        {
            sit->second->processEvent(Sender::TxFailed());
            return;
        }
        auto rit = m_receivers.find(txId);
        if (rit != m_receivers.end())
        {
            rit->second->processEvent(Receiver::TxFailed());
            return;
        }
        // TODO: log unexpected TxConfirmation
    }
}
