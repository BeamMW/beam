#include "wallet.h"
#include "core/serialization_adapters.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include "core/ecc_native.h"
#include <algorithm>

namespace beam
{
    using namespace wallet;
    Coin::Coin()
    {

    }

    Coin::Coin(const ECC::Scalar& key, ECC::Amount amount)
        : m_amount(amount)
    {
        m_key = ECC::Scalar::Native(key);
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

    void Wallet::sendMoney(const Peer& locator, const ECC::Amount& amount)
    {
        boost::uuids::uuid id = boost::uuids::random_generator()();
        Uuid txId;
        std::copy(id.begin(), id.end(), txId.begin());
        auto s = std::make_unique<Sender>(*this, txId, m_keyChain, amount );
        auto p = m_senders.emplace(txId, std::move(s));
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
            m_removedSenders.push_back(std::move(it->second));
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
            m_removedReceivers.push_back(std::move(it->second));
            m_receivers.erase(it);
        }
    }

    void Wallet::handleTxInitiation(sender::InvitationData::Ptr data)
    {
        auto it = m_receivers.find(data->m_txId);
        if (it == m_receivers.end())
        {
            auto p = m_receivers.emplace(data->m_txId, std::make_unique<Receiver>(*this, data));
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
