#include "wallet.h"
#include "core/serialization_adapters.h"

namespace beam
{
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

    Wallet::Wallet(ToWallet::Shared receiver)
        : m_receiver(receiver)
    {

    }

    Wallet::Wallet()
        : m_net(std::make_unique<WalletToNetworkDummyImpl>())
    {

    }

    void Wallet::sendDummyTransaction()
    {
        // create dummy transaction here
        Transaction tx;
        m_net->sendTransaction(tx);
    }

    Wallet::Result Wallet::sendInvitation(const PartialTx& tx)
    {
        auto partialTx = m_receiver->handleInvitation(tx);

        if(partialTx.m_phase == ReceiverInitiation)
        {
            partialTx.m_phase = SenderConfirmation;

            return sendConfirmation(partialTx);
        }
        else
        {
            // error/rollback
        }

        return false;
    }

    Wallet::Result Wallet::sendConfirmation(const PartialTx& tx)
    {
        auto partialTx = m_receiver->handleConfirmation(tx);

        if(partialTx.m_phase == ReceiverConfirmation)
        {
            // all is ok, the money was sent
            // update wallet

            return true;
        }
        else
        {
            // error/rollback
        }

        return false;
    }

    Wallet::PartialTx Wallet::createInitialPartialTx()
    {
        PartialTx tx;
        tx.m_phase = SenderInitiation;

        return tx;
    }

    Wallet::Result Wallet::sendMoneyTo(const Config& config, uint64_t amount)
    {
        auto initialPartialTx = createInitialPartialTx();
        //lockInputs(partialTx.inputs);
        //storeChangeOutputs(partialTx.changeOutputs);

        return sendInvitation(initialPartialTx);
    }

    Wallet::PartialTx Wallet::ToWallet::handleInvitation(const PartialTx& tx)
    {
        PartialTx res = tx;

        if(tx.m_phase == SenderInitiation)
        {
            // do all the job

            res.m_phase = ReceiverInitiation;
        }

        return res;
    }

    Wallet::PartialTx Wallet::ToWallet::handleConfirmation(const PartialTx& tx)
    {
        PartialTx res = tx;

        if(tx.m_phase == SenderConfirmation)
        {
            // do all the job

            res.m_phase = ReceiverConfirmation;
        }

        return res;
    }

}
