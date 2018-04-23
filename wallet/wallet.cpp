#include "wallet.h"
#include "core/serialization_adapters.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include "core/ecc_native.h"
#include <algorithm>

namespace ECC
{
    Context g_Ctx;
    const Context& Context::get() { return g_Ctx; }
}

namespace beam
{
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
        //m_network.addListener(this);
    }

    void Wallet::sendDummyTransaction()
    {
        // create dummy transaction here
        Transaction tx;
        m_net->sendTransaction(tx);
    }

    void Wallet::sendMoney(const PeerLocator& locator, const ECC::Amount& amount)
    {
        std::lock_guard<std::mutex> lock{ m_sendersMutex };
        boost::uuids::uuid id = boost::uuids::random_generator()();
        Uuid txId;
        std::copy(id.begin(), id.end(), txId.begin());
        auto s = std::make_unique<wallet::Sender>(*this, txId, m_keyChain, amount );
        auto [it, _] = m_senders.emplace(txId, std::move(s));
        it->second->start();
    }

    void Wallet::sendTxInitiation(wallet::sender::InvitationData::Ptr data)
    {
        m_network.sendTxInitiation(PeerLocator(), std::move(data));
    }

    void Wallet::sendTxConfirmation(wallet::sender::ConfirmationData::Ptr data)
    {
        m_network.sendTxConfirmation(PeerLocator(), data);
    }

    void Wallet::sendChangeOutputConfirmation()
    {
        m_network.sendChangeOutputConfirmation(PeerLocator());
    }

    void Wallet::sendTxConfirmation(wallet::receiver::ConfirmationData::Ptr data)
    {
        m_network.sendTxConfirmation(PeerLocator(), data);
    }

    void Wallet::registerTx(const Transaction& transaction)
    {
        m_network.registerTx(PeerLocator(), transaction);
    }

    void Wallet::handleTxInitiation(wallet::sender::InvitationData::Ptr data)
    {
        std::lock_guard<std::mutex> lock{ m_receiversMutex };
        auto it = m_receivers.find(data->m_txId);
        if (it == m_receivers.end())
        {
            auto [it, _] = m_receivers.emplace(data->m_txId, std::make_unique<wallet::Receiver>(*this, data));
            it->second->start();
        }
        else
        {
            // TODO: log unexpected TxInitation
        }
    }
    
    void Wallet::handleTxConfirmation(wallet::sender::ConfirmationData::Ptr data)
    {
        std::lock_guard<std::mutex> lock{ m_receiversMutex };
        auto it = m_receivers.find(data->m_txId);
        if (it != m_receivers.end())
        {
            it->second->enqueueEvent(wallet::Receiver::TxConfirmationCompleted{data});
        }
        else
        {
            // TODO: log unexpected TxConfirmation
        }
    }
   
    void Wallet::handleTxConfirmation(wallet::receiver::ConfirmationData::Ptr data)
    {
        std::lock_guard<std::mutex> lock{ m_sendersMutex };
        auto it = m_senders.find(data->m_txId);
        if (it != m_senders.end())
        {
            it->second->enqueueEvent(wallet::Sender::TxInitCompleted{data});
        }
        else
        {
            // TODO: log unexpected TxConfirmation
        }
    }

    void Wallet::handleTxRegistration(const Transaction& tx)
    {
        std::lock_guard<std::mutex> lock{ m_receiversMutex };
        if (!m_receivers.empty())
        {
            m_receivers.begin()->second->enqueueEvent(wallet::Receiver::TxRegistrationCompleted());
        }
    }

    void Wallet::pumpEvents()
    {
        {
            std::lock_guard<std::mutex> lock{ m_sendersMutex };
            for (auto& s : m_senders)
            {
                s.second->executeQueuedEvents();
            }
        }
        {
            std::lock_guard<std::mutex> lock{ m_receiversMutex };
            for (auto& r : m_receivers)
            {
                r.second->executeQueuedEvents();
            }
        }
    }
}
