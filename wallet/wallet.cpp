#include "wallet.h"
#include "core/serialization_adapters.h"

namespace beam
{
    // temporary impl of WalletToNetwork interface
    struct WalletToNetworkDummyImpl : public Wallet::ToNetwork
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

    Wallet::Wallet()
        : m_net(std::make_unique<WalletToNetworkDummyImpl>())
    {

    }

    void Wallet::Sender::sendInvitation()
    {
        
    }

    void Wallet::Sender::sendConfirmation()
    {
        
    }

    void Wallet::Receiver::handleInvitation()
    {
        
    }

    void Wallet::Receiver::handleConfirmation()
    {
    }

    void Wallet::sendDummyTransaction()
    {
        // create dummy transaction here
        Transaction tx;
        m_net->sendTransaction(tx);
    }
}
