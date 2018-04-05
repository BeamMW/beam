#include "wallet.h"

namespace beam
{
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
        // create dummy transaction here, serealize it and post to the Node TX pool
        Transaction tx;

        ByteBuffer buffer;
        tx.serializeTo(buffer);

        // and call something like net.post("/pool/push", buffer)
    }
}
